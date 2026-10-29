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

import { useMemo, useRef, useState, type ChangeEvent } from 'react';
import { FileUp, Plus, RotateCw } from 'lucide-react';
import { useSqlConsole } from '../../hooks/useSqlConsole';
import type { ConnectionProfile, ConnectorPlugin } from '../../models/sql';
import { AddConnectionDialog } from './AddConnectionDialog';
import { ConnectDialog } from './ConnectDialog';
import { ConnectionTile } from './ConnectionTile';

interface EngineGroup { key: string; label: string; plugin?: ConnectorPlugin; connections: ConnectionProfile[] }

function groupByEngine(connections: ConnectionProfile[], plugins: ConnectorPlugin[]): EngineGroup[] {
  const groups = new Map<string, EngineGroup>();
  for (const connection of connections) {
    const plugin = plugins.find((item) => item.id === connection.type || item.aliases.includes(connection.type));
    const key = plugin?.id ?? connection.type;
    const group = groups.get(key) ?? { key, label: plugin?.name ?? connection.type.toUpperCase(), plugin, connections: [] };
    group.connections.push(connection);
    groups.set(key, group);
  }
  for (const group of groups.values()) group.connections.sort((a, b) => Number(b.default) - Number(a.default) || a.name.localeCompare(b.name));
  return [...groups.values()].sort((a, b) => a.label.localeCompare(b.label));
}

export function ConnectionsHome() {
  const { state, openConnection, reloadConnections, addConnection, removeConnection, importConnections } = useSqlConsole();
  const [adding, setAdding] = useState(false);
  const [connecting, setConnecting] = useState<ConnectionProfile | null>(null);
  const [notice, setNotice] = useState<{ kind: 'info' | 'error'; text: string } | null>(null);
  const fileInput = useRef<HTMLInputElement>(null);
  const groups = useMemo(() => groupByEngine(state.connections, state.plugins), [state.connections, state.plugins]);

  const onImportFile = async (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    event.target.value = '';
    if (!file) return;
    try {
      const result = await importConnections(await file.text());
      setNotice({ kind: 'info', text: `Imported ${result.imported} connection${result.imported === 1 ? '' : 's'} from ${file.name}` + (result.duplicates ? `, ${result.duplicates} already present` : '') + (result.skipped ? `, ${result.skipped} unsupported` : '') + '.' });
    } catch (error) { setNotice({ kind: 'error', text: error instanceof Error ? error.message : 'Import failed.' }); }
  };
  const remove = async (connection: ConnectionProfile) => {
    try { await removeConnection(connection.id); setNotice({ kind: 'info', text: `Removed ${connection.name}.` }); }
    catch (error) { setNotice({ kind: 'error', text: error instanceof Error ? error.message : 'Could not remove the connection.' }); }
  };

  return <main className="connections-home">
    <header className="connections-header">
      <div><h1>Connections</h1><p>{state.connections.length} saved · {state.plugins.filter((plugin) => plugin.available).length} engine plugins ready</p></div>
      <div className="toolbar-actions">
        <input ref={fileInput} type="file" accept=".xml,text/xml,application/xml" hidden aria-label="Connections XML file" onChange={onImportFile} />
        <button type="button" className="secondary-button" onClick={() => fileInput.current?.click()} title="Import SqlStudio / Workbench connections.xml"><FileUp size={14} /> Import XML</button>
        <button type="button" className="run-button" onClick={() => setAdding(true)} disabled={state.connectionsStatus !== 'ready'}><Plus size={14} /> Add connection</button>
      </div>
    </header>
    {notice && <div className={`home-notice ${notice.kind}`} role="status">{notice.text}</div>}
    <div className="connections-scroll">
      {state.connectionsStatus === 'loading' && <p className="home-empty">Loading connections…</p>}
      {state.connectionsStatus === 'error' && <div className="connection-error" role="alert">{state.connectionsError}
        <button type="button" className="secondary-button" onClick={reloadConnections}><RotateCw size={13} /> Retry</button></div>}
      {state.connectionsStatus === 'ready' && groups.length === 0 && <div className="home-empty">
        <strong>No connections yet</strong><span>Add a MySQL, PostgreSQL, or QuestDB connection, or import a SqlStudio connections.xml file.</span></div>}
      {groups.map((group) => <section key={group.key} className="engine-group" aria-label={`${group.label} connections`}>
        <h2>{group.label}<small>{group.connections.length}</small>{group.plugin && !group.plugin.available && <em className="warning-text">driver not installed</em>}</h2>
        <div className="tile-grid">
          {group.connections.map((connection) => <ConnectionTile key={connection.id} connection={connection} plugin={group.plugin}
            onOpen={() => setConnecting(connection)} onRemove={() => void remove(connection)} />)}
        </div>
      </section>)}
    </div>
    {adding && <AddConnectionDialog plugins={state.plugins} onSubmit={addConnection} onClose={() => setAdding(false)} />}
    {connecting && <ConnectDialog connection={connecting} onClose={() => setConnecting(null)}
      onConnect={(password) => { const target = connecting; setConnecting(null); openConnection(target, password); }} />}
  </main>;
}
