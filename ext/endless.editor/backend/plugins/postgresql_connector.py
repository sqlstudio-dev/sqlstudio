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

"""PostgreSQL connector plugin backed by psycopg 3."""

from typing import Any

from app.plugins.base import ConnectorPlugin

try:
    import psycopg
except ImportError as error:  # pragma: no cover - depends on the environment
    psycopg = None
    DRIVER_ERROR = str(error)
else:
    DRIVER_ERROR = ""


class PostgreSqlConnector(ConnectorPlugin):
    id = "postgresql"
    name = "PostgreSQL"
    default_port = 5432
    watermark = "postgresql"
    driver = "psycopg[binary]"
    aliases = ("postgres",)

    def availability(self) -> str | None:
        return None if psycopg else f"Install psycopg[binary] to use PostgreSQL connections ({DRIVER_ERROR})."

    def connect(self, host: str, port: int, username: str, password: str, database: str) -> Any:
        options: dict[str, Any] = {"host": host, "port": port, "user": username, "connect_timeout": 8}
        if password:
            options["password"] = password
        if database:
            options["dbname"] = database
        return psycopg.connect(**options, autocommit=True)

    def describe_columns(self, description: Any) -> list[dict[str, Any]]:
        return [{"name": column.name, "type": getattr(column, "type_display", None) or str(column.type_code)}
                for column in description]

    def error_details(self, error: BaseException) -> dict[str, Any] | None:
        if psycopg is None or not isinstance(error, psycopg.Error):
            return None
        diagnostic = getattr(error, "diag", None)
        message = getattr(diagnostic, "message_primary", None) or str(error) or "Database execution failed."
        sqlstate = getattr(error, "sqlstate", None)
        return {"message": message.strip(), "code": sqlstate, "sqlState": sqlstate}


plugin = PostgreSqlConnector()
