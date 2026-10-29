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

from time import perf_counter
from typing import Any

from app.database.session import SessionClosedError, SqlSession
from app.models.responses import cell_to_json


def execute_sql(session: SqlSession, sql: str) -> dict[str, Any]:
    started = perf_counter()
    results: list[dict[str, Any]] = []
    error: dict[str, Any] | None = None
    plugin = session.plugin
    with session.lock:
        cursor = None
        try:
            if session.closed:
                raise SessionClosedError("Database session is closed. Reload to start a new session.")
            cursor = session.connection.cursor()
            cursor.execute(sql)
            while True:
                result_started = perf_counter()
                if cursor.description:
                    columns = plugin.describe_columns(cursor.description)
                    rows = []
                    truncated = False
                    # Drain every row without retaining it: an unread result would
                    # poison the next command on this same physical connection.
                    while batch := cursor.fetchmany(256):
                        available = max(0, session.result_limit - len(rows))
                        rows.extend([[cell_to_json(v) for v in row] for row in batch[:available]])
                        truncated = truncated or len(batch) > available
                    result = {"type": "resultset", "columns": columns, "rows": rows,
                              "returnedRows": len(rows), "truncated": truncated}
                else:
                    result = {"type": "command", "affectedRows": max(0, cursor.rowcount),
                              "message": "Query OK"}
                result.update(warningCount=plugin.warning_count(cursor),
                              executionTimeMs=round((perf_counter() - result_started) * 1000, 2))
                results.append(result)
                # CALL may produce multiple result sets; consume and display all.
                if not cursor.nextset():
                    break
        except SessionClosedError as exc:
            error = {"message": str(exc), "code": None, "sqlState": None}
        except Exception as exc:
            error = plugin.error_details(exc) or {"message": "Database execution failed unexpectedly.",
                                                  "code": None, "sqlState": None}
        finally:
            if cursor is not None:
                try:
                    cursor.close()
                except Exception:
                    session.close()
                    if error is None:
                        error = {"message": "The database connection was lost while reading results.",
                                 "code": None, "sqlState": None}
    elapsed = round((perf_counter() - started) * 1000, 2)
    if error is not None:
        error["executionTimeMs"] = elapsed
        return {"type": "error", "error": error, "partialResults": results}
    if len(results) == 1:
        results[0]["executionTimeMs"] = elapsed
        return results[0]
    return {"type": "batch", "results": results, "executionTimeMs": elapsed}
