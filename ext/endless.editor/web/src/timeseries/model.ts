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

import type { TimeSeriesAggregation } from '../models/sql';

export interface TimeSeriesMapping { timeColumn: string; valueColumns: string[]; groupColumn: string | null }

export interface ColumnProfile {
  name: string; index: number; isTime: boolean; isNumeric: boolean; isGroup: boolean;
  /** 'metadata' when the driver kind decided, 'sampled' when values were inspected. */
  evidence: 'metadata' | 'sampled' | 'none'; cardinality: number;
}

export interface TimeSeriesDetection {
  candidate: boolean; columns: ColumnProfile[]; timeColumns: string[]; numericColumns: string[]; groupColumns: string[];
  suggested: TimeSeriesMapping | null;
}

/** One plotted line: timestamps in epoch milliseconds, ascending and unique. */
export interface TimeSeries {
  key: string; label: string; column: string; group: string | null; times: number[]; values: (number | null)[];
}

export interface TimeSeriesDataset {
  series: TimeSeries[]; sourceRows: number; invalidTimestamps: number; nullValues: number; duplicates: number;
  start: number | null; end: number | null;
  /** Raw cell text of the newest timestamp, echoed back to the plugin for incremental fetches. */
  lastRawTime: string | null;
}

export type NaiveTimezone = 'local' | 'utc';
export type DownsampleMethod = 'minmax' | 'lttb';
export type ChartType = 'line' | 'area' | 'bar' | 'scatter' | 'histogram';
export type LiveMode = 'manual' | 'polling' | 'streaming';
export type TimeRange = '5m' | '15m' | '1h' | '6h' | '24h' | '7d' | 'all';

export const TIME_RANGE_MS: Record<Exclude<TimeRange, 'all'>, number> = {
  '5m': 5 * 60_000, '15m': 15 * 60_000, '1h': 3_600_000, '6h': 6 * 3_600_000, '24h': 86_400_000, '7d': 7 * 86_400_000,
};
export const REFRESH_SECONDS = [0, 1, 5, 10, 30, 60] as const;
export const INTERVAL_SECONDS = [1, 10, 60, 300, 3600] as const;

export interface VisualizationConfig {
  version: 1;
  /** `null` keeps the automatically detected mapping. */
  mapping: TimeSeriesMapping | null;
  chartType: ChartType;
  aggregation: TimeSeriesAggregation | 'none';
  intervalSeconds: number;
  serverSide: boolean;
  timeRange: TimeRange;
  liveMode: LiveMode;
  refreshSeconds: number;
  incremental: boolean;
  maxPoints: number;
  maxRetainedPoints: number;
  downsample: DownsampleMethod;
  naiveTimezone: NaiveTimezone;
  displayTimezone: NaiveTimezone;
  rightAxis: string[];
  hiddenSeries: string[];
  yMin: number | null;
  yMax: number | null;
}

export const defaultVisualizationConfig: VisualizationConfig = {
  version: 1, mapping: null, chartType: 'line', aggregation: 'none', intervalSeconds: 60, serverSide: false, timeRange: 'all',
  liveMode: 'manual', refreshSeconds: 5, incremental: true, maxPoints: 4000, maxRetainedPoints: 200_000, downsample: 'minmax',
  naiveTimezone: 'local', displayTimezone: 'local', rightAxis: [], hiddenSeries: [], yMin: null, yMax: null,
};

export const emptyDataset = (): TimeSeriesDataset => ({
  series: [], sourceRows: 0, invalidTimestamps: 0, nullValues: 0, duplicates: 0, start: null, end: null, lastRawTime: null,
});
