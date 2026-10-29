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
import type { ConnectionProfile, ConnectorPlugin, SqlExecutionResponse, SqlResultSet } from '../src/models/sql';

vi.mock('../src/api/sqlApi', () => ({ sqlApi: {
  listPlugins: vi.fn(), listConnections: vi.fn(), createConnection: vi.fn(), deleteConnection: vi.fn(), importConnections: vi.fn(),
  createSession: vi.fn(), execute: vi.fn(), closeSession: vi.fn(), cancel: vi.fn(), timeSeries: vi.fn(),
} }));
const capabilities = { timeSeries: true, supportedLiveModes: ['polling' as const], serverSideAggregation: true, timeBucketing: true, incrementalQueries: true, latestValueQueries: false, queryCancellation: true };
const plugins: ConnectorPlugin[] = [{ id: 'mysql', name: 'MySQL', defaultPort: 3306, watermark: 'mysql', driver: 'PyMySQL', aliases: ['mariadb'], available: true, unavailableReason: null, capabilities }];
const connection: ConnectionProfile = { id: 'c1', name: 'test', type: 'mysql', host: 'localhost', port: 3306, username: 'root', database: 'sakila', default: true, lastConnected: null, options: {} };
const session = { sessionId: 'one', connectionId: 'c1', name: 'test', database: 'sakila', host: 'localhost', port: 3306, username: 'root', type: 'mysql' };
const timeSeriesResult: SqlExecutionResponse = {
  type: 'resultset', columns: [{ name: 'ts', type: 'DATETIME', kind: 'timestamp' }, { name: 'device', type: 'VARCHAR', kind: 'string' }, { name: 'value', type: 'DOUBLE', kind: 'number' }],
  rows: Array.from({ length: 40 }, (_, i) => [`2026-01-01T10:00:${String(i % 20).padStart(2, '0')}`, i % 2 ? 'B' : 'A', i * 1.5]),
  returnedRows: 40, truncated: false, warningCount: 0, executionTimeMs: 3,
};
const plainResult: SqlExecutionResponse = { type: 'resultset', columns: [{ name: 'name', kind: 'string' }, { name: 'city', kind: 'string' }], rows: [['a', 'x'], ['b', 'y']], returnedRows: 2, truncated: false, warningCount: 0, executionTimeMs: 1 };

const mount = () => render(<StrictMode><SqlConsoleProvider><App /></SqlConsoleProvider></StrictMode>);
const openNotebook = async () => {
  fireEvent.click(await screen.findByRole('button', { name: 'Open test' }));
  fireEvent.change(screen.getByLabelText('Password'), { target: { value: 'pw' } });
  fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
  await screen.findByText('CONNECTED');
};
const run = async (sql: string) => {
  const input = screen.getByRole('textbox', { name: 'SQL command' });
  fireEvent.change(input, { target: { value: sql } });
  fireEvent.keyDown(input, { key: 'Enter', ctrlKey: true });
  await screen.findByRole('tablist', { name: 'Query result views' });
};

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  vi.mocked(sqlApi.listPlugins).mockResolvedValue(plugins);
  vi.mocked(sqlApi.listConnections).mockResolvedValue([connection]);
  vi.mocked(sqlApi.createSession).mockResolvedValue(session);
  vi.mocked(sqlApi.closeSession).mockResolvedValue(undefined);
  Element.prototype.scrollIntoView = vi.fn();
});
afterEach(cleanup);

describe('result tabs', () => {
  it.each(['success', 'failure'])('keeps the existing chart visible while a refresh finishes with %s', async (outcome) => {
    let finish!: (value: { sql: string; result: SqlExecutionResponse }) => void;
    let fail!: (error: Error) => void;
    vi.mocked(sqlApi.execute).mockResolvedValueOnce(timeSeriesResult);
    vi.mocked(sqlApi.timeSeries).mockImplementationOnce(() => new Promise((resolve, reject) => { finish = resolve; fail = reject; }));
    mount(); await openNotebook(); await run('SELECT ts, device, value FROM m');
    fireEvent.click(screen.getByRole('tab', { name: 'Time Series' }));
    const chart = screen.getByTestId('time-series-chart');
    fireEvent.click(screen.getByRole('button', { name: 'Refresh now' }));
    expect(screen.getByRole('progressbar', { name: 'Refreshing query data' })).toBeTruthy();
    expect(screen.getByRole('region', { name: 'Time series' }).getAttribute('aria-busy')).toBe('true');
    expect(screen.getByTestId('time-series-chart')).toBe(chart);
    expect(document.querySelector('.result-skeleton')).toBeNull();
    await act(async () => {
      if (outcome === 'success') finish({ sql: 'wrapped', result: timeSeriesResult });
      else fail(new Error('Refresh unavailable'));
    });
    expect(screen.queryByRole('progressbar')).toBeNull();
    expect(screen.getByRole('region', { name: 'Time series' }).getAttribute('aria-busy')).toBe('false');
    expect(screen.getByTestId('time-series-chart')).toBe(chart);
    if (outcome === 'failure') expect(screen.getByRole('alert').textContent).toContain('Refresh unavailable');
  });

  it.each(['Time Series', 'Statistics', 'Visualize'])('shows a skeleton instead of an empty %s view during refresh', async (tab) => {
    let finish!: (value: SqlExecutionResponse) => void;
    const emptyResult: SqlResultSet = {
      type: 'resultset', columns: [{ name: 'ts', kind: 'timestamp' }, { name: 'value', kind: 'number' }],
      rows: [], returnedRows: 0, truncated: false, warningCount: 0, executionTimeMs: 1,
    };
    vi.mocked(sqlApi.execute).mockResolvedValueOnce(emptyResult)
      .mockImplementationOnce(() => new Promise((resolve) => { finish = resolve; }));
    mount(); await openNotebook(); await run('SELECT ts, value FROM m');
    fireEvent.click(screen.getByRole('tab', { name: tab }));
    fireEvent.click(screen.getByRole('button', { name: 'Refresh now' }));
    expect(screen.getByRole('progressbar', { name: 'Loading query data' })).toBeTruthy();
    expect(screen.queryByText('No time-series columns detected')).toBeNull();
    expect(screen.queryByText('No samples in the selected range.')).toBeNull();
    expect(screen.queryByTestId('time-series-chart')).toBeNull();
    if (tab !== 'Statistics') expect(document.querySelector('.chart-skeleton')).toBeTruthy();
    await act(async () => finish(emptyResult));
    expect(screen.queryByRole('progressbar')).toBeNull();
    expect(screen.getByText('No time-series columns detected')).toBeTruthy();
  });

  it('keeps the data grid for plain results and offers manual column mapping', async () => {
    vi.mocked(sqlApi.execute).mockResolvedValueOnce(plainResult);
    mount(); await openNotebook(); await run('SELECT name, city FROM people');
    expect(screen.getByRole('tab', { name: 'Data Grid', selected: true })).toBeTruthy();
    expect(screen.getByText('x')).toBeTruthy();
    fireEvent.click(screen.getByRole('tab', { name: 'Time Series' }));
    expect(screen.getByText('No time-series columns detected')).toBeTruthy();
    expect(screen.getByRole('combobox', { name: 'X axis' })).toBeTruthy();
    expect(screen.queryByTestId('time-series-chart')).toBeNull();
  });

  it('detects time-series results, renders chart and statistics, and persists preferences per query', async () => {
    vi.mocked(sqlApi.execute).mockResolvedValue(timeSeriesResult);
    mount(); await openNotebook(); await run('SELECT ts, device, value FROM m');
    expect(screen.getByRole('tab', { name: 'Data Grid', selected: true })).toBeTruthy();
    expect(screen.getByRole('columnheader', { name: /device/ })).toBeTruthy();
    fireEvent.click(screen.getByRole('tab', { name: 'Time Series' }));
    const chart = await screen.findByTestId('time-series-chart');
    expect(chart.getAttribute('data-series')).toBe('2');
    // 40 rows share 20 distinct timestamps, so duplicates collapse to 10 points per device.
    expect(document.querySelector('.ts-meta')?.textContent).toContain('20 of 20 points rendered · 40 rows');
    expect(screen.getByRole('combobox', { name: 'Mode' })).toBeTruthy();
    expect(within(screen.getByRole('combobox', { name: 'Mode' })).getByRole('option', { name: 'Polling' })).toBeTruthy();
    fireEvent.click(screen.getByRole('tab', { name: 'Statistics' }));
    expect(screen.getByRole('columnheader', { name: 'Std dev' })).toBeTruthy();
    expect(screen.getAllByText(/samples/).length).toBeGreaterThan(0);
    fireEvent.click(screen.getByRole('tab', { name: 'Visualize' }));
    fireEvent.click(screen.getByRole('radio', { name: 'Histogram' }));
    expect(screen.getByRole('radio', { name: 'Histogram', checked: true })).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Series configuration' }));
    fireEvent.change(screen.getByRole('combobox', { name: 'Aggregation' }), { target: { value: 'avg' } });
    const stored = Object.keys(localStorage).find((key) => key.startsWith('endless.visualization:c1:'));
    expect(stored).toBeTruthy();
    expect(JSON.parse(localStorage.getItem(stored!)!)).toMatchObject({ chartType: 'histogram', aggregation: 'avg' });
  });

  it('polls through the plugin, shows live status, and stops when the connection closes', async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true });
    try {
      vi.mocked(sqlApi.execute).mockResolvedValue(timeSeriesResult);
      vi.mocked(sqlApi.timeSeries).mockResolvedValue({ sql: 'wrapped', result: { ...timeSeriesResult, rows: [['2026-01-01T10:00:19', 'A', 99]] } });
      mount(); await openNotebook(); await run('SELECT ts, device, value FROM m');
      fireEvent.click(screen.getByRole('tab', { name: 'Time Series' }));
      fireEvent.change(screen.getByRole('combobox', { name: 'Mode' }), { target: { value: 'polling' } });
      fireEvent.change(screen.getByRole('combobox', { name: 'Refresh' }), { target: { value: '1' } });
      fireEvent.click(screen.getByRole('button', { name: 'Start live updates' }));
      await screen.findByText('LIVE');
      await waitFor(() => expect(sqlApi.timeSeries).toHaveBeenCalled());
      expect(vi.mocked(sqlApi.timeSeries).mock.calls[0][1]).toMatchObject({ timeColumn: 'ts', since: '2026-01-01T10:00:19', sinceExclusive: false });
      expect(screen.getByText('Plugin-generated SQL (incremental)')).toBeTruthy();
      await act(async () => { await vi.advanceTimersByTimeAsync(1100); });
      expect(vi.mocked(sqlApi.timeSeries).mock.calls.length).toBeGreaterThanOrEqual(2);
      const calls = vi.mocked(sqlApi.timeSeries).mock.calls.length;
      fireEvent.click(screen.getByRole('button', { name: 'Back to connections' }));
      await screen.findByRole('button', { name: 'Open test' });
      await act(async () => { await vi.advanceTimersByTimeAsync(3000); });
      expect(vi.mocked(sqlApi.timeSeries).mock.calls.length).toBe(calls);
      await waitFor(() => expect(sqlApi.closeSession).toHaveBeenCalledWith('one'));
    } finally { vi.useRealTimers(); }
  });
});
