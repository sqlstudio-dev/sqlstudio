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

"""Shared psycopg transport and metadata for PostgreSQL-wire connectors."""

from typing import Any

from app.plugins.base import ConnectorPlugin

try:
    import psycopg
except ImportError as error:  # pragma: no cover - depends on the environment
    psycopg = None
    DRIVER_ERROR = str(error)
else:
    DRIVER_ERROR = ""

OID_KINDS = {
    1114: "timestamp", 1184: "timestamp", 1082: "date", 1083: "time", 1266: "time",
    20: "number", 21: "number", 23: "number", 26: "number", 700: "number", 701: "number", 1700: "number",
    16: "boolean", 17: "binary", 25: "string", 1043: "string", 1042: "string", 18: "string", 19: "string",
    114: "string", 3802: "string", 2950: "string", 142: "string",
}


def quote(identifier: str) -> str:
    return '"' + identifier.replace('"', '""') + '"'


def literal(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


class PgWireConnector(ConnectorPlugin):
    driver = "psycopg[binary]"
    prepare_threshold: int | None = 5

    def availability(self) -> str | None:
        return None if psycopg else f"Install psycopg[binary] to use {self.name} connections ({DRIVER_ERROR})."

    def connect(self, host: str, port: int, username: str, password: str, database: str) -> Any:
        if psycopg is None:
            raise RuntimeError(self.availability())
        options: dict[str, Any] = {"host": host, "port": port, "user": username, "connect_timeout": 8}
        if password:
            options["password"] = password
        if database:
            options["dbname"] = database
        return psycopg.connect(**options, autocommit=True, prepare_threshold=self.prepare_threshold)

    def describe_columns(self, description: Any) -> list[dict[str, Any]]:
        columns = []
        for column in description:
            native = getattr(column, "type_display", None) or str(column.type_code)
            kind = OID_KINDS.get(column.type_code)
            if kind is None:
                lowered = native.lower()
                kind = ("timestamp" if "timestamp" in lowered else "date" if lowered == "date" else "time" if lowered.startswith("time")
                        else "number" if any(t in lowered for t in ("int", "numeric", "double", "real", "serial")) else "other")
            columns.append({"name": column.name, "type": native, "kind": kind})
        return columns

    def error_details(self, error: BaseException) -> dict[str, Any] | None:
        if psycopg is None or not isinstance(error, psycopg.Error):
            return None
        diagnostic = getattr(error, "diag", None)
        message = getattr(diagnostic, "message_primary", None) or str(error) or "Database execution failed."
        sqlstate = getattr(error, "sqlstate", None)
        return {"message": message.strip(), "code": sqlstate, "sqlState": sqlstate}

    def cancel(self, connection: Any, host: str, port: int, username: str, password: str) -> bool:
        # PGWire cancel requests use a separate socket and do not take the session lock.
        cancel = getattr(connection, "cancel_safe", None) or connection.cancel
        cancel()
        return True
