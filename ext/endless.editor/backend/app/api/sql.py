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

from fastapi import APIRouter, HTTPException, Response
from starlette.concurrency import run_in_threadpool

from app.database.session_manager import SessionManager, UnknownConnectionError, UnsupportedEngineError
from app.models.responses import SessionCreateRequest, SqlExecuteRequest, SqlSessionResponse, TimeSeriesQueryRequest
from app.plugins.base import TimeSeriesQuerySpec, UnsupportedCapabilityError
from app.services.sql_executor import execute_sql


def create_sql_router(session_manager: SessionManager) -> APIRouter:
    router = APIRouter(prefix="/api/sessions", tags=["sql"])

    @router.post("", response_model=SqlSessionResponse)
    async def create_session(request: SessionCreateRequest) -> dict[str, object]:
        try:
            session = await run_in_threadpool(session_manager.create, request.connectionId, request.password)
        except UnknownConnectionError as error:
            raise HTTPException(status_code=404, detail=str(error)) from error
        except UnsupportedEngineError as error:
            raise HTTPException(status_code=400, detail=str(error)) from error
        except Exception as error:
            details = session_manager.describe_error(request.connectionId, error)
            raise HTTPException(status_code=503, detail=f"Could not open the database session: {details}") from error
        profile = session.profile
        return {"sessionId": session.session_id, "connectionId": profile.id, "name": profile.name,
                "database": profile.database or None, "host": profile.host, "port": profile.port,
                "username": profile.username, "type": profile.type}

    @router.delete("/{session_id}", status_code=204)
    async def close_session(session_id: str) -> Response:
        await run_in_threadpool(session_manager.close, session_id)
        return Response(status_code=204)

    @router.post("/{session_id}/execute")
    async def execute(session_id: str, request: SqlExecuteRequest) -> dict[str, object]:
        session = session_manager.get(session_id)
        if session is None:
            raise HTTPException(status_code=404, detail="SQL session not found. Reload to start a new session.")
        if not request.sql.strip():
            raise HTTPException(status_code=400, detail="SQL statement must not be empty.")
        return await run_in_threadpool(execute_sql, session, request.sql)

    @router.post("/{session_id}/cancel")
    async def cancel(session_id: str) -> dict[str, object]:
        session = session_manager.get(session_id)
        if session is None:
            raise HTTPException(status_code=404, detail="SQL session not found.")
        try:
            cancelled = await run_in_threadpool(session.cancel)
        except UnsupportedCapabilityError as error:
            raise HTTPException(status_code=400, detail=str(error)) from error
        except Exception as error:
            details = session.plugin.error_details(error)
            raise HTTPException(status_code=503, detail=str(details["message"]) if details else "Cancel request failed.") from error
        return {"cancelled": bool(cancelled)}

    @router.post("/{session_id}/time-series")
    async def time_series(session_id: str, request: TimeSeriesQueryRequest) -> dict[str, object]:
        """Run a plugin-generated window/bucket query derived from the user's read-only SQL."""
        session = session_manager.get(session_id)
        if session is None:
            raise HTTPException(status_code=404, detail="SQL session not found. Reload to start a new session.")
        spec = TimeSeriesQuerySpec(sql=request.sql, time_column=request.timeColumn, value_columns=tuple(request.valueColumns),
                                   group_column=request.groupColumn, aggregation=request.aggregation,
                                   interval_seconds=request.intervalSeconds, since=request.since, until=request.until,
                                   since_exclusive=request.sinceExclusive, limit=request.limit or session_manager.time_series_limit)
        if spec.interval_seconds and not spec.value_columns:
            raise HTTPException(status_code=400, detail="Aggregation needs at least one value column.")
        try:
            sql = session.plugin.time_series_query(spec)
        except UnsupportedCapabilityError as error:
            raise HTTPException(status_code=400, detail=str(error)) from error
        except ValueError as error:
            raise HTTPException(status_code=400, detail=str(error)) from error
        return {"sql": sql, "result": await run_in_threadpool(execute_sql, session, sql, spec.limit)}

    return router
