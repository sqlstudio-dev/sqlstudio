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

import { sqlApi } from '../api/sqlApi';
import type { ConnectionInput, ConnectionProfile, SqlExecutionResponse, SqlSession } from '../models/sql';
import type { SqlConsoleAction, SqlConsoleState } from '../reducers/sqlConsoleReducer';

type Dispatch = (action: SqlConsoleAction) => void;
const messageOf = (error: unknown, fallback: string) => error instanceof Error ? error.message : fallback;

export async function loadConnections(dispatch: Dispatch): Promise<void> {
  dispatch({ type: 'CONNECTIONS_LOAD_REQUEST' });
  try {
    const [connections, plugins] = await Promise.all([sqlApi.listConnections(), sqlApi.listPlugins()]);
    dispatch({ type: 'CONNECTIONS_LOAD_SUCCESS', connections, plugins });
  } catch (error) {
    dispatch({ type: 'CONNECTIONS_LOAD_FAILURE', message: messageOf(error, 'Could not load connections.') });
  }
}

export async function addConnection(input: ConnectionInput, dispatch: Dispatch): Promise<ConnectionProfile> {
  const connection = await sqlApi.createConnection(input);
  dispatch({ type: 'CONNECTION_ADDED', connection });
  return connection;
}

export async function removeConnection(id: string, dispatch: Dispatch): Promise<void> {
  await sqlApi.deleteConnection(id);
  dispatch({ type: 'CONNECTION_REMOVED', id });
}

export async function importConnections(xml: string, dispatch: Dispatch) {
  const result = await sqlApi.importConnections(xml);
  dispatch({ type: 'CONNECTIONS_IMPORTED', connections: result.connections });
  return result;
}

export async function createSqlSession(connection: ConnectionProfile, password: string, dispatch: Dispatch): Promise<SqlSession | null> {
  dispatch({ type: 'SESSION_CREATE_REQUEST' });
  try {
    const session = await sqlApi.createSession(connection.id, password);
    dispatch({ type: 'SESSION_CREATE_SUCCESS', session });
    return session;
  }
  catch (error) {
    dispatch({ type: 'SESSION_CREATE_FAILURE', message: messageOf(error, 'Could not connect.') });
    return null;
  }
}

export async function executeSql(state: SqlConsoleState, dispatch: Dispatch): Promise<void> {
  const sql = state.currentSql.trim();
  if (!sql || !state.session || state.executions.some((item) => item.status === 'pending')) return;
  const startedAt = new Date().toISOString();
  const id = crypto.randomUUID();
  const pending = { id, sql, startedAt, status: 'pending' as const };
  dispatch({ type: 'EXECUTE_SQL_REQUEST', execution: pending });
  try {
    const response: SqlExecutionResponse = await sqlApi.execute(state.session.sessionId, sql);
    if (response.type === 'error') {
      dispatch({ type: 'EXECUTE_SQL_FAILURE', id, execution: { ...pending, finishedAt: new Date().toISOString(), status: 'error', error: response.error, partialResults: response.partialResults } });
      return;
    }
    dispatch({ type: 'EXECUTE_SQL_SUCCESS', id, execution: { ...pending, finishedAt: new Date().toISOString(), status: 'success', result: response } });
  } catch (error) {
    dispatch({ type: 'EXECUTE_SQL_FAILURE', id, execution: {
      ...pending, finishedAt: new Date().toISOString(), status: 'error',
      error: { message: error instanceof Error ? error.message : 'SQL execution failed.', executionTimeMs: Date.now() - new Date(startedAt).getTime() },
    } });
  }
}
export async function closeSqlSession(session: SqlSession): Promise<void> {
  await sqlApi.closeSession(session.sessionId).catch(() => undefined);
}
