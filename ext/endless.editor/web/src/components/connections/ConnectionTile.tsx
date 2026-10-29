/*
 * Copyright (c) 2026 dev4fun. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

import { Trash2 } from 'lucide-react';
import type { ConnectionProfile, ConnectorPlugin } from '../../models/sql';
import { Watermark } from './Watermark';

interface ConnectionTileProps { connection: ConnectionProfile; plugin?: ConnectorPlugin; onOpen: () => void; onRemove: () => void }

const lastUsed = (seconds: number | null) => seconds ? new Date(seconds * 1000).toLocaleDateString() : 'never';

export function ConnectionTile({ connection, plugin, onOpen, onRemove }: ConnectionTileProps) {
  const version = typeof connection.options.serverVersion === 'string' ? connection.options.serverVersion : '';
  return <article className={`connection-tile${plugin && !plugin.available ? ' unavailable' : ''}`}>
    <button type="button" className="tile-open" onClick={onOpen} aria-label={`Open ${connection.name}`} title={plugin?.unavailableReason ?? `Connect to ${connection.name}`}>
      <Watermark kind={plugin?.watermark ?? connection.type} />
      <header><strong>{connection.name}</strong>{connection.default && <em>default</em>}</header>
      <span className="tile-endpoint">{connection.username ? `${connection.username}@` : ''}{connection.host}:{connection.port}</span>
      <span className="tile-detail">{connection.database || 'no default schema'}{version && ` · v${version}`}</span>
      <footer><span>{plugin?.name ?? connection.type}</span><span>last used {lastUsed(connection.lastConnected)}</span></footer>
    </button>
    <button type="button" className="tile-remove" aria-label={`Remove ${connection.name}`} title="Remove connection" onClick={onRemove}><Trash2 size={13} /></button>
  </article>;
}
