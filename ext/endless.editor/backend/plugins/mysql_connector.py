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

from app.plugins.base import ConnectorPlugin, TimeSeriesQuerySpec, is_read_only_statement, strip_statement

try:
    import pymysql
    from pymysql.constants import FIELD_TYPE
    from pymysql.cursors import SSCursor
except ImportError as error:  # pragma: no cover - depends on the environment
    pymysql = None
    DRIVER_ERROR = str(error)
    FIELD_NAMES: dict[int, str] = {}
else:
    DRIVER_ERROR = ""
    FIELD_NAMES = {value: name for name, value in vars(FIELD_TYPE).items() if isinstance(value, int)}

KINDS = {
    "TIMESTAMP": "timestamp", "DATETIME": "timestamp", "DATE": "date", "NEWDATE": "date", "TIME": "time", "YEAR": "number",
    "TINY": "number", "SHORT": "number", "INT24": "number", "LONG": "number", "LONGLONG": "number", "FLOAT": "number",
    "DOUBLE": "number", "DECIMAL": "number", "NEWDECIMAL": "number", "BIT": "binary", "VARCHAR": "string",
    "VAR_STRING": "string", "STRING": "string", "ENUM": "string", "SET": "string", "JSON": "string", "BLOB": "string",
    "TINY_BLOB": "string", "MEDIUM_BLOB": "string", "LONG_BLOB": "string", "GEOMETRY": "other", "NULL": "other",
}


def quote(identifier: str) -> str:
    return "`" + identifier.replace("`", "``") + "`"


def literal(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


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
        options = {"host": host, "port": port, "user": username, "password": password, "database": database or None,
                   "connect_timeout": 8, "charset": "utf8mb4", "autocommit": True, "cursorclass": SSCursor}
        return pymysql.connect(**options)

    def describe_columns(self, description: Any) -> list[dict[str, Any]]:
        # The DB-API description cannot tell TEXT from BLOB; the frontend samples cell values for that.
        return [{"name": d[0], "type": FIELD_NAMES.get(d[1], str(d[1])), "kind": KINDS.get(FIELD_NAMES.get(d[1], ""), "other")}
                for d in description]

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

    def visualization_capabilities(self) -> dict[str, Any]:
        # MySQL has no push protocol for arbitrary queries, so only polling is offered.
        return {"timeSeries": True, "supportedLiveModes": ["polling"], "serverSideAggregation": True, "timeBucketing": True,
                "incrementalQueries": True, "latestValueQueries": False, "queryCancellation": True}

    def cancel(self, connection: Any, host: str, port: int, username: str, password: str) -> bool:
        # KILL QUERY must be sent from a second connection; it aborts the statement, not the session.
        options = {"host": host, "port": port, "user": username, "password": password, "connect_timeout": 5, "autocommit": True}
        killer = pymysql.connect(**options)
        try:
            with killer.cursor() as cursor:
                cursor.execute("KILL QUERY %s", (int(connection.thread_id()),))
            return True
        finally:
            killer.close()

    def time_series_query(self, spec: TimeSeriesQuerySpec) -> str:
        source = strip_statement(spec.sql)
        if not is_read_only_statement(source):
            raise ValueError("Only read-only SELECT queries can be visualised as time series.")
        time = "`q`." + quote(spec.time_column)
        where = []
        if spec.since:
            where.append(f"{time} {'>' if spec.since_exclusive else '>='} {literal(spec.since)}")
        if spec.until:
            where.append(f"{time} <= {literal(spec.until)}")
        where_sql = (" WHERE " + " AND ".join(where)) if where else ""
        if spec.interval_seconds:
            seconds = int(spec.interval_seconds)
            # FROM_UNIXTIME(UNIX_TIMESTAMP()) round-trips through the session time zone, so DATETIME values keep their wall-clock.
            select = [f"FROM_UNIXTIME(FLOOR(UNIX_TIMESTAMP({time}) / {seconds}) * {seconds}) AS {quote(spec.time_column)}"]
            group = ["1"]
            if spec.group_column:
                select.append("`q`." + quote(spec.group_column) + " AS " + quote(spec.group_column))
                group.append("2")
            fn = spec.aggregation.upper()
            select.extend(f"{fn}(`q`.{quote(column)}) AS {quote(column)}" for column in spec.value_columns)
            sql = f"SELECT {', '.join(select)} FROM ({source}) AS `q`{where_sql} GROUP BY {', '.join(group)} ORDER BY {', '.join(group)}"
        else:
            sql = f"SELECT `q`.* FROM ({source}) AS `q`{where_sql} ORDER BY {time}"
        return sql + (f" LIMIT {int(spec.limit)}" if spec.limit else "")


plugin = MySqlConnector()
