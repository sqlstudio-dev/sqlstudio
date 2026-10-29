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
from decimal import Decimal
from pathlib import Path
from unittest.mock import Mock, patch

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
        self.assertEqual(ids, ["mysql", "postgresql"])
        self.assertIs(PLUGINS.for_type("mariadb"), MYSQL)
        self.assertIsNone(PLUGINS.for_type("sqlite"))
        info = {plugin["id"]: plugin for plugin in PLUGINS.to_json()}
        self.assertEqual((info["mysql"]["watermark"], info["mysql"]["defaultPort"]), ("mysql", 3306))
        self.assertEqual((info["postgresql"]["watermark"], info["postgresql"]["defaultPort"]), ("postgresql", 5432))
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "broken_connector.py").write_text("raise RuntimeError('boom')\n", encoding="utf-8")
            Path(directory, "ignored.py").write_text("plugin = None\n", encoding="utf-8")
            with self.assertLogs("endless.plugins", level="WARNING"):
                self.assertEqual(len(load_plugins(Path(directory))), 0)

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
            self.assertEqual([p["id"] for p in plugins], ["mysql", "postgresql"])
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
