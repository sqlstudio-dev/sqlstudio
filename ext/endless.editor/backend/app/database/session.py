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

from dataclasses import dataclass, field
from threading import RLock
from typing import Any

from app.database.connection_store import ConnectionProfile
from app.plugins.base import ConnectorPlugin


class SessionClosedError(RuntimeError):
    pass


@dataclass
class SqlSession:
    session_id: str
    connection: Any
    profile: ConnectionProfile
    plugin: ConnectorPlugin
    result_limit: int = 1000
    #: Kept in memory only so the plugin can open a side channel for KILL QUERY style cancels.
    password: str = field(default="", repr=False)
    lock: RLock = field(default_factory=RLock)
    closed: bool = False

    @classmethod
    def connect(cls, session_id: str, profile: ConnectionProfile, plugin: ConnectorPlugin, password: str,
                result_limit: int) -> "SqlSession":
        connection = plugin.connect(profile.host, profile.port, profile.username, password, profile.database)
        return cls(session_id=session_id, connection=connection, profile=profile, plugin=plugin, result_limit=result_limit,
                   password=password)

    def cancel(self) -> bool:
        """Ask the plugin to abort the running statement without taking the execution lock."""
        if self.closed:
            return False
        return self.plugin.cancel(self.connection, self.profile.host, self.profile.port, self.profile.username, self.password)

    def close(self) -> None:
        with self.lock:
            if not self.closed:
                self.closed = True
                self.plugin.close(self.connection)
