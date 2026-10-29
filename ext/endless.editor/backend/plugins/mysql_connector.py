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

"""MySQL / MariaDB connector plugin backed by PyMySQL."""

from typing import Any

from app.plugins.base import ConnectorPlugin

try:
    import pymysql
    from pymysql.constants import FIELD_TYPE
    from pymysql.cursors import SSCursor
except ImportError as error:  # pragma: no cover - depends on the environment
    pymysql = None
    DRIVER_ERROR = str(error)
else:
    DRIVER_ERROR = ""
    FIELD_NAMES = {value: name for name, value in vars(FIELD_TYPE).items() if isinstance(value, int)}


class MySqlConnector(ConnectorPlugin):
    id = "mysql"
    name = "MySQL"
    default_port = 3306
    watermark = "mysql"
    driver = "PyMySQL"
    aliases = ("mariadb",)

    def availability(self) -> str | None:
        return None if pymysql else f"Install PyMySQL to use MySQL connections ({DRIVER_ERROR})."

    def connect(self, host: str, port: int, username: str, password: str, database: str) -> Any:
        # The streaming cursor lets the executor drain large results in batches.
        return pymysql.connect(host=host, port=port, user=username, password=password, database=database or None,
                               connect_timeout=8, charset="utf8mb4", autocommit=True, cursorclass=SSCursor)

    def describe_columns(self, description: Any) -> list[dict[str, Any]]:
        return [{"name": d[0], "type": FIELD_NAMES.get(d[1], str(d[1]))} for d in description]

    def warning_count(self, cursor: Any) -> int:
        return int(getattr(cursor, "warning_count", 0) or 0)

    def error_details(self, error: BaseException) -> dict[str, Any] | None:
        if pymysql is None or not isinstance(error, pymysql.Error):
            return None
        return {"message": str(error.args[1]) if len(error.args) > 1 else "Database execution failed.",
                "code": error.args[0] if error.args else None, "sqlState": None}

    def close(self, connection: Any) -> None:
        if getattr(connection, "open", True):
            super().close(connection)


plugin = MySqlConnector()
