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

import { StrictMode } from 'react';
import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import App from '../src/App';
import { SqlConsoleProvider } from '../src/context/SqlConsoleContext';
import { sqlApi } from '../src/api/sqlApi';
import type { ConnectionProfile, ConnectorPlugin, SqlExecutionResponse } from '../src/models/sql';

vi.mock('../src/api/sqlApi', () => ({ sqlApi: {
  listPlugins: vi.fn(), listConnections: vi.fn(), createConnection: vi.fn(), deleteConnection: vi.fn(), importConnections: vi.fn(),
  createSession: vi.fn(), execute: vi.fn(), closeSession: vi.fn(),
} }));
const plugins: ConnectorPlugin[] = [
  { id: 'mysql', name: 'MySQL', defaultPort: 3306, watermark: 'mysql', driver: 'PyMySQL', aliases: ['mariadb'], available: true, unavailableReason: null },
  { id: 'postgresql', name: 'PostgreSQL', defaultPort: 5432, watermark: 'postgresql', driver: 'psycopg', aliases: [], available: true, unavailableReason: null },
];
const profile = (over: Partial<ConnectionProfile>): ConnectionProfile => ({
  id: 'c1', name: 'test', type: 'mysql', host: 'localhost', port: 3306, username: 'root', database: 'sakila', default: true, lastConnected: null, options: {}, ...over,
});
const connections = [profile({}), profile({ id: 'c2', name: 'analytics', type: 'postgresql', port: 5432, default: false }), profile({ id: 'c3', name: 'legacy', type: 'mariadb', port: 3307, default: false })];
const session = { sessionId: 'one', connectionId: 'c1', name: 'test', database: 'sakila', host: 'localhost', port: 3306, username: 'root', type: 'mysql' };
const result: SqlExecutionResponse = { type: 'resultset', columns: [{ name: 'value' }], rows: [[null], [42]], returnedRows: 2, truncated: false, warningCount: 0, executionTimeMs: 12 };
const mount = () => render(<StrictMode><SqlConsoleProvider><App /></SqlConsoleProvider></StrictMode>);
const historyHas = (sql: string) => Array.from(document.querySelectorAll('.executed-prompt pre')).some((entry) => entry.textContent === sql);
const openTile = async (name = 'test', password = 'pw') => {
  fireEvent.click(await screen.findByRole('button', { name: `Open ${name}` }));
  fireEvent.change(screen.getByLabelText('Password'), { target: { value: password } });
  fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
};

beforeEach(() => {
  vi.clearAllMocks();
  vi.mocked(sqlApi.listPlugins).mockResolvedValue(plugins);
  vi.mocked(sqlApi.listConnections).mockResolvedValue(connections);
  vi.mocked(sqlApi.createSession).mockResolvedValue(session);
  vi.mocked(sqlApi.closeSession).mockResolvedValue(undefined);
  Element.prototype.scrollIntoView = vi.fn();
});
afterEach(cleanup);

describe('Connections home', () => {
  it('groups tiles by engine with watermarks, adds a connection, imports XML, and removes tiles', async () => {
    mount();
    const mysql = within(await screen.findByRole('region', { name: 'MySQL connections' }));
    expect(mysql.getByRole('button', { name: 'Open test' })).toBeTruthy();
    expect(mysql.getByRole('button', { name: 'Open legacy' })).toBeTruthy();
    expect(within(screen.getByRole('region', { name: 'PostgreSQL connections' })).getByRole('button', { name: 'Open analytics' })).toBeTruthy();
    expect(sqlApi.listConnections).toHaveBeenCalledTimes(1);
    expect(document.querySelectorAll('img.tile-watermark')).toHaveLength(3);
    expect(document.querySelectorAll('.watermark-mysql')).toHaveLength(2);
    expect(document.querySelectorAll('.watermark-postgresql')).toHaveLength(1);

    vi.mocked(sqlApi.createConnection).mockResolvedValue(profile({ id: 'c4', name: 'warehouse', type: 'postgresql', host: 'db.local', port: 5433, default: false }));
    fireEvent.click(screen.getByRole('button', { name: 'Add connection' }));
    const dialog = within(screen.getByRole('dialog', { name: 'Add connection' }));
    fireEvent.change(dialog.getByLabelText('Database engine'), { target: { value: 'postgresql' } });
    expect((dialog.getByLabelText('Port') as HTMLInputElement).value).toBe('5432');
    fireEvent.change(dialog.getByLabelText('Name'), { target: { value: 'warehouse' } });
    fireEvent.change(dialog.getByLabelText('Host'), { target: { value: 'db.local' } });
    fireEvent.change(dialog.getByLabelText('Port'), { target: { value: '5433' } });
    fireEvent.change(dialog.getByLabelText('Username'), { target: { value: 'app' } });
    fireEvent.click(dialog.getByRole('button', { name: 'Save connection' }));
    await waitFor(() => expect(screen.queryByRole('dialog')).toBeNull());
    expect(sqlApi.createConnection).toHaveBeenCalledWith({ name: 'warehouse', type: 'postgresql', host: 'db.local', port: 5433, username: 'app', database: '' });
    expect(await screen.findByRole('button', { name: 'Open warehouse' })).toBeTruthy();

    vi.mocked(sqlApi.importConnections).mockResolvedValue({ imported: 1, duplicates: 2, skipped: 0, connections: [profile({ id: 'c5', name: 'imported', default: false })] });
    const file = new File(['<data/>'], 'connections.xml', { type: 'text/xml' });
    await act(async () => { fireEvent.change(screen.getByLabelText('Connections XML file'), { target: { files: [file] } }); });
    await screen.findByText('Imported 1 connection from connections.xml, 2 already present.');
    expect(sqlApi.importConnections).toHaveBeenCalledWith('<data/>');
    expect(screen.getByRole('button', { name: 'Open imported' })).toBeTruthy();

    vi.mocked(sqlApi.deleteConnection).mockResolvedValue(undefined);
    fireEvent.click(screen.getByRole('button', { name: 'Remove legacy' }));
    await waitFor(() => expect(screen.queryByRole('button', { name: 'Open legacy' })).toBeNull());
    expect(sqlApi.deleteConnection).toHaveBeenCalledWith('c3');
  });

  it('shows an empty state and a retry when loading fails', async () => {
    vi.mocked(sqlApi.listConnections).mockRejectedValueOnce(new Error('Backend offline')).mockResolvedValueOnce([]);
    mount();
    fireEvent.click(await screen.findByRole('button', { name: 'Retry' }));
    await screen.findByText('No connections yet');
  });
});

describe('SQL notebook', () => {
  it('prompts for a password, opens one session in StrictMode, and closes it on unmount', async () => {
    const view = mount();
    await openTile();
    await screen.findByText('CONNECTED');
    expect(sqlApi.createSession).toHaveBeenCalledTimes(1);
    expect(sqlApi.createSession).toHaveBeenCalledWith('c1', 'pw');
    expect(screen.getByText('root@localhost:3306 · MySQL')).toBeTruthy();
    expect(document.activeElement).toBe(screen.getByRole('textbox', { name: 'SQL command' }));
    view.unmount();
    await waitFor(() => expect(sqlApi.closeSession).toHaveBeenCalledWith('one'));
  });

  it('returns to the connections home and closes the session', async () => {
    mount();
    await openTile();
    await screen.findByText('CONNECTED');
    fireEvent.click(screen.getByRole('button', { name: 'Back to connections' }));
    await screen.findByRole('button', { name: 'Open test' });
    await waitFor(() => expect(sqlApi.closeSession).toHaveBeenCalledWith('one'));
  });

  it('preserves history, shows a skeleton, blocks duplicates, and permits drafting the next query', async () => {
    let finish!: (result: SqlExecutionResponse) => void;
    vi.mocked(sqlApi.execute).mockImplementationOnce(() => new Promise((resolve) => { finish = resolve; }));
    mount();
    await openTile();
    await screen.findByText('CONNECTED');
    const input = screen.getByRole('textbox') as HTMLTextAreaElement;
    fireEvent.change(input, { target: { value: 'SELECT 1' } });
    fireEvent.keyDown(input, { key: 'Enter', ctrlKey: true });
    expect(screen.getByRole('status', { name: 'Query result loading' })).toBeTruthy();
    expect(input.value).toBe('');
    expect(input.disabled).toBe(false);
    fireEvent.change(input, { target: { value: 'SELECT 2' } });
    fireEvent.keyDown(input, { key: 'Enter', metaKey: true });
    fireEvent.click(screen.getByRole('button', { name: 'Clear console' }));
    expect(sqlApi.execute).toHaveBeenCalledTimes(1);
    expect(historyHas('SELECT 1')).toBe(true);
    await act(async () => finish(result));
    expect(screen.getByText('NULL')).toBeTruthy();
    expect(screen.getByText('42')).toBeTruthy();
    expect(input.value).toBe('SELECT 2');
    vi.mocked(sqlApi.execute).mockResolvedValueOnce({ type: 'error', error: { message: 'Table does not exist', code: 1146, executionTimeMs: 1 } });
    fireEvent.keyDown(input, { key: 'Enter', metaKey: true });
    await screen.findByText('Table does not exist');
    expect(historyHas('SELECT 1')).toBe(true);
    expect(historyHas('SELECT 2')).toBe(true);
    expect(screen.getAllByRole('textbox')).toHaveLength(1);
    expect(document.activeElement).toBe(input);
    vi.mocked(sqlApi.execute).mockResolvedValueOnce(result);
    fireEvent.change(input, { target: { value: 'SELECT 3' } });
    fireEvent.keyDown(input, { key: 'Enter', ctrlKey: true });
    await waitFor(() => expect(screen.getAllByText('42')).toHaveLength(2));
    expect(sqlApi.execute).toHaveBeenLastCalledWith('one', 'SELECT 3');
  });

  it('retries a failed connection with the same password and ignores blank input', async () => {
    vi.mocked(sqlApi.createSession).mockRejectedValueOnce(new Error('Connection refused'));
    mount();
    await openTile('test', 'secret');
    await screen.findByText('Connection refused');
    fireEvent.click(screen.getByRole('button', { name: 'Reconnect' }));
    await screen.findByText('CONNECTED');
    expect(sqlApi.createSession).toHaveBeenNthCalledWith(2, 'c1', 'secret');
    fireEvent.change(screen.getByRole('textbox'), { target: { value: '   ' } });
    fireEvent.keyDown(screen.getByRole('textbox'), { key: 'Enter', ctrlKey: true });
    expect(sqlApi.execute).not.toHaveBeenCalled();
  });
});
