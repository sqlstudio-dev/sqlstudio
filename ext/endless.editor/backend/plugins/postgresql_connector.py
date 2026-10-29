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

from app.plugins.base import TimeSeriesQuerySpec, is_read_only_statement, strip_statement
from app.plugins.pgwire import PgWireConnector, literal, quote


class PostgreSqlConnector(PgWireConnector):
    id = "postgresql"
    name = "PostgreSQL"
    default_port = 5432
    watermark = "postgresql"
    aliases = ("postgres",)

    def visualization_capabilities(self) -> dict[str, Any]:
        # LISTEN/NOTIFY is not a query stream, so PostgreSQL also offers polling only.
        return {"timeSeries": True, "supportedLiveModes": ["polling"], "serverSideAggregation": True, "timeBucketing": True,
                "incrementalQueries": True, "latestValueQueries": False, "queryCancellation": True}

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
