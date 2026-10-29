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

from uuid import uuid4

from fastapi import APIRouter, HTTPException, Response
from starlette.concurrency import run_in_threadpool

from app.database.connection_import import ConnectionImportError, parse_connections_xml
from app.database.connection_store import ConnectionProfile, ConnectionStore
from app.models.responses import ConnectionCreateRequest, ConnectionImportRequest
from app.plugins.loader import PluginRegistry


def create_connections_router(store: ConnectionStore, plugins: PluginRegistry) -> APIRouter:
    router = APIRouter(prefix="/api", tags=["connections"])

    @router.get("/plugins")
    async def list_plugins() -> list[dict[str, object]]:
        return plugins.to_json()

    @router.get("/connections")
    async def list_connections() -> list[dict[str, object]]:
        return [profile.to_json() for profile in await run_in_threadpool(store.list)]

    @router.post("/connections", status_code=201)
    async def add_connection(request: ConnectionCreateRequest) -> dict[str, object]:
        plugin = plugins.for_type(request.type)
        if plugin is None:
            raise HTTPException(status_code=400, detail=f"No connector plugin is loaded for '{request.type}'.")
        profile = ConnectionProfile(id=str(uuid4()), name=request.name.strip(), type=request.type.lower(),
                                    host=request.host.strip(), port=request.port, username=request.username.strip(),
                                    database=request.database.strip())
        await run_in_threadpool(store.add, profile)
        return profile.to_json()

    @router.delete("/connections/{connection_id}", status_code=204)
    async def remove_connection(connection_id: str) -> Response:
        if not await run_in_threadpool(store.remove, connection_id):
            raise HTTPException(status_code=404, detail="Connection not found.")
        return Response(status_code=204)

    @router.post("/connections/import")
    async def import_connections(request: ConnectionImportRequest) -> dict[str, object]:
        try:
            profiles, skipped = parse_connections_xml(request.xml)
        except ConnectionImportError as error:
            raise HTTPException(status_code=400, detail=str(error)) from error
        supported = [p for p in profiles if plugins.for_type(p.type) is not None]
        added = await run_in_threadpool(store.add_many, supported)
        return {"imported": len(added), "duplicates": len(supported) - len(added),
                "skipped": skipped + len(profiles) - len(supported), "connections": [p.to_json() for p in added]}

    return router
