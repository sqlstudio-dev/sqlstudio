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

import { describe, expect, it } from 'vitest';
import type { SqlCell, SqlColumn } from '../src/models/sql';
import { detectTimeSeries, isReadOnlyStatement, validateMapping } from '../src/timeseries/detect';
import { aggregateDataset, buildDataset, mergeDataset, sliceDataset, totalPoints } from '../src/timeseries/dataset';
import { downsampleDataset, lttbDownsample, minMaxDownsample } from '../src/timeseries/downsample';
import { configKey, loadVisualizationConfig, saveVisualizationConfig } from '../src/timeseries/config';
import { defaultVisualizationConfig } from '../src/timeseries/model';
import { histogram, seriesStatistics } from '../src/timeseries/statistics';
import { epochToMs, formatTimestamp, parseTimestamp } from '../src/timeseries/timestamps';

const columns: SqlColumn[] = [
  { name: 'ts', type: 'DATETIME', kind: 'timestamp' }, { name: 'device', type: 'VARCHAR', kind: 'string' },
  { name: 'voltage', type: 'DOUBLE', kind: 'number' }, { name: 'current', type: 'NEWDECIMAL', kind: 'number' },
];
const rows: SqlCell[][] = [
  ['2026-01-01T10:00:00', 'A', 230.1, '1.5'], ['2026-01-01T10:00:00', 'B', 229.0, '1.6'],
  ['2026-01-01T10:00:02', 'A', 231.0, null], ['2026-01-01T10:00:01', 'A', 230.5, '1.4'],
  ['2026-01-01T10:00:02', 'A', 232.0, '1.7'], ['not a date', 'B', 1, 1], ['2026-01-01T10:00:01', 'B', null, '1.8'],
];

describe('time-series detection', () => {
  it('uses driver metadata for timestamp, numeric, and group columns', () => {
    const detection = detectTimeSeries(columns, rows);
    expect(detection.candidate).toBe(true);
    expect(detection.suggested).toEqual({ timeColumn: 'ts', valueColumns: ['voltage', 'current'], groupColumn: 'device' });
    expect(detection.columns.find((c) => c.name === 'ts')?.evidence).toBe('metadata');
  });

  it('falls back to sampling values when metadata is missing and never trusts names alone', () => {
    const untyped: SqlColumn[] = [{ name: 'created_at' }, { name: 'reading' }, { name: 'label' }, { name: 'epoch_time' }];
    const data: SqlCell[][] = Array.from({ length: 20 }, (_, i) => [`2026-02-0${(i % 9) + 1} 12:00:0${i % 10}`, String(i * 1.5), 'x', 1_700_000_000 + i]);
    const detection = detectTimeSeries(untyped, data);
    expect(detection.timeColumns).toEqual(['created_at', 'epoch_time']);
    expect(detection.numericColumns).toEqual(['reading']);
    expect(detection.columns.find((c) => c.name === 'created_at')?.evidence).toBe('sampled');
    const named = detectTimeSeries([{ name: 'timestamp' }, { name: 'value' }], [['hello', 1], ['world', 2]]);
    expect(named.candidate).toBe(false);
    expect(detectTimeSeries(columns, []).candidate).toBe(false);
  });

  it('validates stored mappings against the current columns', () => {
    expect(validateMapping({ timeColumn: 'ts', valueColumns: ['voltage', 'missing'], groupColumn: 'gone' }, columns)).toEqual({ timeColumn: 'ts', valueColumns: ['voltage'], groupColumn: null });
    expect(validateMapping({ timeColumn: 'nope', valueColumns: ['voltage'], groupColumn: null }, columns)).toBeNull();
  });

  it('only treats read-only statements as pollable', () => {
    expect(isReadOnlyStatement('  -- c\nselect 1')).toBe(true);
    expect(isReadOnlyStatement('/* x */ WITH a AS (SELECT 1) SELECT * FROM a')).toBe(true);
    expect(isReadOnlyStatement('UPDATE t SET a = 1')).toBe(false);
    expect(isReadOnlyStatement('selection')).toBe(false);
  });
});

describe('timestamps', () => {
  it('parses naive values in the chosen zone and offset-aware values absolutely', () => {
    expect(parseTimestamp('2026-01-01T10:00:00Z')).toBe(Date.UTC(2026, 0, 1, 10));
    expect(parseTimestamp('2026-01-01 10:00:00.250+02:00')).toBe(Date.UTC(2026, 0, 1, 8, 0, 0, 250));
    expect(parseTimestamp('2026-01-01 10:00:00-0530')).toBe(Date.UTC(2026, 0, 1, 15, 30));
    expect(parseTimestamp('2026-01-01 10:00:00', 'utc')).toBe(Date.UTC(2026, 0, 1, 10));
    expect(parseTimestamp('2026-01-01 10:00:00', 'local')).toBe(new Date(2026, 0, 1, 10).getTime());
    expect(parseTimestamp('2026-03-15')).toBe(new Date(2026, 2, 15).getTime());
    expect(parseTimestamp('2026-13-01')).toBeNull();
    expect(parseTimestamp('yesterday')).toBeNull();
    expect(parseTimestamp(null)).toBeNull();
    expect(parseTimestamp({ binary: true, size: 2 })).toBeNull();
  });

  it('classifies numeric epochs by magnitude', () => {
    expect(epochToMs(1_700_000_000)).toBe(1_700_000_000_000);
    expect(epochToMs(1_700_000_000_000)).toBe(1_700_000_000_000);
    expect(epochToMs(1_700_000_000_000_000)).toBe(1_700_000_000_000);
    expect(parseTimestamp(1_700_000_000_000_000_000)).toBe(1_700_000_000_000);
    expect(formatTimestamp(Date.UTC(2026, 0, 1, 10, 5, 6, 7), 'utc', true)).toBe('2026-01-01 10:05:06.007');
  });
});

describe('dataset building', () => {
  const mapping = { timeColumn: 'ts', valueColumns: ['voltage', 'current'], groupColumn: 'device' };

  it('groups series, sorts out-of-order samples, collapses duplicates, and counts nulls and invalid rows', () => {
    const dataset = buildDataset(columns, rows, mapping, 'utc');
    expect(dataset.sourceRows).toBe(7);
    expect(dataset.invalidTimestamps).toBe(1);
    expect(dataset.nullValues).toBe(2);
    expect(dataset.series.map((s) => s.label)).toEqual(['A · current', 'A · voltage', 'B · current', 'B · voltage']);
    const voltageA = dataset.series.find((s) => s.label === 'A · voltage')!;
    expect(voltageA.times).toEqual([Date.UTC(2026, 0, 1, 10), Date.UTC(2026, 0, 1, 10, 0, 1), Date.UTC(2026, 0, 1, 10, 0, 2)]);
    expect(voltageA.values).toEqual([230.1, 230.5, 232]);
    expect(dataset.duplicates).toBe(2);
    expect(dataset.lastRawTime).toBe('2026-01-01T10:00:02');
    expect(dataset.start).toBe(Date.UTC(2026, 0, 1, 10));
    expect(dataset.end).toBe(Date.UTC(2026, 0, 1, 10, 0, 2));
  });

  it('labels single-value grouped series by group and ungrouped series by column', () => {
    expect(buildDataset(columns, rows, { ...mapping, valueColumns: ['voltage'] }, 'utc').series.map((s) => s.label)).toEqual(['A', 'B']);
    expect(buildDataset(columns, rows, { ...mapping, groupColumn: null }, 'utc').series.map((s) => s.label)).toEqual(['current', 'voltage']);
  });

  it('merges incremental batches with ring-buffer retention', () => {
    const first = buildDataset(columns, rows.slice(0, 2), mapping, 'utc');
    const second = buildDataset(columns, [['2026-01-01T10:00:00', 'A', 999, '9'], ['2026-01-01T10:00:05', 'A', 240, '2'], ['2026-01-01T10:10:00', 'A', 250, '3']], mapping, 'utc');
    const merged = mergeDataset(first, second, { maxPoints: 2, windowMs: null });
    const voltageA = merged.series.find((s) => s.label === 'A · voltage')!;
    expect(voltageA.values).toEqual([240, 250]);
    const windowed = mergeDataset(first, second, { maxPoints: 1000, windowMs: 60_000 });
    expect(windowed.series.find((s) => s.label === 'A · voltage')!.times).toEqual([Date.UTC(2026, 0, 1, 10, 10)]);
    expect(windowed.series.some((s) => s.group === 'B')).toBe(false);
  });

  it('slices ranges and aggregates buckets client-side', () => {
    const dataset = buildDataset(columns, rows, { ...mapping, valueColumns: ['voltage'], groupColumn: null }, 'utc');
    expect(totalPoints(sliceDataset(dataset, Date.UTC(2026, 0, 1, 10, 0, 1), null))).toBe(2);
    const buckets = aggregateDataset(dataset, 60_000, 'avg');
    expect(buckets.series[0].times).toEqual([Date.UTC(2026, 0, 1, 10)]);
    // Ungrouped duplicates collapse (last row wins), so 10:00:01 holds B's null and is skipped.
    expect(buckets.series[0].values[0]).toBeCloseTo((229 + 232) / 2);
    expect(aggregateDataset(dataset, 60_000, 'count').series[0].values).toEqual([2]);
    expect(aggregateDataset(dataset, 60_000, 'max').series[0].values).toEqual([232]);
  });
});

describe('downsampling and statistics', () => {
  const times = Array.from({ length: 10_000 }, (_, i) => i * 1000);
  const values = times.map((_, i) => (i === 5_000 ? 1_000 : Math.sin(i / 50)));

  it('keeps spikes with min/max and bounds output with LTTB', () => {
    const reduced = minMaxDownsample(times, values, 200);
    expect(reduced.times.length).toBeLessThanOrEqual(200);
    expect(reduced.values).toContain(1_000);
    const lttb = lttbDownsample(times, values, 200);
    expect(lttb.times.length).toBe(200);
    expect(lttb.times[0]).toBe(0);
    expect(lttb.times[199]).toBe(9_999_000);
    expect(minMaxDownsample(times.slice(0, 10), values.slice(0, 10), 200).times.length).toBe(10);
    expect(lttbDownsample([1, 2, 3], [1, null, 3], 10)).toEqual({ times: [1, 3], values: [1, 3] });
  });

  it('shares the point budget across series', () => {
    const series = ['a', 'b'].map((key) => ({ key, label: key, column: key, group: null, times, values }));
    const dataset = downsampleDataset({ series, sourceRows: 0, invalidTimestamps: 0, nullValues: 0, duplicates: 0, start: 0, end: 1, lastRawTime: null }, 1000, 'minmax');
    expect(totalPoints(dataset)).toBeLessThanOrEqual(1000);
    expect(dataset.series.every((s) => s.values.includes(1_000))).toBe(true);
  });

  it('computes statistics and histograms with nulls ignored', () => {
    const stats = seriesStatistics({ key: 'k', label: 'k', column: 'k', group: null, times: [0, 1000, 2000, 3000], values: [2, null, 4, 6] });
    expect(stats).toMatchObject({ count: 3, nulls: 1, min: 2, max: 6, mean: 4, first: 2, last: 6, latest: 6, latestTime: 3000 });
    expect(stats.stdDev).toBeCloseTo(Math.sqrt(8 / 3));
    expect(stats.samplesPerSecond).toBeCloseTo(1);
    expect(histogram([1, 2, 2, 3, null], 2)).toEqual({ edges: [1, 2, 3], counts: [1, 3] });
    expect(histogram([null], 4).counts).toEqual([]);
  });
});

describe('visualization preferences', () => {
  it('persists per connection and normalised statement', () => {
    const key = configKey('c1', 'SELECT  1\n');
    expect(key).toBe(configKey('c1', 'SELECT 1'));
    expect(key).not.toBe(configKey('c2', 'SELECT 1'));
    expect(loadVisualizationConfig(key)).toEqual(defaultVisualizationConfig);
    saveVisualizationConfig(key, { ...defaultVisualizationConfig, chartType: 'area', refreshSeconds: 10 });
    expect(loadVisualizationConfig(key)).toMatchObject({ chartType: 'area', refreshSeconds: 10, maxPoints: defaultVisualizationConfig.maxPoints });
    localStorage.setItem(key, '{broken');
    expect(loadVisualizationConfig(key)).toEqual(defaultVisualizationConfig);
  });
});
