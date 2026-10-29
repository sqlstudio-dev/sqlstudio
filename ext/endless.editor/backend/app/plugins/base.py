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

"""Contract implemented by every ``backend/plugins/*_connector.py`` module.

A plugin module exposes a module-level ``plugin`` instance of a
:class:`ConnectorPlugin` subclass. The loader imports every matching file at
startup; the frontend lists loaded plugins as selectable database engines.
"""

import re
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any, Callable

#: Normalised column kinds used by the frontend for time-series detection.
COLUMN_KINDS = ("timestamp", "date", "time", "number", "boolean", "string", "binary", "other")
LIVE_MODES = ("polling", "streaming")
AGGREGATIONS = ("avg", "sum", "min", "max", "count")
_LEADING_COMMENTS = re.compile(r"^(?:\s|--[^\n]*\n|/\*.*?\*/)+", re.S)
_READ_ONLY_KEYWORDS = ("select", "with", "show", "explain", "describe", "desc", "table", "values")


class UnsupportedCapabilityError(RuntimeError):
    pass


@dataclass(frozen=True)
class TimeSeriesQuerySpec:
    """Server-side window/aggregation request over a user-authored read-only query."""

    sql: str
    time_column: str
    value_columns: tuple[str, ...] = ()
    group_column: str | None = None
    aggregation: str = "avg"
    #: ``None`` keeps individual rows (windowed / incremental fetch); otherwise bucket size.
    interval_seconds: int | None = None
    since: str | None = None
    until: str | None = None
    since_exclusive: bool = False
    limit: int | None = None
    options: dict[str, Any] = field(default_factory=dict)


def strip_statement(sql: str) -> str:
    """Trim whitespace and trailing semicolons so the query can be used as a derived table."""
    return sql.strip().rstrip(";").strip()


def is_read_only_statement(sql: str) -> bool:
    body = _LEADING_COMMENTS.sub("", sql.strip() + "\n").lstrip("(").strip().lower()
    return any(body.startswith(keyword) and not body[len(keyword):len(keyword) + 1].isalnum() for keyword in _READ_ONLY_KEYWORDS)


class ConnectorPlugin(ABC):
    #: Stable identifier stored in connection.json as the connection ``type``.
    id: str = ""
    #: Human readable engine name used for grouping tiles.
    name: str = ""
    default_port: int = 0
    #: Watermark logo drawn on connection tiles (mysql, postgresql, mssql, database).
    watermark: str = "database"
    #: Python package providing the driver, shown when the driver is missing.
    driver: str = ""
    #: Additional connection ``type`` values served by this plugin.
    aliases: tuple[str, ...] = ()

    def availability(self) -> str | None:
        """Return ``None`` when the driver can be used, otherwise a reason."""
        return None

    @abstractmethod
    def connect(self, host: str, port: int, username: str, password: str, database: str) -> Any:
        """Open an autocommit DB-API connection, raising a driver error on failure."""

    @abstractmethod
    def describe_columns(self, description: Any) -> list[dict[str, Any]]:
        """Map ``cursor.description`` to ``[{"name": ..., "type": ..., "kind": ...}]``."""

    @abstractmethod
    def error_details(self, error: BaseException) -> dict[str, Any] | None:
        """Normalize a driver exception, or return ``None`` for foreign errors."""

    def warning_count(self, cursor: Any) -> int:
        return 0

    def close(self, connection: Any) -> None:
        try:
            connection.close()
        except Exception:
            pass

    # -- optional visualisation / live-query capabilities -------------------------------

    def visualization_capabilities(self) -> dict[str, Any]:
        """Describe optional time-series features; plugins override what they implement."""
        return {"timeSeries": True, "supportedLiveModes": [], "serverSideAggregation": False, "timeBucketing": False,
                "incrementalQueries": False, "latestValueQueries": False, "queryCancellation": False}

    def cancel(self, connection: Any, host: str, port: int, username: str, password: str) -> bool:
        """Cancel the statement running on ``connection``; return whether a cancel was sent."""
        raise UnsupportedCapabilityError(f"{self.name} connections do not support query cancellation.")

    def time_series_query(self, spec: TimeSeriesQuerySpec) -> str:
        """Wrap a read-only user query into a windowed or time-bucketed statement."""
        raise UnsupportedCapabilityError(f"{self.name} connections do not support server-side time-series queries.")

    def subscribe(self, connection: Any, sql: str, on_batch: Callable[[list[list[Any]]], None]) -> Callable[[], None]:
        """Start a native push subscription and return an unsubscribe callable.

        No shipped plugin implements streaming; the frontend only offers the mode when a
        plugin lists ``"streaming"`` in ``supportedLiveModes``.
        """
        raise UnsupportedCapabilityError(f"{self.name} connections do not support streaming queries.")

    def to_json(self) -> dict[str, Any]:
        reason = self.availability()
        return {"id": self.id, "name": self.name, "defaultPort": self.default_port, "watermark": self.watermark,
                "driver": self.driver, "aliases": list(self.aliases), "available": reason is None, "unavailableReason": reason,
                "capabilities": self.visualization_capabilities()}
