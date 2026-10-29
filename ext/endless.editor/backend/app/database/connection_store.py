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

"""Persisted connection profiles (``connection.json``). Passwords are never stored."""

import json
import os
import time
from dataclasses import asdict, dataclass, field, replace
from pathlib import Path
from threading import RLock
from typing import Any
from uuid import uuid4

STORE_VERSION = 1


@dataclass(frozen=True)
class ConnectionProfile:
    id: str
    name: str
    type: str
    host: str
    port: int
    username: str = ""
    database: str = ""
    default: bool = False
    lastConnected: int | None = None
    options: dict[str, Any] = field(default_factory=dict)

    @classmethod
    def from_json(cls, data: dict[str, Any]) -> "ConnectionProfile":
        return cls(
            id=str(data.get("id") or uuid4()), name=str(data.get("name") or "Connection"),
            type=str(data.get("type") or "mysql").lower(), host=str(data.get("host") or "127.0.0.1"),
            port=int(data.get("port") or 0), username=str(data.get("username") or ""),
            database=str(data.get("database") or ""), default=bool(data.get("default", False)),
            lastConnected=int(data["lastConnected"]) if data.get("lastConnected") else None,
            options={k: v for k, v in dict(data.get("options") or {}).items() if "password" not in k.lower()},
        )

    def to_json(self) -> dict[str, Any]:
        return asdict(self)


def default_connections_file() -> Path:
    override = os.getenv("ENDLESS_CONNECTIONS_FILE")
    return Path(override) if override else Path(__file__).resolve().parents[2] / "connection.json"


class ConnectionStore:
    def __init__(self, path: Path) -> None:
        self.path = path
        self._lock = RLock()
        self._connections: list[ConnectionProfile] = []
        self._loaded = False

    def exists(self) -> bool:
        return self.path.is_file()

    def load(self) -> list[ConnectionProfile]:
        with self._lock:
            self._connections = []
            if self.path.is_file():
                data = json.loads(self.path.read_text(encoding="utf-8") or "{}")
                entries = data.get("connections", data if isinstance(data, list) else [])
                self._connections = [ConnectionProfile.from_json(entry) for entry in entries if isinstance(entry, dict)]
            self._loaded = True
            return list(self._connections)

    def save(self) -> None:
        with self._lock:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            payload = {"version": STORE_VERSION, "connections": [c.to_json() for c in self._connections]}
            temporary = self.path.with_suffix(self.path.suffix + ".tmp")
            temporary.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
            os.replace(temporary, self.path)

    def list(self) -> list[ConnectionProfile]:
        with self._lock:
            if not self._loaded:
                self.load()
            return list(self._connections)

    def get(self, connection_id: str) -> ConnectionProfile | None:
        return next((c for c in self.list() if c.id == connection_id), None)

    def add(self, profile: ConnectionProfile) -> ConnectionProfile:
        with self._lock:
            self.list()
            if any(c.id == profile.id for c in self._connections):
                raise ValueError(f"Connection {profile.id} already exists")
            self._connections.append(profile)
            self.save()
            return profile

    def add_many(self, profiles: list[ConnectionProfile]) -> list[ConnectionProfile]:
        """Add profiles whose id is not yet stored; returns the newly added ones."""
        with self._lock:
            known = {c.id for c in self.list()}
            added: list[ConnectionProfile] = []
            for profile in profiles:
                if profile.id not in known:
                    known.add(profile.id)
                    added.append(profile)
            if added:
                self._connections.extend(added)
                self.save()
            return added

    def remove(self, connection_id: str) -> bool:
        with self._lock:
            before = len(self.list())
            self._connections = [c for c in self._connections if c.id != connection_id]
            if len(self._connections) != before:
                self.save()
                return True
            return False

    def touch(self, connection_id: str) -> None:
        with self._lock:
            now = int(time.time())
            self._connections = [replace(c, lastConnected=now) if c.id == connection_id else c for c in self.list()]
            self.save()
