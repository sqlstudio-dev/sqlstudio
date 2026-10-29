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

from abc import ABC, abstractmethod
from typing import Any


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
        """Map ``cursor.description`` to ``[{"name": ..., "type": ...}]``."""

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

    def to_json(self) -> dict[str, Any]:
        reason = self.availability()
        return {"id": self.id, "name": self.name, "defaultPort": self.default_port, "watermark": self.watermark,
                "driver": self.driver, "aliases": list(self.aliases), "available": reason is None, "unavailableReason": reason}
