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

import importlib.util
import logging
import os
from pathlib import Path

from app.plugins.base import ConnectorPlugin

log = logging.getLogger("endless.plugins")
PLUGIN_SUFFIX = "_connector.py"


def default_plugins_dir() -> Path:
    override = os.getenv("ENDLESS_PLUGINS_DIR")
    return Path(override) if override else Path(__file__).resolve().parents[2] / "plugins"


class PluginRegistry:
    def __init__(self, plugins: list[ConnectorPlugin]) -> None:
        self._plugins = {plugin.id: plugin for plugin in plugins}
        self._by_type = dict(self._plugins)
        for plugin in plugins:
            for alias in plugin.aliases:
                self._by_type.setdefault(alias, plugin)

    def __iter__(self):
        return iter(self._plugins.values())

    def __len__(self) -> int:
        return len(self._plugins)

    def for_type(self, connection_type: str) -> ConnectorPlugin | None:
        return self._by_type.get(connection_type.lower())

    def to_json(self) -> list[dict[str, object]]:
        return [plugin.to_json() for plugin in self._plugins.values()]


def load_plugins(directory: Path | None = None) -> PluginRegistry:
    """Import every ``*_connector.py`` in ``directory`` and collect its ``plugin``."""
    directory = directory or default_plugins_dir()
    plugins: list[ConnectorPlugin] = []
    if not directory.is_dir():
        log.warning("Plugin directory %s does not exist; no database engines loaded", directory)
        return PluginRegistry(plugins)
    for path in sorted(directory.glob("*" + PLUGIN_SUFFIX)):
        module_name = "endless_plugins." + path.stem
        try:
            spec = importlib.util.spec_from_file_location(module_name, path)
            if spec is None or spec.loader is None:
                raise ImportError("cannot build a module spec")
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            plugin = getattr(module, "plugin", None)
            if not isinstance(plugin, ConnectorPlugin) or not plugin.id:
                raise TypeError("module must define `plugin`, a ConnectorPlugin instance with an id")
            if any(existing.id == plugin.id for existing in plugins):
                raise ValueError(f"duplicate plugin id {plugin.id!r}")
        except Exception as error:
            log.warning("Skipping plugin %s: %s", path.name, error)
            continue
        plugins.append(plugin)
        log.info("Loaded plugin %s (%s)%s", plugin.id, path.name,
                 "" if plugin.availability() is None else " - driver unavailable")
    return PluginRegistry(plugins)
