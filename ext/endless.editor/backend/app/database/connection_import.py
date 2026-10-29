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

"""Import SqlStudio / MySQL Workbench GRT ``connections.xml`` into connection profiles."""

import os
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

from app.database.connection_store import ConnectionProfile

# Keys of db.mgmt.Connection.parameterValues that are kept as non-secret options.
KEPT_OPTIONS = ("serverVersion", "useSSL", "SQL_MODE", "sslCA", "sslCert", "sslKey", "sslCipher")
DEFAULT_PORTS = {"mysql": 3306, "mariadb": 3306, "postgresql": 5432}


class ConnectionImportError(ValueError):
    pass


def driver_type(driver_link: str) -> str | None:
    driver = driver_link.lower()
    if "mariadb" in driver:
        return "mariadb"
    if "mysql" in driver:
        return "mysql"
    if "postgres" in driver:
        return "postgresql"
    return None


def parse_connections_xml(text: str) -> tuple[list[ConnectionProfile], int]:
    """Return imported profiles and the number of entries skipped for unknown drivers."""
    try:
        root = ElementTree.fromstring(text)
    except ElementTree.ParseError as error:
        raise ConnectionImportError(f"The file is not valid connections XML: {error}") from error
    profiles: list[ConnectionProfile] = []
    skipped = 0
    for node in root.iter("value"):
        if node.get("struct-name") != "db.mgmt.Connection":
            continue
        fields = {child.get("key"): (child.text or "") for child in node if child.get("key")}
        driver = next((child.text or "" for child in node if child.tag == "link" and child.get("key") == "driver"), "")
        connection_type = driver_type(driver)
        if connection_type is None:
            skipped += 1
            continue
        parameters = next((child for child in node if child.get("key") == "parameterValues"), None)
        values = {p.get("key"): (p.text or "") for p in parameters} if parameters is not None else {}
        port = int(values.get("port") or DEFAULT_PORTS[connection_type])
        identifier = (node.get("id") or "").strip("{}").lower() or f"{connection_type}-{values.get('hostName', '')}-{port}"
        last = values.get("lastConnected") or ""
        profiles.append(ConnectionProfile(
            id=identifier, name=fields.get("name") or fields.get("hostIdentifier") or "Imported connection",
            type=connection_type, host=values.get("hostName") or "127.0.0.1", port=port,
            username=values.get("userName", ""), database=values.get("schema", ""),
            default=fields.get("isDefault", "0") == "1", lastConnected=int(last) if last.isdigit() else None,
            options={key: values[key] for key in KEPT_OPTIONS if values.get(key)},
        ))
    return profiles, skipped


def discover_connections_xml() -> Path | None:
    """Find a SqlStudio connections.xml using the environment or the OS user data folder."""
    override = os.getenv("SQLSTUDIO_CONNECTIONS_XML")
    candidates = [Path(override)] if override else []
    home = Path.home()
    if sys.platform == "win32" and os.getenv("APPDATA"):
        candidates.append(Path(os.environ["APPDATA"]) / "MySQL" / "SqlStudio" / "connections.xml")
    elif sys.platform == "darwin":
        candidates.append(home / "Library" / "Application Support" / "MySQL" / "SqlStudio" / "connections.xml")
    else:
        candidates.append(home / ".config" / "MySQL" / "SqlStudio" / "connections.xml")
    return next((path for path in candidates if path.is_file()), None)
