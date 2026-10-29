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

import type { DownsampleMethod, TimeSeries, TimeSeriesDataset } from './model';

/**
 * Min/max bucket downsampling: each bucket keeps its extreme values (plus the first point when
 * both extremes coincide), so spikes and outliers survive reduction.
 */
export function minMaxDownsample(times: number[], values: (number | null)[], maxPoints: number): { times: number[]; values: (number | null)[] } {
  const n = times.length;
  if (n <= maxPoints || maxPoints < 4) return { times, values };
  const buckets = Math.max(1, Math.floor(maxPoints / 2));
  const span = n / buckets;
  const keep: number[] = [];
  for (let b = 0; b < buckets; b++) {
    const start = Math.floor(b * span); const end = Math.min(n, Math.floor((b + 1) * span));
    let minIndex = -1; let maxIndex = -1; let firstNull = -1;
    for (let i = start; i < end; i++) {
      const value = values[i];
      if (value === null) { if (firstNull < 0) firstNull = i; continue; }
      if (minIndex < 0 || value < (values[minIndex] as number)) minIndex = i;
      if (maxIndex < 0 || value > (values[maxIndex] as number)) maxIndex = i;
    }
    if (minIndex < 0) { if (firstNull >= 0) keep.push(firstNull); continue; }
    if (minIndex === maxIndex) keep.push(minIndex);
    else keep.push(Math.min(minIndex, maxIndex), Math.max(minIndex, maxIndex));
  }
  return { times: keep.map((i) => times[i]), values: keep.map((i) => values[i]) };
}

/** Largest-Triangle-Three-Buckets over non-null samples; null gaps are dropped. */
export function lttbDownsample(times: number[], values: (number | null)[], maxPoints: number): { times: number[]; values: (number | null)[] } {
  const indices: number[] = [];
  for (let i = 0; i < values.length; i++) if (values[i] !== null) indices.push(i);
  const n = indices.length;
  if (n <= maxPoints || maxPoints < 3) return { times: indices.map((i) => times[i]), values: indices.map((i) => values[i]) };
  const sampled = [indices[0]];
  const every = (n - 2) / (maxPoints - 2);
  let a = 0;
  for (let i = 0; i < maxPoints - 2; i++) {
    const rangeStart = Math.floor((i + 1) * every) + 1; const rangeEnd = Math.min(Math.floor((i + 2) * every) + 1, n);
    let avgX = 0; let avgY = 0;
    for (let j = rangeStart; j < rangeEnd; j++) { avgX += times[indices[j]]; avgY += values[indices[j]] as number; }
    const count = Math.max(1, rangeEnd - rangeStart); avgX /= count; avgY /= count;
    const bucketStart = Math.floor(i * every) + 1; const bucketEnd = Math.min(Math.floor((i + 1) * every) + 1, n);
    const ax = times[indices[a]]; const ay = values[indices[a]] as number;
    let best = -1; let bestArea = -1;
    for (let j = bucketStart; j < bucketEnd; j++) {
      const area = Math.abs((ax - avgX) * ((values[indices[j]] as number) - ay) - (ax - times[indices[j]]) * (avgY - ay));
      if (area > bestArea) { bestArea = area; best = j; }
    }
    sampled.push(indices[best < 0 ? bucketStart : best]); a = best < 0 ? bucketStart : best;
  }
  sampled.push(indices[n - 1]);
  return { times: sampled.map((i) => times[i]), values: sampled.map((i) => values[i]) };
}

export function downsampleSeries(series: TimeSeries, maxPoints: number, method: DownsampleMethod): TimeSeries {
  const reduced = method === 'lttb' ? lttbDownsample(series.times, series.values, maxPoints) : minMaxDownsample(series.times, series.values, maxPoints);
  return reduced.times === series.times ? series : { ...series, ...reduced };
}

/** Share the point budget across visible series so the total rendered count stays bounded. */
export function downsampleDataset(dataset: TimeSeriesDataset, maxPoints: number, method: DownsampleMethod): TimeSeriesDataset {
  if (dataset.series.length === 0) return dataset;
  const perSeries = Math.max(16, Math.floor(maxPoints / dataset.series.length));
  return { ...dataset, series: dataset.series.map((series) => downsampleSeries(series, perSeries, method)) };
}
