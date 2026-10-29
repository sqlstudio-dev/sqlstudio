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

import { useCallback, useEffect, useRef } from 'react';
import { ArrowDownToLine, ArrowLeft, Eraser, Play, RotateCw } from 'lucide-react';
import { useSqlConsole } from '../../hooks/useSqlConsole';
import { useSqlKeyboardShortcuts } from '../../hooks/useSqlKeyboardShortcuts';
import { SqlExecutionHistory } from './SqlExecutionHistory';
import { SqlPrompt, type SqlPromptHandle } from './SqlPrompt';

export function SqlConsole() {
  const { state, dispatch, execute, retryConnection, closeConnection } = useSqlConsole();
  const promptRef = useRef<SqlPromptHandle>(null);
  const bottomRef = useRef<HTMLDivElement>(null);
  const pending = state.executions.some((execution) => execution.status === 'pending');
  const canExecute = Boolean(state.currentSql.trim() && state.session && !pending);
  const runQuery = useCallback(() => { if (canExecute) void execute(); }, [canExecute, execute]);
  useSqlKeyboardShortcuts(runQuery);
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth', block: 'end' });
    if (state.sessionStatus === 'ready') promptRef.current?.focus();
  }, [state.executions, state.sessionStatus]);
  const connection = state.activeConnection;
  const engine = state.plugins.find((plugin) => connection && (plugin.id === connection.type || plugin.aliases.includes(connection.type)))?.name ?? connection?.type ?? 'Database';

  return <main className="console-main">
    <header className="console-toolbar"><div className="toolbar-left">
      <button type="button" className="icon-button" aria-label="Back to connections" title="Back to connections" onClick={closeConnection}><ArrowLeft size={15} /></button>
      <span className={`connection-led ${state.sessionStatus}`} />
      <div className="connection-label"><strong>{connection?.name ?? 'SQL session'}</strong>
        <span>{connection ? `${connection.username ? connection.username + '@' : ''}${connection.host}:${connection.port} · ${engine}` : 'Database session'}</span></div>
      <span className={`session-chip ${state.sessionStatus}`}>{state.sessionStatus === 'ready' ? 'CONNECTED' : state.sessionStatus === 'connecting' ? 'CONNECTING' : 'OFFLINE'}</span>
    </div><div className="toolbar-actions">
      <button className="icon-button" aria-label="Clear console" title="Clear history (keeps database session)" disabled={pending} onClick={() => dispatch({ type: 'CLEAR_CONSOLE' })}><Eraser size={15} /></button>
      {state.sessionStatus === 'error' && <button className="icon-button" aria-label="Reconnect" title="Reconnect" onClick={retryConnection}><RotateCw size={15} /></button>}
      <button className="run-button" disabled={!canExecute} onClick={runQuery} title="Run SQL (Ctrl+Enter)"><Play size={13} fill="currentColor" /> Run <kbd>Ctrl ↵</kbd></button>
    </div></header>
    <div className="console-scroll">{state.sessionStatus === 'error' && <div className="connection-error">{state.sessionError}</div>}
      <div className="notebook-content">
        {state.executions.length === 0 && state.sessionStatus === 'ready' && <div className="notebook-welcome"><span className="welcome-index">01</span>
          <div><strong>SQL notebook</strong><span>Session ready. Write a query below to begin.</span></div></div>}
        <SqlExecutionHistory executions={state.executions} />
        <div className="active-cell" ref={bottomRef}><SqlPrompt ref={promptRef} value={state.currentSql} disabled={false}
          history={state.executions.map((execution) => execution.sql)}
          onChange={(sql) => dispatch({ type: 'SQL_INPUT_CHANGED', sql })} />
          {!state.currentSql && !pending && <div className="prompt-hint"><ArrowDownToLine size={12} /> Ctrl/Cmd + Enter to execute · ↑ / ↓ command history · One statement per run</div>}
        </div>
      </div>
    </div>
    <footer className="console-statusbar"><span><i /> {pending ? 'Executing statement' : state.sessionStatus === 'ready' ? 'Ready' : state.sessionStatus === 'connecting' ? 'Opening database session' : 'Connection unavailable'}</span>
      <span>{state.executions.length} {state.executions.length === 1 ? 'execution' : 'executions'}</span></footer>
  </main>;
}
