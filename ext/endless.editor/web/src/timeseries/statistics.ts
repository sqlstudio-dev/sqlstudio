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

import type { TimeSeries } from './model';

export interface SeriesStatistics {
  key: string; label: string; count: number; nulls: number; latest: number | null; latestTime: number | null;
  min: number | null; max: number | null; mean: number | null; stdDev: number | null;
  first: number | null; last: number | null; samplesPerSecond: number | null;
}

export function seriesStatistics(series: TimeSeries): SeriesStatistics {
  let count = 0; let nulls = 0; let sum = 0; let sumSquares = 0; let min = Infinity; let max = -Infinity;
  let first: number | null = null; let last: number | null = null; let latestTime: number | null = null;
  for (let i = 0; i < series.values.length; i++) {
    const value = series.values[i];
    if (value === null) { nulls++; continue; }
    count++; sum += value; sumSquares += value * value;
    if (value < min) min = value;
    if (value > max) max = value;
    if (first === null) first = value;
    last = value; latestTime = series.times[i];
  }
  const mean = count ? sum / count : null;
  const variance = count > 1 && mean !== null ? Math.max(0, sumSquares / count - mean * mean) : null;
  const spanMs = series.times.length > 1 ? series.times[series.times.length - 1] - series.times[0] : 0;
  return {
    key: series.key, label: series.label, count, nulls, latest: last, latestTime, min: count ? min : null, max: count ? max : null,
    mean, stdDev: variance === null ? null : Math.sqrt(variance), first, last,
    samplesPerSecond: spanMs > 0 && series.times.length > 1 ? (series.times.length - 1) / (spanMs / 1000) : null,
  };
}

export function histogram(values: (number | null)[], bins: number): { edges: number[]; counts: number[] } {
  const present = values.filter((value): value is number => value !== null);
  if (present.length === 0 || bins < 1) return { edges: [], counts: [] };
  let min = Infinity; let max = -Infinity;
  for (const value of present) { if (value < min) min = value; if (value > max) max = value; }
  if (min === max) return { edges: [min, min + 1], counts: [present.length] };
  const width = (max - min) / bins;
  const counts = new Array<number>(bins).fill(0);
  for (const value of present) counts[Math.min(bins - 1, Math.floor((value - min) / width))]++;
  return { edges: Array.from({ length: bins + 1 }, (_, i) => min + i * width), counts };
}

export function formatNumber(value: number | null, digits = 3): string {
  if (value === null || !Number.isFinite(value)) return '—';
  if (Number.isInteger(value) && Math.abs(value) < 1e15) return value.toLocaleString();
  const magnitude = Math.abs(value);
  return magnitude >= 1e6 || (magnitude > 0 && magnitude < 1e-3) ? value.toExponential(digits) : value.toLocaleString(undefined, { maximumFractionDigits: digits });
}
