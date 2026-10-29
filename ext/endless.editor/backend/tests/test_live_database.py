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

"""Opt-in checks using only connection-local variables and temporary tables."""
import os
import tempfile
import unittest
from pathlib import Path

from app.database.config import DatabaseConnectionConfig
from app.database.connection_store import ConnectionStore
from app.database.session_manager import SessionManager
from app.plugins.loader import load_plugins
from app.services.sql_executor import execute_sql


@unittest.skipUnless(os.getenv("SQL_LIVE_TEST") == "1", "Set SQL_LIVE_TEST=1 to test the configured database")
class LiveDatabaseTests(unittest.TestCase):
    def test_session_results_and_transactions(self):
        config = DatabaseConnectionConfig.from_environment()
        with tempfile.TemporaryDirectory() as directory:
            store = ConnectionStore(Path(directory) / "connection.json")
            profile = store.add(config.seed_profile())
            manager = SessionManager(store, load_plugins(), config.result_limit)
            session = manager.create(profile.id, config.password)
            self._exercise(session, profile.database)

    def _exercise(self, session, database):
        def execute(sql):
            result = execute_sql(session, sql)
            self.assertNotEqual(result["type"], "error", result)
            return result
        try:
            execute("SET @endless_control_test = 10")
            self.assertEqual(execute("SELECT @endless_control_test")["rows"], [[10]])
            if database:
                schema = database.replace("`", "``")
                execute("USE `" + schema + "`")
                self.assertEqual(execute("SELECT DATABASE()")["rows"], [[database]])
            execute("CREATE TEMPORARY TABLE endless_control_test (id INT PRIMARY KEY, label VARCHAR(30)) ENGINE=InnoDB")
            execute("INSERT INTO endless_control_test VALUES (1, 'before')")
            execute("START TRANSACTION")
            self.assertEqual(execute("UPDATE endless_control_test SET label='after' WHERE id=1")["affectedRows"], 1)
            self.assertEqual(execute("SELECT label FROM endless_control_test WHERE id=1")["rows"], [["after"]])
            execute("ROLLBACK")
            self.assertEqual(execute("SELECT label FROM endless_control_test WHERE id=1")["rows"], [["before"]])
            self.assertEqual(execute_sql(session, "SELECT * FROM endless_control_missing_test_table")["type"], "error")
            self.assertEqual(execute("SELECT 1")["rows"], [[1]])
            digits = "(SELECT 0 n UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9)"
            limited = execute(f"SELECT a.n FROM {digits} a CROSS JOIN {digits} b CROSS JOIN {digits} c CROSS JOIN {digits} d")
            self.assertEqual(limited["returnedRows"], min(session.result_limit, 10000))
            self.assertEqual(limited["truncated"], session.result_limit < 10000)
            self.assertEqual(execute("SELECT @endless_control_test")["rows"], [[10]])
        finally:
            session.close()


if __name__ == "__main__":
    unittest.main()
