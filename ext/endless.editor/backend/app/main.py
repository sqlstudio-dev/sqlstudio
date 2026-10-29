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

import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.api.connections import create_connections_router
from app.api.sql import create_sql_router
from app.database.config import DatabaseConnectionConfig
from app.database.connection_import import discover_connections_xml, parse_connections_xml
from app.database.connection_store import ConnectionStore, default_connections_file
from app.database.session_manager import SessionManager
from app.plugins.loader import load_plugins

log = logging.getLogger("endless.startup")
config = DatabaseConnectionConfig.from_environment()
plugins = load_plugins()
store = ConnectionStore(default_connections_file())


def initialize_connections(store: ConnectionStore, config: DatabaseConnectionConfig) -> None:
    """Create connection.json on first start from SqlStudio's connections.xml or the environment defaults."""
    if store.exists():
        store.load()
        return
    xml_path = discover_connections_xml()
    if xml_path is not None:
        try:
            profiles, skipped = parse_connections_xml(xml_path.read_text(encoding="utf-8"))
            store.add_many(profiles)
            log.info("Imported %d connections from %s (%d skipped)", len(profiles), xml_path, skipped)
        except Exception as error:
            log.warning("Could not import %s: %s", xml_path, error)
    if not store.list():
        store.add(config.seed_profile())
    store.save()


initialize_connections(store, config)
session_manager = SessionManager(store, plugins, config.result_limit, config.time_series_limit)


@asynccontextmanager
async def lifespan(_: FastAPI):
    yield
    session_manager.close_all()


app = FastAPI(title="Endless Control SQL API", version="1.0.0", lifespan=lifespan)
app.add_middleware(CORSMiddleware, allow_origins=["http://localhost:5173", "http://127.0.0.1:5173", "https://appassets.example"],
                   allow_methods=["GET", "POST", "DELETE", "OPTIONS"], allow_headers=["Content-Type"])
app.include_router(create_connections_router(store, plugins))
app.include_router(create_sql_router(session_manager))


@app.get("/api/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}