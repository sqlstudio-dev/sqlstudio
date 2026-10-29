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

import { Braces, Database, Infinity as InfinityIcon, LayoutGrid } from 'lucide-react';
import { ConnectionsHome } from './components/connections/ConnectionsHome';
import { SqlConsole } from './components/sql-console/SqlConsole';
import { useSqlConsole } from './hooks/useSqlConsole';

export default function App() {
  const { state, closeConnection } = useSqlConsole();
  const active = state.activeConnection;
  return <div className="app-shell">
    <aside className="activity-rail" aria-label="Workspace">
      <div className="app-brand" role="img" aria-label="Endless Editor" title="Endless Editor"><InfinityIcon size={24} strokeWidth={2.25} aria-hidden="true" /></div>
      <div className="rail-divider" />
      <button type="button" className={`rail-action${active ? '' : ' active'}`} aria-label="Connections" title="Connections" onClick={closeConnection}><LayoutGrid size={19} /></button>
      <div className={`rail-action${active ? ' active' : ''}`} aria-label="SQL notebook" title="Endless Editor - SQL notebook"><InfinityIcon size={21} aria-hidden="true" /></div>
      <div className="rail-spacer" /><div className="user-mark">EC</div>
    </aside>
    <section className="workspace-shell">
      <header className="window-bar">
        <div className="breadcrumbs"><span>WORKSPACE</span><b>/</b>{active
          ? <><button type="button" className="breadcrumb-link" onClick={closeConnection}>Connections</button><b>/</b><strong>{active.name}</strong></>
          : <strong>Connections</strong>}</div>
        <div className="window-meta"><Braces size={14} /><span>SQL</span><i />{active ? `${active.host}:${active.port}` : 'Local development'}</div>
      </header>
      <div className="editor-tabs">
        {active
          ? <div className="editor-tab"><Database size={14} /><span>{active.name}</span><i /></div>
          : <div className="editor-tab"><LayoutGrid size={14} /><span>Connections</span><i /></div>}
        <div className="tab-context">{active ? 'SQL notebook' : 'Endless console'}</div>
      </div>
      {active ? <SqlConsole /> : <ConnectionsHome />}
    </section>
  </div>;
}
