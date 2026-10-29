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

import { createContext, useCallback, useEffect, useMemo, useReducer, useRef, type Dispatch, type ReactNode } from 'react';
import { addConnection, closeSqlSession, createSqlSession, executeSql, importConnections, loadConnections, removeConnection } from '../actions/sqlConsoleActions';
import type { ConnectionInput, ConnectionProfile, SqlSession } from '../models/sql';
import { initialSqlConsoleState, sqlConsoleReducer, type SqlConsoleAction, type SqlConsoleState } from '../reducers/sqlConsoleReducer';

interface SqlConsoleContextValue {
  state: SqlConsoleState;
  dispatch: Dispatch<SqlConsoleAction>;
  execute: () => Promise<void>;
  retryConnection: () => void;
  reloadConnections: () => void;
  openConnection: (connection: ConnectionProfile, password: string) => void;
  closeConnection: () => void;
  addConnection: (input: ConnectionInput) => Promise<ConnectionProfile>;
  removeConnection: (id: string) => Promise<void>;
  importConnections: (xml: string) => ReturnType<typeof importConnections>;
}
export const SqlConsoleContext = createContext<SqlConsoleContextValue | null>(null);

export function SqlConsoleProvider({ children }: { children: ReactNode }) {
  const [state, dispatch] = useReducer(sqlConsoleReducer, initialSqlConsoleState);
  const connecting = useRef<Promise<SqlSession | null> | null>(null);
  // The password only lives in memory for reconnects; it is never persisted.
  const credentials = useRef<{ connection: ConnectionProfile; password: string } | null>(null);
  const loading = useRef(false);
  const mounted = useRef(false);
  const executing = useRef(false);
  const disposal = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const safeDispatch = useCallback((action: SqlConsoleAction) => { if (mounted.current) dispatch(action); }, []);

  const reloadConnections = useCallback(() => {
    if (loading.current) return;
    loading.current = true;
    void loadConnections(safeDispatch).finally(() => { loading.current = false; });
  }, [safeDispatch]);

  const releaseSession = useCallback(() => {
    const pending = connecting.current;
    connecting.current = null;
    void pending?.then((session) => session && closeSqlSession(session));
  }, []);

  const retryConnection = useCallback(() => {
    if (connecting.current || !credentials.current) return;
    const { connection, password } = credentials.current;
    connecting.current = createSqlSession(connection, password, safeDispatch);
    void connecting.current.then((session) => {
      if (!session) connecting.current = null;
    });
  }, [safeDispatch]);

  const openConnection = useCallback((connection: ConnectionProfile, password: string) => {
    releaseSession();
    credentials.current = { connection, password };
    dispatch({ type: 'CONNECTION_OPENED', connection });
    retryConnection();
  }, [releaseSession, retryConnection]);

  const closeConnection = useCallback(() => {
    releaseSession();
    credentials.current = null;
    dispatch({ type: 'CONNECTION_CLOSED' });
  }, [releaseSession]);

  useEffect(() => {
    mounted.current = true;
    clearTimeout(disposal.current);
    reloadConnections();
    const close = () => { void connecting.current?.then((session) => session && closeSqlSession(session)); };
    window.addEventListener('pagehide', close);
    return () => {
      mounted.current = false;
      window.removeEventListener('pagehide', close);
      // Reuse the connection during StrictMode's immediate effect replay.
      // Dispose real unmounts, even if session creation is still pending.
      disposal.current = setTimeout(close, 0);
    };
  }, [reloadConnections]);

  const execute = useCallback(async () => {
    // Guard calls occurring before React has rendered the pending state.
    if (executing.current) return;
    executing.current = true;
    try { await executeSql(state, dispatch); }
    finally { executing.current = false; }
  }, [state]);

  const value = useMemo<SqlConsoleContextValue>(() => ({
    state, dispatch, execute, retryConnection, reloadConnections, openConnection, closeConnection,
    addConnection: (input) => addConnection(input, safeDispatch),
    removeConnection: (id) => removeConnection(id, safeDispatch),
    importConnections: (xml) => importConnections(xml, safeDispatch),
  }), [state, execute, retryConnection, reloadConnections, openConnection, closeConnection, safeDispatch]);
  return <SqlConsoleContext.Provider value={value}>{children}</SqlConsoleContext.Provider>;
}
