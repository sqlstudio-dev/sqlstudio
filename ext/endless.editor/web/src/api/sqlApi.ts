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

import type { ConnectionImportResult, ConnectionInput, ConnectionProfile, ConnectorPlugin, SqlExecutionResponse, SqlSession, TimeSeriesQueryRequest, TimeSeriesQueryResponse } from '../models/sql';

const apiBase = (import.meta.env.VITE_API_URL ?? 'http://127.0.0.1:8000').replace(/\/$/, '');

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(apiBase + path, {
    ...init, headers: { 'Content-Type': 'application/json', ...init?.headers },
  });
  if (response.status === 204) return undefined as T;
  const body = await response.json().catch(() => null);
  if (!response.ok) {
    throw new Error(typeof body?.detail === 'string' ? body.detail : 'Backend request failed (' + response.status + ')');
  }
  if (!body) throw new Error('The backend returned an invalid response.');
  return body as T;
}

export const sqlApi = {
  listPlugins: () => request<ConnectorPlugin[]>('/api/plugins'),
  listConnections: () => request<ConnectionProfile[]>('/api/connections'),
  createConnection: (input: ConnectionInput) => request<ConnectionProfile>('/api/connections', { method: 'POST', body: JSON.stringify(input) }),
  deleteConnection: (id: string) => request<void>('/api/connections/' + encodeURIComponent(id), { method: 'DELETE' }),
  importConnections: (xml: string) => request<ConnectionImportResult>('/api/connections/import', { method: 'POST', body: JSON.stringify({ xml }) }),
  createSession: (connectionId: string, password: string) => request<SqlSession>('/api/sessions', { method: 'POST', body: JSON.stringify({ connectionId, password }) }),
  closeSession: (id: string) => request<void>('/api/sessions/' + encodeURIComponent(id), { method: 'DELETE', keepalive: true }),
  execute: (sessionId: string, sql: string, signal?: AbortSignal) => request<SqlExecutionResponse>(
    '/api/sessions/' + encodeURIComponent(sessionId) + '/execute', { method: 'POST', body: JSON.stringify({ sql }), signal },
  ),
  cancel: (sessionId: string) => request<{ cancelled: boolean }>('/api/sessions/' + encodeURIComponent(sessionId) + '/cancel', { method: 'POST' }),
  timeSeries: (sessionId: string, body: TimeSeriesQueryRequest, signal?: AbortSignal) => request<TimeSeriesQueryResponse>(
    '/api/sessions/' + encodeURIComponent(sessionId) + '/time-series', { method: 'POST', body: JSON.stringify(body), signal },
  ),
};
