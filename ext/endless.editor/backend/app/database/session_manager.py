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

from threading import RLock
from uuid import uuid4

from app.database.connection_store import ConnectionStore
from app.database.session import SqlSession
from app.plugins.loader import PluginRegistry


class UnknownConnectionError(LookupError):
    pass


class UnsupportedEngineError(LookupError):
    pass


class SessionManager:
    def __init__(self, store: ConnectionStore, plugins: PluginRegistry, result_limit: int = 1000) -> None:
        self._store = store
        self._plugins = plugins
        self._result_limit = result_limit
        self._sessions: dict[str, SqlSession] = {}
        self._lock = RLock()

    def create(self, connection_id: str, password: str = "") -> SqlSession:
        profile = self._store.get(connection_id)
        if profile is None:
            raise UnknownConnectionError(f"Connection {connection_id} does not exist.")
        plugin = self._plugins.for_type(profile.type)
        if plugin is None:
            raise UnsupportedEngineError(f"No connector plugin is loaded for {profile.type} connections.")
        reason = plugin.availability()
        if reason is not None:
            raise UnsupportedEngineError(reason)
        session_id = str(uuid4())
        session = SqlSession.connect(session_id, profile, plugin, password, self._result_limit)
        with self._lock:
            self._sessions[session_id] = session
        self._store.touch(profile.id)
        return session

    def describe_error(self, connection_id: str, error: BaseException) -> str:
        """Human readable driver message for a failed connection attempt."""
        profile = self._store.get(connection_id)
        plugin = self._plugins.for_type(profile.type) if profile else None
        details = plugin.error_details(error) if plugin else None
        return str(details["message"]) if details else "Check that the server is reachable and the credentials are valid."

    def get(self, session_id: str) -> SqlSession | None:
        with self._lock:
            return self._sessions.get(session_id)

    def close(self, session_id: str) -> None:
        with self._lock:
            session = self._sessions.pop(session_id, None)
        if session:
            session.close()

    def close_all(self) -> None:
        with self._lock:
            sessions = list(self._sessions.values())
            self._sessions.clear()
        for session in sessions:
            session.close()
