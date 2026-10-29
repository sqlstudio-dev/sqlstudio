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

"""QuestDB connector using PGWire for SQL and native timestamp bucketing."""

from datetime import datetime, timezone
from typing import Any

from app.plugins.base import AGGREGATIONS, TimeSeriesQuerySpec, is_read_only_statement, strip_statement
from app.plugins.pgwire import PgWireConnector, literal, quote


class QuestDbConnector(PgWireConnector):
    id = "questdb"
    name = "QuestDB"
    default_port = 8812
    watermark = "questdb"
    aliases = ("quest",)
    prepare_threshold = None

    def connect(self, host: str, port: int, username: str, password: str, database: str) -> Any:
        connection = super().connect(host, port, username, password, database or "qdb")
        from psycopg.abc import Buffer
        from psycopg.types.datetime import TimestampBinaryLoader, TimestampLoader

        # QuestDB timestamps are UTC, even though PGWire reports timestamp without time zone.
        class UtcTimestampLoader(TimestampLoader):
            def load(self, data: Buffer) -> datetime:
                return super().load(data).replace(tzinfo=timezone.utc)

        class UtcTimestampBinaryLoader(TimestampBinaryLoader):
            def load(self, data: Buffer) -> datetime:
                return super().load(data).replace(tzinfo=timezone.utc)

        connection.adapters.register_loader("timestamp", UtcTimestampLoader)
        connection.adapters.register_loader("timestamp", UtcTimestampBinaryLoader)
        return connection

    def visualization_capabilities(self) -> dict[str, Any]:
        return {"timeSeries": True, "supportedLiveModes": ["polling"], "serverSideAggregation": True, "timeBucketing": True,
                "incrementalQueries": True, "latestValueQueries": False, "queryCancellation": True}

    def time_series_query(self, spec: TimeSeriesQuerySpec) -> str:
        source = strip_statement(spec.sql)
        if not is_read_only_statement(source):
            raise ValueError("Only read-only SELECT queries can be visualised as time series.")
        if spec.aggregation not in AGGREGATIONS:
            raise ValueError("Unsupported time-series aggregation.")
        if spec.interval_seconds is not None and (spec.interval_seconds < 1 or not spec.value_columns):
            raise ValueError("Time bucketing needs a positive interval and at least one value column.")
        if spec.limit is not None and spec.limit < 1:
            raise ValueError("The time-series row limit must be positive.")
        time = '"q".' + quote(spec.time_column)
        where = []
        if spec.since:
            where.append(f"{time} {'>' if spec.since_exclusive else '>='} {literal(spec.since)}")
        if spec.until:
            where.append(f"{time} <= {literal(spec.until)}")
        where_sql = (" WHERE " + " AND ".join(where)) if where else ""
        if spec.interval_seconds is not None:
            select = [f"timestamp_floor('{int(spec.interval_seconds)}s', {time}) AS {quote(spec.time_column)}"]
            group = [quote(spec.time_column)]
            if spec.group_column:
                select.append('"q".' + quote(spec.group_column) + " AS " + quote(spec.group_column))
                group.append(quote(spec.group_column))
            select.extend(f'{spec.aggregation}("q".{quote(column)}) AS {quote(column)}' for column in spec.value_columns)
            sql = f'SELECT {", ".join(select)} FROM ({source}) AS "q"{where_sql} GROUP BY {", ".join(group)} ORDER BY {", ".join(group)}'
        else:
            sql = f'SELECT "q".* FROM ({source}) AS "q"{where_sql} ORDER BY {time}'
        return sql + (f" LIMIT {int(spec.limit)}" if spec.limit is not None else "")


plugin = QuestDbConnector()
