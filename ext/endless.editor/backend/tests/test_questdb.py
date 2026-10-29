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

import os
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path
from threading import Event
from time import sleep
from unittest.mock import Mock, patch

import psycopg
from fastapi import FastAPI
from fastapi.testclient import TestClient

from app.api.connections import create_connections_router
from app.api.sql import create_sql_router
from app.database.connection_store import ConnectionProfile, ConnectionStore
from app.database.session_manager import SessionManager
from app.plugins.base import TimeSeriesQuerySpec
from app.plugins.loader import load_plugins
from app.services.sql_executor import execute_sql

PLUGINS = load_plugins()
QUESTDB = PLUGINS.for_type("questdb")
SOURCE = """SELECT timestamp_sequence('2026-01-01T00:00:00.000000Z', 1000000) AS ts,
    concat('device-', cast(x % 2 as string)) AS device, x * 1.0 AS value FROM long_sequence(120)"""


class QuestDbTests(unittest.TestCase):
    def test_discovery_and_capabilities(self):
        self.assertIs(PLUGINS.for_type("quest"), QUESTDB)
        info = QUESTDB.to_json()
        self.assertEqual((info["id"], info["name"], info["watermark"], info["defaultPort"]),
                         ("questdb", "QuestDB", "questdb", 8812))
        self.assertEqual(info["driver"], "psycopg[binary]")
        self.assertEqual(info["capabilities"]["supportedLiveModes"], ["polling"])
        for capability in ("timeSeries", "serverSideAggregation", "timeBucketing", "incrementalQueries", "queryCancellation"):
            self.assertTrue(info["capabilities"][capability], capability)

    def test_driver_options_and_postgresql_defaults_are_independent(self):
        connection = Mock()
        with patch("app.plugins.pgwire.psycopg.connect", return_value=connection) as connect:
            self.assertIs(QUESTDB.connect("db.local", 8812, "reader", "pw", ""), connection)
            self.assertEqual(connect.call_args.kwargs, {
                "host": "db.local", "port": 8812, "user": "reader", "password": "pw",
                "dbname": "qdb", "connect_timeout": 8, "autocommit": True, "prepare_threshold": None,
            })
            self.assertEqual(connection.adapters.register_loader.call_count, 2)
            QUESTDB.connect("db.local", 8812, "reader", "", "custom")
            self.assertEqual(connect.call_args.kwargs["dbname"], "custom")
            self.assertNotIn("password", connect.call_args.kwargs)
            PLUGINS.for_type("postgresql").connect("pg.local", 5432, "reader", "", "")
            self.assertEqual(connect.call_args.kwargs["prepare_threshold"], 5)
            self.assertNotIn("dbname", connect.call_args.kwargs)

    def test_missing_driver_is_reported_for_both_pgwire_engines(self):
        with patch("app.plugins.pgwire.psycopg", None):
            for name in ("questdb", "postgresql"):
                plugin = PLUGINS.for_type(name)
                self.assertIn(plugin.name, plugin.availability())
                self.assertFalse(plugin.to_json()["available"])
                with self.assertRaisesRegex(RuntimeError, "Install psycopg"):
                    plugin.connect("localhost", plugin.default_port, "reader", "", "")

    def test_errors_and_cancellation(self):
        error = psycopg.errors.SyntaxError("Invalid QuestDB SQL")
        self.assertEqual(QUESTDB.error_details(error), {
            "message": "Invalid QuestDB SQL", "code": "42601", "sqlState": "42601",
        })
        self.assertIsNone(QUESTDB.error_details(ValueError("foreign error")))
        connection = Mock()
        self.assertTrue(QUESTDB.cancel(connection, "h", 8812, "u", ""))
        connection.cancel_safe.assert_called_once_with()
        connection = Mock(cancel_safe=None)
        self.assertTrue(QUESTDB.cancel(connection, "h", 8812, "u", ""))
        connection.cancel.assert_called_once_with()

    def test_bucket_query_uses_questdb_dialect_and_preserves_groups(self):
        sql = QUESTDB.time_series_query(TimeSeriesQuerySpec(
            sql="SELECT * FROM measurements;", time_column="ts", value_columns=('volt"age', "current"),
            group_column="device", interval_seconds=90, since="2026-01-01", until="2026-01-02", limit=500,
        ))
        self.assertEqual(sql, """SELECT timestamp_floor('90s', "q"."ts") AS "ts", "q"."device" AS "device", """
                         """avg("q"."volt""age") AS "volt""age", avg("q"."current") AS "current" """
                         """FROM (SELECT * FROM measurements) AS "q" WHERE "q"."ts" >= '2026-01-01' """
                         """AND "q"."ts" <= '2026-01-02' GROUP BY "ts", "device" ORDER BY "ts", "device" LIMIT 500""")
        self.assertNotIn("date_bin", sql)

    def test_incremental_query_quotes_identifiers_and_literals(self):
        sql = QUESTDB.time_series_query(TimeSeriesQuerySpec(
            sql="SELECT ts, value FROM measurements;", time_column='time"stamp',
            since="it's", since_exclusive=True, limit=20,
        ))
        self.assertEqual(sql, """SELECT "q".* FROM (SELECT ts, value FROM measurements) AS "q" """
                         """WHERE "q"."time""stamp" > 'it''s' ORDER BY "q"."time""stamp" LIMIT 20""")
        self.assertEqual(QUESTDB.time_series_query(TimeSeriesQuerySpec(sql="SELECT ts FROM t", time_column="ts")),
                         'SELECT "q".* FROM (SELECT ts FROM t) AS "q" ORDER BY "q"."ts"')

    def test_invalid_queries_and_options_raise_explicit_errors(self):
        for options in ({"sql": "DELETE FROM measurements"}, {"aggregation": "unknown"},
                        {"interval_seconds": 0}, {"interval_seconds": 60}, {"limit": 0}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                QUESTDB.time_series_query(TimeSeriesQuerySpec(**{"sql": "SELECT ts FROM t", "time_column": "ts", **options}))


@unittest.skipUnless(os.getenv("QUESTDB_LIVE_TEST") == "1", "Set QUESTDB_LIVE_TEST=1 to test QuestDB over PGWire")
class LiveQuestDbTests(unittest.TestCase):
    """Read-only integration checks: no tables or saved user connections are changed."""

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        store = ConnectionStore(Path(directory.name) / "connection.json")
        store.add(ConnectionProfile(id="questdb-live", name="QuestDB live", type="questdb",
                                    host=os.getenv("QUESTDB_HOST", "127.0.0.1"), port=int(os.getenv("QUESTDB_PORT", "8812")),
                                    username=os.getenv("QUESTDB_USERNAME", "admin"), database="qdb"))
        self.manager = SessionManager(store, PLUGINS, result_limit=1000)
        self.addCleanup(self.manager.close_all)
        app = FastAPI()
        app.include_router(create_connections_router(store, PLUGINS))
        app.include_router(create_sql_router(self.manager))
        self.client = TestClient(app)
        self.addCleanup(self.client.close)
        response = self.client.post("/api/sessions", json={"connectionId": "questdb-live", "password": os.getenv("QUESTDB_PASSWORD", "")})
        self.assertEqual(response.status_code, 200, response.text)
        self.session_id = response.json()["sessionId"]
        self.endpoint = f"/api/sessions/{self.session_id}"
        self.session = self.manager.get(self.session_id)

    def execute(self, sql):
        response = self.client.post(self.endpoint + "/execute", json={"sql": sql})
        self.assertEqual(response.status_code, 200, response.text)
        result = response.json()
        self.assertEqual(result["type"], "resultset", result)
        return result

    def test_metadata_utc_incremental_aggregation_and_error_recovery(self):
        plugins = self.client.get("/api/plugins").json()
        self.assertTrue(next(p for p in plugins if p["id"] == "questdb")["available"])
        result = self.execute(SOURCE)
        self.assertEqual([c["kind"] for c in result["columns"]], ["timestamp", "string", "number"])
        self.assertEqual(result["returnedRows"], 120)
        self.assertEqual(result["rows"][0], ["2026-01-01T00:00:00+00:00", "device-1", 1.0])
        request = {"sql": SOURCE, "timeColumn": "ts", "since": "2026-01-01T00:00:59+00:00",
                   "sinceExclusive": True, "until": "2026-01-01T00:01:01+00:00"}
        response = self.client.post(self.endpoint + "/time-series", json=request)
        self.assertEqual(response.status_code, 200, response.text)
        self.assertEqual([row[2] for row in response.json()["result"]["rows"]], [61.0, 62.0])
        expected = {"avg": [31.0, 30.0, 91.0, 90.0], "sum": [930.0, 900.0, 2730.0, 2700.0],
                    "min": [2.0, 1.0, 62.0, 61.0], "max": [60.0, 59.0, 120.0, 119.0], "count": [30, 30, 30, 30]}
        for aggregation, values in expected.items():
            response = self.client.post(self.endpoint + "/time-series", json={
                "sql": SOURCE, "timeColumn": "ts", "valueColumns": ["value"], "groupColumn": "device",
                "aggregation": aggregation, "intervalSeconds": 60,
            })
            self.assertEqual(response.status_code, 200, response.text)
            body = response.json()
            self.assertIn("timestamp_floor('60s'", body["sql"])
            self.assertEqual(body["result"]["type"], "resultset", body)
            self.assertEqual([row[2] for row in body["result"]["rows"]], values)
        error = self.client.post(self.endpoint + "/execute", json={"sql": "SELECT * FROM endless_missing_questdb_test_table"}).json()
        self.assertEqual(error["type"], "error")
        self.assertIn("table does not exist", error["error"]["message"])
        self.assertEqual(self.execute("SELECT 1")["rows"], [[1]])
        self.assertEqual(self.client.delete(self.endpoint).status_code, 204)
        self.assertEqual(self.client.post(self.endpoint + "/execute", json={"sql": "SELECT 1"}).status_code, 404)

    def test_truncation_and_binary_timestamps(self):
        result = execute_sql(self.session, SOURCE, row_limit=10)
        self.assertEqual(result["returnedRows"], 10)
        self.assertTrue(result["truncated"])
        self.assertEqual(self.execute("SELECT 1")["rows"], [[1]])
        with self.session.connection.cursor(binary=True) as cursor:
            cursor.execute("SELECT cast('2026-01-01T00:00:00.000000Z' as timestamp)")
            self.assertEqual(cursor.fetchone()[0], datetime(2026, 1, 1, tzinfo=timezone.utc))

    def test_cancel_running_query_and_reuse_connection(self):
        started = Event()

        def run():
            started.set()
            return execute_sql(self.session, "SELECT sum(sin(x)) FROM long_sequence(100000000)")

        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(run)
            self.assertTrue(started.wait(2))
            sleep(0.1)
            response = self.client.post(self.endpoint + "/cancel")
            self.assertEqual(response.status_code, 200, response.text)
            self.assertTrue(response.json()["cancelled"])
            result = future.result(timeout=15)
        self.assertEqual(result["type"], "error", result)
        self.assertIn("cancel", result["error"]["message"].lower())
        self.assertEqual(self.execute("SELECT 1")["rows"], [[1]])


if __name__ == "__main__":
    unittest.main()
