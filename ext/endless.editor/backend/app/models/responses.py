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

from datetime import date, datetime, time, timedelta
from decimal import Decimal
from math import isfinite
from typing import Any, Literal

from pydantic import BaseModel, Field


class SqlExecuteRequest(BaseModel):
    sql: str = Field(min_length=1, max_length=1_000_000)


class SessionCreateRequest(BaseModel):
    connectionId: str = Field(min_length=1, max_length=200)
    password: str = Field(default="", max_length=1024)


class SqlSessionResponse(BaseModel):
    sessionId: str
    connectionId: str
    name: str
    database: str | None
    host: str
    port: int
    username: str
    type: str


class ConnectionCreateRequest(BaseModel):
    name: str = Field(min_length=1, max_length=120)
    type: str = Field(min_length=1, max_length=40)
    host: str = Field(min_length=1, max_length=255)
    port: int = Field(ge=1, le=65535)
    username: str = Field(default="", max_length=128)
    database: str = Field(default="", max_length=128)


class ConnectionImportRequest(BaseModel):
    xml: str = Field(min_length=1, max_length=5_000_000)


class TimeSeriesQueryRequest(BaseModel):
    """Server-side window or bucket aggregation over a read-only user query."""

    sql: str = Field(min_length=1, max_length=1_000_000)
    timeColumn: str = Field(min_length=1, max_length=255)
    valueColumns: list[str] = Field(default_factory=list, max_length=64)
    groupColumn: str | None = Field(default=None, max_length=255)
    aggregation: Literal["avg", "sum", "min", "max", "count"] = "avg"
    intervalSeconds: int | None = Field(default=None, ge=1, le=31_536_000)
    since: str | None = Field(default=None, max_length=64)
    until: str | None = Field(default=None, max_length=64)
    sinceExclusive: bool = False
    limit: int | None = Field(default=None, ge=1, le=1_000_000)


def cell_to_json(value: Any) -> Any:
    if value is None or isinstance(value, (str, bool)):
        return value
    if isinstance(value, int):
        # JavaScript numbers cannot represent all BIGINT values exactly.
        return value if abs(value) <= 2**53 - 1 else str(value)
    if isinstance(value, float):
        return value if isfinite(value) else str(value)
    if isinstance(value, Decimal):
        return str(value)
    if isinstance(value, (bytes, bytearray, memoryview)):
        return {"binary": True, "size": len(value)}
    if isinstance(value, (datetime, date, time)):
        return value.isoformat()
    if isinstance(value, timedelta):
        # MySQL TIME also represents durations outside the 24-hour clock.
        return str(value)
    return str(value)
