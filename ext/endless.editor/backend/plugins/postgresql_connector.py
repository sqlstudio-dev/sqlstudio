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

from app.plugins.base import ConnectorPlugin, TimeSeriesQuerySpec, is_read_only_statement, strip_statement

try:
    import psycopg
except ImportError as error:  # pragma: no cover - depends on the environment
    psycopg = None
    DRIVER_ERROR = str(error)
else:
    DRIVER_ERROR = ""

# Built-in type OIDs; anything else is classified by its textual type name.
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

    def visualization_capabilities(self) -> dict[str, Any]:
        # LISTEN/NOTIFY is not a query stream, so PostgreSQL also offers polling only.
        return {"timeSeries": True, "supportedLiveModes": ["polling"], "serverSideAggregation": True, "timeBucketing": True,
                "incrementalQueries": True, "latestValueQueries": False, "queryCancellation": True}

    def cancel(self, connection: Any, host: str, port: int, username: str, password: str) -> bool:
        # libpq cancel requests travel over a separate socket and need no credentials.
        cancel = getattr(connection, "cancel_safe", None) or connection.cancel
        cancel()
        return True

    def time_series_query(self, spec: TimeSeriesQuerySpec) -> str:
        source = strip_statement(spec.sql)
        if not is_read_only_statement(source):
            raise ValueError("Only read-only SELECT queries can be visualised as time series.")
        time = '"q".' + quote(spec.time_column)
        where = []
        if spec.since:
            where.append(f"{time} {'>' if spec.since_exclusive else '>='} {literal(spec.since)}")
        if spec.until:
            where.append(f"{time} <= {literal(spec.until)}")
        where_sql = (" WHERE " + " AND ".join(where)) if where else ""
        if spec.interval_seconds:
            seconds = int(spec.interval_seconds)
            # date_bin keeps the column's own type (timestamp vs timestamptz); the untyped origin literal adapts to it.
            select = [f"date_bin(make_interval(secs => {seconds}), {time}, '2000-01-01') AS {quote(spec.time_column)}"]
            group = ["1"]
            if spec.group_column:
                select.append('"q".' + quote(spec.group_column) + " AS " + quote(spec.group_column))
                group.append("2")
            fn = spec.aggregation.lower()
            select.extend(f"{fn}(\"q\".{quote(column)}) AS {quote(column)}" for column in spec.value_columns)
            sql = f"SELECT {', '.join(select)} FROM ({source}) AS \"q\"{where_sql} GROUP BY {', '.join(group)} ORDER BY {', '.join(group)}"
        else:
            sql = f"SELECT \"q\".* FROM ({source}) AS \"q\"{where_sql} ORDER BY {time}"
        return sql + (f" LIMIT {int(spec.limit)}" if spec.limit else "")


plugin = PostgreSqlConnector()
