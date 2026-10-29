# Copyright (c) 2026 dev4fun. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is designed to work with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms, as
# designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have either included with
# the program or referenced in the documentation.
# This program is distributed in the hope that it will be useful,  but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
# the GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

import tempfile
import unittest
from collections import namedtuple
from decimal import Decimal
from pathlib import Path
from unittest.mock import MagicMock, Mock, patch

import pymysql
from fastapi import FastAPI
from fastapi.testclient import TestClient

from app.api.connections import create_connections_router
from app.api.sql import create_sql_router
from app.database.config import DatabaseConnectionConfig
from app.database.connection_import import parse_connections_xml
from app.database.connection_store import ConnectionProfile, ConnectionStore
from app.database.session import SqlSession
from app.database.session_manager import SessionManager
from app.models.responses import cell_to_json
from app.plugins.base import ConnectorPlugin, TimeSeriesQuerySpec, UnsupportedCapabilityError, is_read_only_statement
from app.plugins.loader import load_plugins
from app.services.sql_executor import execute_sql

PLUGINS = load_plugins()
MYSQL = PLUGINS.for_type("mysql")
PROFILE = ConnectionProfile(id="demo", name="Demo", type="mysql", host="localhost", port=3306, username="root")
SAMPLE_XML = """<?xml version="1.0"?>
<data grt_format="2.0">
  <value type="list" content-type="object" content-struct-name="db.mgmt.Connection">
    <value type="object" struct-name="db.mgmt.Connection" id="{14D59616-AC22-414E-959F-D1AEEDC390A3}">
      <link type="object" struct-name="db.mgmt.Driver" key="driver">com.mysql.rdbms.mysql.driver.native</link>
      <value type="string" key="hostIdentifier">Mysql@127.0.0.1:33082</value>
      <value type="int" key="isDefault">1</value>
      <value type="dict" key="parameterValues">
        <value type="string" key="hostName">127.0.0.1</value>
        <value type="int" key="lastConnected">1793202095</value>
        <value type="string" key="password">secret</value>
        <value type="int" key="port">33082</value>
        <value type="string" key="schema">sakila</value>
        <value type="string" key="serverVersion">8.2.0</value>
        <value type="int" key="useSSL">1</value>
        <value type="string" key="userName">root</value>
      </value>
      <value type="string" key="name">test</value>
    </value>
    <value type="object" struct-name="db.mgmt.Connection" id="{4AE55273-3D1F-4A34-BFD2-47043DB15172}">
      <link type="object" struct-name="db.mgmt.Driver" key="driver">com.oracle.rdbms.driver</link>
      <value type="string" key="name">unsupported</value>
    </value>
  </value>
</data>"""


class StreamingCursor:
    def __init__(self):
        self.description = [("value", 3)]
        self.warning_count = 0
        self.rowcount = 0
        self.unread = False
        self.position = 0
        self.total = 0

    def execute(self, sql):
        if self.unread:
            raise RuntimeError("Unread result found")
        if sql == "INVALID":
            raise pymysql.ProgrammingError(1064, "Invalid SQL")
        self.total = 1001 if sql == "LARGE" else 1
        if sql.startswith("SELECT `q`.* FROM (UPDATE"):
            raise AssertionError("non read-only query must be rejected before execution")
        self.position = 0
        self.unread = True

    def fetchmany(self, size):
        end = min(self.total, self.position + size)
        rows = [(i,) for i in range(self.position, end)]
        self.position = end
        if not rows:
            self.unread = False
        return rows

    def nextset(self):
        if self.unread:
            raise RuntimeError("Unread result found")
        return None

    def close(self):
        if self.unread:
            raise RuntimeError("Unread result found")


class SqlTests(unittest.TestCase):
    def setUp(self):
        self.cursor = StreamingCursor()
        self.connection = Mock(open=True)
        self.connection.cursor.return_value = self.cursor
        self.session = SqlSession("test", self.connection, PROFILE, MYSQL, result_limit=10)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.store = ConnectionStore(Path(self.temp.name) / "connection.json")

    def test_truncation_drains_result_and_next_query_works(self):
        result = execute_sql(self.session, "LARGE")
        self.assertEqual(result["returnedRows"], 10)
        self.assertTrue(result["truncated"])
        self.assertEqual(self.cursor.position, 1001)
        self.assertEqual(execute_sql(self.session, "SMALL")["rows"], [[0]])

    def test_exact_limit_is_not_truncated(self):
        self.session.result_limit = 1
        self.assertFalse(execute_sql(self.session, "SMALL")["truncated"])

    def test_sql_error_preserves_connection(self):
        result = execute_sql(self.session, "INVALID")
        self.assertEqual(result["error"]["code"], 1064)
        self.assertEqual(execute_sql(self.session, "SMALL")["type"], "resultset")
        self.connection.close.assert_not_called()

    def test_cursor_creation_error_is_normalized(self):
        self.connection.cursor.side_effect = pymysql.OperationalError(2006, "Server gone away")
        self.assertEqual(execute_sql(self.session, "SELECT 1")["error"]["code"], 2006)

    def test_exact_values_and_binary(self):
        self.assertEqual(cell_to_json(Decimal("1234567890.123456789")), "1234567890.123456789")
        self.assertEqual(cell_to_json(2**63 - 1), str(2**63 - 1))
        self.assertEqual(cell_to_json(b"abc"), {"binary": True, "size": 3})
        self.assertIsNone(cell_to_json(None))
        self.assertEqual(cell_to_json(float("inf")), "inf")

    def test_multiple_results_and_commands(self):
        cursor = Mock(description=[("n", 3)], warning_count=2, rowcount=1)
        cursor.fetchmany.side_effect = [[(1,)], []]
        def advance():
            if cursor.description:
                cursor.description = None
                return True
            return None
        cursor.nextset.side_effect = advance
        self.connection.cursor.return_value = cursor
        result = execute_sql(self.session, "CALL demo()")
        self.assertEqual(result["type"], "batch")
        self.assertEqual([r["type"] for r in result["results"]], ["resultset", "command"])
        self.assertEqual(result["results"][0]["warningCount"], 2)

    def test_api_session_lifecycle_and_validation(self):
        self.store.add(PROFILE)
        manager = SessionManager(self.store, PLUGINS, result_limit=10)
        app = FastAPI()
        app.include_router(create_sql_router(manager))
        with patch.object(SqlSession, "connect", return_value=self.session) as connect, TestClient(app) as client:
            self.assertEqual(client.post("/api/sessions", json={"connectionId": "missing"}).status_code, 404)
            created = client.post("/api/sessions", json={"connectionId": "demo", "password": "pw"})
            self.assertEqual(created.status_code, 200)
            self.assertEqual(connect.call_args.args[3], "pw")
            body = created.json()
            self.assertEqual((body["sessionId"], body["connectionId"], body["name"], body["type"]), ("test", "demo", "Demo", "mysql"))
            self.assertIsNotNone(self.store.get("demo").lastConnected)
            # Use the generated manager key: the fake session has a fixed id.
            key = next(iter(manager._sessions))
            endpoint = f"/api/sessions/{key}"
            self.assertEqual(client.post(endpoint + "/execute", json={"sql": "  "}).status_code, 400)
            self.assertEqual(client.post(endpoint + "/execute", json={"sql": "SMALL"}).json()["rows"], [[0]])
            self.assertEqual(client.delete(endpoint).status_code, 204)
            self.assertEqual(client.post(endpoint + "/execute", json={"sql": "SMALL"}).status_code, 404)
            self.connection.close.assert_called_once()
            connect.side_effect = pymysql.OperationalError(1045, "Access denied for user")
            denied = client.post("/api/sessions", json={"connectionId": "demo"})
            self.assertEqual(denied.status_code, 503)
            self.assertIn("Access denied", denied.json()["detail"])

    def test_plugins_are_loaded_from_connector_files(self):
        ids = sorted(plugin.id for plugin in PLUGINS)
        self.assertEqual(ids, ["mysql", "postgresql", "questdb"])
        self.assertIs(PLUGINS.for_type("mariadb"), MYSQL)
        self.assertIsNone(PLUGINS.for_type("sqlite"))
        info = {plugin["id"]: plugin for plugin in PLUGINS.to_json()}
        self.assertEqual((info["mysql"]["watermark"], info["mysql"]["defaultPort"]), ("mysql", 3306))
        self.assertEqual((info["postgresql"]["watermark"], info["postgresql"]["defaultPort"]), ("postgresql", 5432))
        for plugin in info.values():
            capabilities = plugin["capabilities"]
            self.assertEqual(capabilities["supportedLiveModes"], ["polling"], plugin["id"])
            self.assertTrue(capabilities["timeBucketing"] and capabilities["incrementalQueries"] and capabilities["queryCancellation"])
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "broken_connector.py").write_text("raise RuntimeError('boom')\n", encoding="utf-8")
            Path(directory, "ignored.py").write_text("plugin = None\n", encoding="utf-8")
            with self.assertLogs("endless.plugins", level="WARNING"):
                self.assertEqual(len(load_plugins(Path(directory))), 0)

    def test_plugin_without_time_series_support_degrades_gracefully(self):
        class Minimal(ConnectorPlugin):
            id, name = "minimal", "Minimal"
            def connect(self, *a): return None
            def describe_columns(self, d): return []
            def error_details(self, e): return None
        plugin = Minimal()
        self.assertEqual(plugin.to_json()["capabilities"]["supportedLiveModes"], [])
        with self.assertRaises(UnsupportedCapabilityError):
            plugin.time_series_query(TimeSeriesQuerySpec(sql="SELECT 1", time_column="t"))
        with self.assertRaises(UnsupportedCapabilityError):
            plugin.cancel(None, "h", 1, "u", "")
        with self.assertRaises(UnsupportedCapabilityError):
            plugin.subscribe(None, "SELECT 1", lambda batch: None)

    def test_read_only_detection_and_column_kinds(self):
        self.assertTrue(is_read_only_statement("  -- note\n/* c */ SELECT 1"))
        self.assertTrue(is_read_only_statement("WITH x AS (SELECT 1) SELECT * FROM x;"))
        self.assertTrue(is_read_only_statement("(SELECT 1) UNION (SELECT 2)"))
        self.assertFalse(is_read_only_statement("UPDATE t SET a = 1"))
        self.assertFalse(is_read_only_statement("SELECTION"))
        self.assertFalse(is_read_only_statement("DELETE FROM t"))
        columns = MYSQL.describe_columns([("ts", 12, None, 19, 19, 0, True), ("v", 5, None, 22, 22, 31, True), ("name", 253, None, 80, 80, 0, True),
                                          ("doc", 252, None, 65535, 65535, 0, True)])
        self.assertEqual([c["kind"] for c in columns], ["timestamp", "number", "string", "string"])
        self.assertEqual(columns[0]["type"], "DATETIME")
        pg = PLUGINS.for_type("postgresql")
        Column = namedtuple("Column", "name type_code type_display")
        kinds = pg.describe_columns([Column("ts", 1184, "timestamp with time zone"), Column("n", 1700, "numeric"),
                                     Column("flag", 16, "boolean"), Column("custom", 99999, "myint8")])
        self.assertEqual([c["kind"] for c in kinds], ["timestamp", "number", "boolean", "number"])

    def test_time_series_query_generation_per_dialect(self):
        pg = PLUGINS.for_type("postgresql")
        base = dict(sql="SELECT * FROM measurements WHERE site = 'a';", time_column="ts", value_columns=("volt`age", "current"),
                    group_column="device", aggregation="avg", interval_seconds=60, since="2026-01-01T00:00:00", until="2026-01-02")
        mysql_sql = MYSQL.time_series_query(TimeSeriesQuerySpec(**base))
        self.assertIn("FROM_UNIXTIME(FLOOR(UNIX_TIMESTAMP(`q`.`ts`) / 60) * 60) AS `ts`", mysql_sql)
        self.assertIn("AVG(`q`.`volt``age`) AS `volt``age`", mysql_sql)
        self.assertIn("FROM (SELECT * FROM measurements WHERE site = 'a') AS `q` WHERE `q`.`ts` >= '2026-01-01T00:00:00' AND `q`.`ts` <= '2026-01-02'", mysql_sql)
        self.assertTrue(mysql_sql.endswith("GROUP BY 1, 2 ORDER BY 1, 2"))
        pg_sql = pg.time_series_query(TimeSeriesQuerySpec(**base))
        self.assertIn("date_bin(make_interval(secs => 60), \"q\".\"ts\", '2000-01-01') AS \"ts\"", pg_sql)
        self.assertIn('avg("q"."volt`age") AS "volt`age"', pg_sql)
        self.assertIn('"q"."device" AS "device"', pg_sql)
        incremental = MYSQL.time_series_query(TimeSeriesQuerySpec(sql="SELECT ts, v FROM t", time_column="ts", since="2026-01-01 10:00:00",
                                                                  since_exclusive=True, limit=500))
        self.assertEqual(incremental, "SELECT `q`.* FROM (SELECT ts, v FROM t) AS `q` WHERE `q`.`ts` > '2026-01-01 10:00:00' ORDER BY `q`.`ts` LIMIT 500")
        self.assertEqual(pg.time_series_query(TimeSeriesQuerySpec(sql="SELECT ts FROM t", time_column="ts")),
                         'SELECT "q".* FROM (SELECT ts FROM t) AS "q" ORDER BY "q"."ts"')
        self.assertIn("'it''s'", MYSQL.time_series_query(TimeSeriesQuerySpec(sql="SELECT 1", time_column="ts", since="it's")))
        for plugin in (MYSQL, pg):
            with self.assertRaises(ValueError):
                plugin.time_series_query(TimeSeriesQuerySpec(sql="UPDATE t SET a = 1", time_column="ts"))

    def test_cancel_and_time_series_endpoints(self):
        self.store.add(PROFILE)
        manager = SessionManager(self.store, PLUGINS, result_limit=10)
        app = FastAPI()
        app.include_router(create_sql_router(manager))
        self.connection.thread_id.return_value = 42
        self.session.password = "pw"
        killer = MagicMock()
        with patch.object(SqlSession, "connect", return_value=self.session), TestClient(app) as client:
            client.post("/api/sessions", json={"connectionId": "demo", "password": "pw"})
            key = next(iter(manager._sessions))
            with patch("pymysql.connect", return_value=killer) as connect:
                self.assertEqual(client.post(f"/api/sessions/{key}/cancel").json(), {"cancelled": True})
            self.assertEqual(connect.call_args.kwargs["password"], "pw")
            killer.cursor.return_value.__enter__.return_value.execute.assert_called_once_with("KILL QUERY %s", (42,))
            killer.close.assert_called_once()
            response = client.post(f"/api/sessions/{key}/time-series", json={"sql": "SELECT 1 AS ts;", "timeColumn": "ts", "sinceExclusive": True, "since": "2026-01-01"})
            self.assertEqual(response.status_code, 200)
            self.assertEqual(response.json()["sql"], "SELECT `q`.* FROM (SELECT 1 AS ts) AS `q` WHERE `q`.`ts` > '2026-01-01' ORDER BY `q`.`ts` LIMIT 100000")
            self.assertEqual(response.json()["result"]["rows"], [[0]])
            self.assertEqual(client.post(f"/api/sessions/{key}/time-series", json={"sql": "DELETE FROM t", "timeColumn": "ts"}).status_code, 400)
            self.assertEqual(client.post(f"/api/sessions/{key}/time-series", json={"sql": "SELECT 1", "timeColumn": "ts", "intervalSeconds": 60}).status_code, 400)
            self.assertEqual(client.post("/api/sessions/missing/cancel").status_code, 404)

    def test_xml_import_skips_passwords_and_unknown_drivers(self):
        profiles, skipped = parse_connections_xml(SAMPLE_XML)
        self.assertEqual(skipped, 1)
        self.assertEqual(len(profiles), 1)
        profile = profiles[0]
        self.assertEqual((profile.id, profile.name, profile.type), ("14d59616-ac22-414e-959f-d1aeedc390a3", "test", "mysql"))
        self.assertEqual((profile.host, profile.port, profile.username, profile.database), ("127.0.0.1", 33082, "root", "sakila"))
        self.assertTrue(profile.default)
        self.assertEqual(profile.lastConnected, 1793202095)
        self.assertEqual(profile.options, {"serverVersion": "8.2.0", "useSSL": "1"})
        self.assertNotIn("secret", str(profile.to_json()))

    def test_store_round_trip_and_duplicate_import(self):
        self.store.add(PROFILE)
        self.assertEqual(len(self.store.add_many(parse_connections_xml(SAMPLE_XML)[0])), 1)
        self.assertEqual(len(self.store.add_many(parse_connections_xml(SAMPLE_XML)[0])), 0)
        reloaded = ConnectionStore(self.store.path)
        self.assertEqual([c.id for c in reloaded.list()], ["demo", "14d59616-ac22-414e-959f-d1aeedc390a3"])
        self.assertNotIn("password", self.store.path.read_text(encoding="utf-8"))
        self.assertTrue(self.store.remove("demo"))
        self.assertFalse(self.store.remove("demo"))

    def test_connections_api(self):
        app = FastAPI()
        app.include_router(create_connections_router(self.store, PLUGINS))
        with TestClient(app) as client:
            plugins = client.get("/api/plugins").json()
            self.assertEqual([p["id"] for p in plugins], ["mysql", "postgresql", "questdb"])
            self.assertEqual(client.get("/api/connections").json(), [])
            payload = {"name": " Local PG ", "type": "postgresql", "host": "db.local", "port": 5432, "username": "app", "database": "shop"}
            created = client.post("/api/connections", json=payload)
            self.assertEqual(created.status_code, 201)
            self.assertEqual((created.json()["name"], created.json()["type"]), ("Local PG", "postgresql"))
            self.assertEqual(client.post("/api/connections", json={**payload, "type": "oracle"}).status_code, 400)
            self.assertEqual(client.post("/api/connections", json={**payload, "port": 70000}).status_code, 422)
            imported = client.post("/api/connections/import", json={"xml": SAMPLE_XML}).json()
            self.assertEqual((imported["imported"], imported["duplicates"], imported["skipped"]), (1, 0, 1))
            self.assertEqual(client.post("/api/connections/import", json={"xml": "<broken"}).status_code, 400)
            self.assertEqual(len(client.get("/api/connections").json()), 2)
            self.assertEqual(client.delete("/api/connections/" + created.json()["id"]).status_code, 204)
            self.assertEqual(client.delete("/api/connections/" + created.json()["id"]).status_code, 404)

    def test_connection_failure_does_not_expose_password(self):
        self.store.add(PROFILE)
        manager = SessionManager(self.store, PLUGINS)
        app = FastAPI()
        app.include_router(create_sql_router(manager))
        with patch.object(SqlSession, "connect", side_effect=RuntimeError("secret-password")), TestClient(app) as client:
            response = client.post("/api/sessions", json={"connectionId": "demo", "password": "secret-password"})
            self.assertEqual(response.status_code, 503)
            self.assertNotIn("secret-password", response.text)
        self.assertNotIn("secret-password", repr(DatabaseConnectionConfig(password="secret-password")))


if __name__ == "__main__":
    unittest.main()
