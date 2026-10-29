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

import { act, cleanup, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { sqlApi } from '../src/api/sqlApi';
import type { SqlCell, SqlColumn, SqlExecutionResponse, VisualizationCapabilities } from '../src/models/sql';
import { defaultVisualizationConfig, type VisualizationConfig } from '../src/timeseries/model';
import { useLiveQuery } from '../src/timeseries/useLiveQuery';

vi.mock('../src/api/sqlApi', () => ({ sqlApi: { execute: vi.fn(), timeSeries: vi.fn(), cancel: vi.fn() } }));

const columns: SqlColumn[] = [{ name: 'ts', kind: 'timestamp' }, { name: 'v', kind: 'number' }];
const rows: SqlCell[][] = [['2026-01-01T10:00:00Z', 1], ['2026-01-01T10:00:01Z', 2]];
const mapping = { timeColumn: 'ts', valueColumns: ['v'], groupColumn: null };
const polling: VisualizationCapabilities = { timeSeries: true, supportedLiveModes: ['polling'], serverSideAggregation: true, timeBucketing: true, incrementalQueries: true, latestValueQueries: false, queryCancellation: true };
const resultSet = (data: SqlCell[][]): SqlExecutionResponse => ({ type: 'resultset', columns, rows: data, returnedRows: data.length, truncated: false, executionTimeMs: 1, warningCount: 0 });
const config = (over: Partial<VisualizationConfig>): VisualizationConfig => ({ ...defaultVisualizationConfig, liveMode: 'polling', refreshSeconds: 1, ...over });
const values = (result: ReturnType<typeof useLiveQuery>) => result.state.dataset.series[0]?.values ?? [];

beforeEach(() => { vi.useFakeTimers(); vi.clearAllMocks(); });
afterEach(() => { cleanup(); vi.useRealTimers(); });

describe('live query controller', () => {
  it('builds the initial dataset and only offers live modes for read-only statements', () => {
    const { result, rerender } = renderHook((props: { sql: string }) => useLiveQuery({ sessionId: 's', sql: props.sql, columns, rows, mapping, config: config({}), capabilities: polling }), { initialProps: { sql: 'SELECT * FROM m' } });
    expect(values(result.current)).toEqual([1, 2]);
    expect(result.current.availableModes).toEqual(['manual', 'polling']);
    rerender({ sql: 'UPDATE m SET v = 1' });
    expect(result.current.availableModes).toEqual(['manual']);
    expect(result.current.canRefresh).toBe(false);
  });

  it('polls incrementally without overlapping requests, keeps data on errors, and auto-pauses after repeated failures', async () => {
    let resolveFirst!: (value: { sql: string; result: SqlExecutionResponse }) => void;
    vi.mocked(sqlApi.timeSeries).mockImplementationOnce(() => new Promise((resolve) => { resolveFirst = resolve; }));
    const { result } = renderHook(() => useLiveQuery({ sessionId: 's', sql: 'SELECT * FROM m', columns, rows, mapping, config: config({ incremental: true }), capabilities: polling }));
    act(() => result.current.start());
    expect(result.current.state.status).toBe('running');
    expect(sqlApi.timeSeries).toHaveBeenCalledWith('s', { sql: 'SELECT * FROM m', timeColumn: 'ts', since: '2026-01-01T10:00:01Z', sinceExclusive: false }, expect.any(AbortSignal));
    await act(async () => { await result.current.refresh(); });
    expect(result.current.state.skipped).toBe(1);
    expect(sqlApi.timeSeries).toHaveBeenCalledTimes(1);
    await act(async () => { resolveFirst({ sql: 'wrapped', result: resultSet([['2026-01-01T10:00:01Z', 2], ['2026-01-01T10:00:02Z', 3]]) }); });
    expect(values(result.current)).toEqual([1, 2, 3]);
    expect(result.current.state.dataset.duplicates).toBe(0);
    expect(result.current.state.fetchMode).toBe('incremental');
    expect(result.current.state.generatedSql).toBe('wrapped');
    vi.mocked(sqlApi.timeSeries).mockRejectedValue(new Error('Lost connection'));
    for (let i = 0; i < 3; i++) await act(async () => { await vi.advanceTimersByTimeAsync(1000); });
    expect(values(result.current)).toEqual([1, 2, 3]);
    expect(result.current.state.lastError).toBe('Lost connection');
    expect(result.current.state.failures).toBe(3);
    expect(result.current.state.status).toBe('error');
    const calls = vi.mocked(sqlApi.timeSeries).mock.calls.length;
    await act(async () => { await vi.advanceTimersByTimeAsync(5000); });
    expect(vi.mocked(sqlApi.timeSeries).mock.calls.length).toBe(calls);
  });

  it('uses full-window execution when incremental fetches are unavailable and pauses/resumes/stops', async () => {
    vi.mocked(sqlApi.execute).mockResolvedValue(resultSet([['2026-01-01T10:00:05Z', 9]]));
    const caps = { ...polling, incrementalQueries: false };
    const { result, unmount } = renderHook(() => useLiveQuery({ sessionId: 's', sql: 'SELECT * FROM m', columns, rows, mapping, config: config({}), capabilities: caps }));
    act(() => result.current.start());
    await act(async () => { await Promise.resolve(); });
    expect(sqlApi.execute).toHaveBeenCalledWith('s', 'SELECT * FROM m', expect.any(AbortSignal));
    expect(values(result.current)).toEqual([9]);
    expect(result.current.state.fetchMode).toBe('full');
    act(() => result.current.pause());
    expect(result.current.state.status).toBe('paused');
    await act(async () => { await vi.advanceTimersByTimeAsync(3000); });
    expect(sqlApi.execute).toHaveBeenCalledTimes(1);
    act(() => result.current.resume());
    await act(async () => { await vi.advanceTimersByTimeAsync(2100); });
    expect(sqlApi.execute).toHaveBeenCalledTimes(4);
    act(() => result.current.stop());
    expect(result.current.state.status).toBe('stopped');
    unmount();
    await act(async () => { await vi.advanceTimersByTimeAsync(5000); });
    expect(sqlApi.execute).toHaveBeenCalledTimes(4);
  });

  it('runs server-side aggregation through the plugin endpoint and reports the generated SQL', async () => {
    vi.mocked(sqlApi.timeSeries).mockResolvedValue({ sql: 'SELECT bucketed', result: resultSet([['2026-01-01T10:00:00Z', 1.5]]) });
    const { result } = renderHook(() => useLiveQuery({ sessionId: 's', sql: 'SELECT * FROM m', columns, rows, mapping, config: config({ serverSide: true, aggregation: 'avg', intervalSeconds: 60 }), capabilities: polling }));
    await act(async () => { await result.current.refresh(); });
    expect(sqlApi.timeSeries).toHaveBeenCalledWith('s', { sql: 'SELECT * FROM m', timeColumn: 'ts', valueColumns: ['v'], groupColumn: null, aggregation: 'avg', intervalSeconds: 60 }, expect.any(AbortSignal));
    expect(result.current.state.serverAggregated).toBe(true);
    expect(result.current.state.generatedSql).toBe('SELECT bucketed');
    expect(values(result.current)).toEqual([1.5]);
  });

  it('aborts in-flight requests on cancel and session change, and refuses to poll non read-only SQL', async () => {
    let signal: AbortSignal | undefined;
    vi.mocked(sqlApi.execute).mockImplementation((_s, _q, s) => { signal = s; return new Promise(() => undefined); });
    vi.mocked(sqlApi.cancel).mockResolvedValue({ cancelled: true });
    const caps = { ...polling, incrementalQueries: false };
    const { result, rerender } = renderHook((props: { sessionId: string }) => useLiveQuery({ sessionId: props.sessionId, sql: 'SELECT 1', columns, rows, mapping, config: config({}), capabilities: caps }), { initialProps: { sessionId: 's1' } });
    await act(async () => { void result.current.refresh(); });
    expect(result.current.state.busy).toBe(true);
    await act(async () => { await result.current.cancel(); });
    expect(signal?.aborted).toBe(true);
    expect(sqlApi.cancel).toHaveBeenCalledWith('s1');
    expect(result.current.state.busy).toBe(false);
    await act(async () => { void result.current.refresh(); });
    rerender({ sessionId: 's2' });
    expect(signal?.aborted).toBe(true);
    const { result: unsafe } = renderHook(() => useLiveQuery({ sessionId: 's', sql: 'DELETE FROM m', columns, rows, mapping, config: config({}), capabilities: caps }));
    act(() => unsafe.current.start());
    await act(async () => { await unsafe.current.refresh(); });
    expect(unsafe.current.state.status).toBe('idle');
    expect(sqlApi.execute).toHaveBeenCalledTimes(2);
  });

  it('reports streaming as unavailable when no transport exists even if a plugin declares it', () => {
    const streaming = { ...polling, supportedLiveModes: ['polling', 'streaming'] as VisualizationCapabilities['supportedLiveModes'] };
    const { result } = renderHook(() => useLiveQuery({ sessionId: 's', sql: 'SELECT 1', columns, rows, mapping, config: config({ liveMode: 'streaming' }), capabilities: streaming }));
    expect(result.current.availableModes).toEqual(['manual', 'polling', 'streaming']);
    act(() => result.current.start());
    expect(result.current.state.status).toBe('error');
    expect(result.current.state.lastError).toMatch(/streaming transport/i);
  });
});
