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

import type { SqlCell, SqlColumn, TimeSeriesAggregation } from '../models/sql';
import { toNumber } from './detect';
import { emptyDataset, type NaiveTimezone, type TimeSeries, type TimeSeriesDataset, type TimeSeriesMapping } from './model';
import { parseTimestamp } from './timestamps';

const seriesKey = (column: string, group: string | null) => group === null ? column : `${group}\u0000${column}`;
const seriesLabel = (column: string, group: string | null, single: boolean) => group === null ? column : single ? group : `${group} · ${column}`;

/** Sort by time and collapse duplicate timestamps (last sample wins); returns the duplicate count. */
export function normaliseSeries(series: TimeSeries): number {
  const order = series.times.map((_, i) => i).sort((a, b) => series.times[a] - series.times[b] || a - b);
  const times: number[] = []; const values: (number | null)[] = [];
  let duplicates = 0;
  for (const i of order) {
    if (times.length && times[times.length - 1] === series.times[i]) { values[values.length - 1] = series.values[i]; duplicates++; }
    else { times.push(series.times[i]); values.push(series.values[i]); }
  }
  series.times = times; series.values = values;
  return duplicates;
}

function finalise(dataset: TimeSeriesDataset): TimeSeriesDataset {
  let start: number | null = null; let end: number | null = null;
  for (const series of dataset.series) {
    dataset.duplicates += normaliseSeries(series);
    if (series.times.length) {
      start = start === null ? series.times[0] : Math.min(start, series.times[0]);
      end = end === null ? series.times[series.times.length - 1] : Math.max(end, series.times[series.times.length - 1]);
    }
  }
  dataset.series.sort((a, b) => a.label.localeCompare(b.label));
  dataset.start = start; dataset.end = end;
  return dataset;
}

export function buildDataset(columns: SqlColumn[], rows: SqlCell[][], mapping: TimeSeriesMapping, naive: NaiveTimezone = 'local'): TimeSeriesDataset {
  const dataset = emptyDataset();
  const index = new Map(columns.map((column, i) => [column.name, i]));
  const timeIndex = index.get(mapping.timeColumn);
  const groupIndex = mapping.groupColumn ? index.get(mapping.groupColumn) : undefined;
  const valueIndexes = mapping.valueColumns.map((name) => [name, index.get(name)] as const).filter((entry): entry is readonly [string, number] => entry[1] !== undefined);
  if (timeIndex === undefined || valueIndexes.length === 0) return dataset;
  const byKey = new Map<string, TimeSeries>();
  let lastRaw: { ms: number; raw: string } | null = null;
  const single = valueIndexes.length === 1;
  for (const row of rows) {
    dataset.sourceRows++;
    const cell = row[timeIndex];
    const time = parseTimestamp(cell, naive);
    if (time === null) { dataset.invalidTimestamps++; continue; }
    if (!lastRaw || time > lastRaw.ms) lastRaw = { ms: time, raw: typeof cell === 'number' ? String(cell) : String(cell) };
    const groupCell = groupIndex === undefined ? null : row[groupIndex];
    const group = groupIndex === undefined ? null : groupCell === null ? 'NULL' : typeof groupCell === 'object' ? '[binary]' : String(groupCell);
    for (const [column, valueIndex] of valueIndexes) {
      const key = seriesKey(column, group);
      let series = byKey.get(key);
      if (!series) { series = { key, label: seriesLabel(column, group, single), column, group, times: [], values: [] }; byKey.set(key, series); dataset.series.push(series); }
      const value = toNumber(row[valueIndex]);
      if (value === null) dataset.nullValues++;
      series.times.push(time); series.values.push(value);
    }
  }
  dataset.lastRawTime = lastRaw?.raw ?? null;
  return finalise(dataset);
}

export interface RetentionPolicy { maxPoints: number; windowMs: number | null }

/** Merge an incremental batch into the retained buffer and trim it like a ring buffer. */
export function mergeDataset(current: TimeSeriesDataset, incoming: TimeSeriesDataset, retention: RetentionPolicy): TimeSeriesDataset {
  const merged: TimeSeriesDataset = {
    ...emptyDataset(), sourceRows: current.sourceRows + incoming.sourceRows, invalidTimestamps: current.invalidTimestamps + incoming.invalidTimestamps,
    nullValues: current.nullValues + incoming.nullValues, duplicates: current.duplicates + incoming.duplicates,
    lastRawTime: incoming.lastRawTime ?? current.lastRawTime,
  };
  const byKey = new Map<string, TimeSeries>();
  for (const source of [current, incoming]) for (const series of source.series) {
    const target = byKey.get(series.key);
    if (target) { target.times = target.times.concat(series.times); target.values = target.values.concat(series.values); }
    else { const copy = { ...series, times: series.times.slice(), values: series.values.slice() }; byKey.set(series.key, copy); merged.series.push(copy); }
  }
  finalise(merged);
  const cutoff = retention.windowMs !== null && merged.end !== null ? merged.end - retention.windowMs : null;
  for (const series of merged.series) {
    let from = 0;
    if (cutoff !== null) while (from < series.times.length && series.times[from] < cutoff) from++;
    from = Math.max(from, series.times.length - retention.maxPoints);
    if (from > 0) { series.times = series.times.slice(from); series.values = series.values.slice(from); }
  }
  merged.series = merged.series.filter((series) => series.times.length > 0);
  return finalise({ ...merged, duplicates: merged.duplicates });
}

export function sliceDataset(dataset: TimeSeriesDataset, from: number | null, to: number | null): TimeSeriesDataset {
  if (from === null && to === null) return dataset;
  const series = dataset.series.map((item) => {
    let lo = 0; let hi = item.times.length;
    if (from !== null) while (lo < hi && item.times[lo] < from) lo++;
    if (to !== null) while (hi > lo && item.times[hi - 1] > to) hi--;
    return { ...item, times: item.times.slice(lo, hi), values: item.values.slice(lo, hi) };
  }).filter((item) => item.times.length > 0);
  let start: number | null = null; let end: number | null = null;
  for (const item of series) { start = start === null ? item.times[0] : Math.min(start, item.times[0]); end = Math.max(end ?? -Infinity, item.times[item.times.length - 1]); }
  return { ...dataset, series, start, end };
}

/** Client-side time bucketing for plugins without server-side aggregation or for raw buffers. */
export function aggregateDataset(dataset: TimeSeriesDataset, intervalMs: number, aggregation: TimeSeriesAggregation): TimeSeriesDataset {
  if (intervalMs <= 0) return dataset;
  const series = dataset.series.map((item) => {
    const times: number[] = []; const values: (number | null)[] = [];
    let bucket = NaN; let sum = 0; let count = 0; let min = Infinity; let max = -Infinity;
    const flush = () => {
      if (Number.isNaN(bucket)) return;
      times.push(bucket);
      values.push(count === 0 ? null : aggregation === 'count' ? count : aggregation === 'sum' ? sum : aggregation === 'min' ? min : aggregation === 'max' ? max : sum / count);
    };
    for (let i = 0; i < item.times.length; i++) {
      const start = Math.floor(item.times[i] / intervalMs) * intervalMs;
      if (start !== bucket) { flush(); bucket = start; sum = 0; count = 0; min = Infinity; max = -Infinity; }
      const value = item.values[i];
      if (value !== null) { sum += value; count++; min = Math.min(min, value); max = Math.max(max, value); }
    }
    flush();
    return { ...item, times, values };
  });
  return { ...dataset, series };
}

export const totalPoints = (dataset: TimeSeriesDataset): number => dataset.series.reduce((sum, series) => sum + series.times.length, 0);
