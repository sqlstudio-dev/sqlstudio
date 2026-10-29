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

import os
from dataclasses import dataclass, field

from app.database.connection_store import ConnectionProfile


@dataclass(frozen=True)
class DatabaseConnectionConfig:
    """Environment defaults: result limit plus the seed connection used when no connection.json exists."""

    type: str = "mysql"
    host: str = "127.0.0.1"
    port: int = 3306
    username: str = "root"
    password: str = field(default="", repr=False)
    database: str = "sakila"
    result_limit: int = 1000

    def __post_init__(self) -> None:
        if not 1 <= self.port <= 65535 or self.result_limit < 1:
            raise ValueError("Invalid database port or result limit")

    @classmethod
    def from_environment(cls) -> "DatabaseConnectionConfig":
        return cls(
            type=os.getenv("DB_TYPE", "mysql").lower(),
            host=os.getenv("DB_HOST", "127.0.0.1"),
            port=int(os.getenv("DB_PORT", "3306")),
            username=os.getenv("DB_USERNAME", "root"),
            password=os.getenv("DB_PASSWORD", ""),
            database=os.getenv("DB_DATABASE", "sakila"),
            result_limit=int(os.getenv("DB_RESULT_LIMIT", "1000")),
        )

    def seed_profile(self) -> ConnectionProfile:
        return ConnectionProfile(id="local-default", name="Local development", type=self.type, host=self.host,
                                 port=self.port, username=self.username, database=self.database, default=True)
