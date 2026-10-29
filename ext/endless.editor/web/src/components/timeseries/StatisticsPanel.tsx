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

import { useMemo } from 'react';
import type { NaiveTimezone, TimeSeriesDataset } from '../../timeseries/model';
import { formatNumber, seriesStatistics } from '../../timeseries/statistics';
import { formatDuration, formatTimestamp } from '../../timeseries/timestamps';
import type { LiveStatus } from '../../timeseries/useLiveQuery';
import { SERIES_COLORS } from './TimeSeriesChart';

interface StatisticsPanelProps { dataset: TimeSeriesDataset; timezone: NaiveTimezone; status: LiveStatus; lastUpdated: number | null; hiddenSeries: string[] }

/** Per-series statistics for the selected range plus KPI cards with the latest measurement. */
export function StatisticsPanel({ dataset, timezone, status, lastUpdated, hiddenSeries }: StatisticsPanelProps) {
  const stats = useMemo(() => dataset.series.map((series, index) => ({ ...seriesStatistics(series), color: SERIES_COLORS[index % SERIES_COLORS.length], hidden: hiddenSeries.includes(series.key) })), [dataset, hiddenSeries]);
  const samples = stats.reduce((sum, item) => sum + item.count, 0);
  if (stats.length === 0) return <div className="empty-result">No samples in the selected range.</div>;
  return <div className="ts-statistics">
    <div className="kpi-grid" aria-label="Latest values">
      {stats.filter((item) => !item.hidden).map((item) => <article key={item.key} className="kpi-card" style={{ borderLeftColor: item.color }}>
        <span className="kpi-label">{item.label}</span>
        <strong className="kpi-value">{formatNumber(item.latest)}</strong>
        <span className="kpi-meta">{item.latestTime !== null ? formatTimestamp(item.latestTime, timezone) : 'no sample'} · {item.count.toLocaleString()} samples</span>
      </article>)}
      <article className="kpi-card kpi-summary">
        <span className="kpi-label">Range</span>
        <strong className="kpi-value">{samples.toLocaleString()} <small>samples</small></strong>
        <span className="kpi-meta">{dataset.start !== null && dataset.end !== null ? `${formatTimestamp(dataset.start, timezone)} → ${formatTimestamp(dataset.end, timezone)} (${formatDuration(dataset.end - dataset.start)})` : '—'}</span>
        <span className="kpi-meta">Status <b className={`ts-status ts-status-${status}`}>{status.toUpperCase()}</b>{lastUpdated ? ` · updated ${formatTimestamp(lastUpdated, timezone)}` : ''}</span>
      </article>
    </div>
    <div className="result-grid-wrap"><table className="result-grid ts-stats-table"><thead><tr>
      <th>Series</th><th>Latest</th><th>Min</th><th>Max</th><th>Average</th><th>Std dev</th><th>Samples</th><th>Nulls</th><th>First</th><th>Last</th><th>First timestamp</th><th>Last timestamp</th><th>Rate</th>
    </tr></thead><tbody>
      {stats.map((item) => <tr key={item.key} className={item.hidden ? 'ts-hidden-row' : undefined}>
        <td><i className="ts-swatch" style={{ background: item.color }} /> {item.label}</td>
        <td className="cell-number">{formatNumber(item.latest)}</td><td className="cell-number">{formatNumber(item.min)}</td><td className="cell-number">{formatNumber(item.max)}</td>
        <td className="cell-number">{formatNumber(item.mean)}</td><td className="cell-number">{formatNumber(item.stdDev)}</td>
        <td className="cell-number">{item.count.toLocaleString()}</td><td className="cell-number">{item.nulls.toLocaleString()}</td>
        <td className="cell-number">{formatNumber(item.first)}</td><td className="cell-number">{formatNumber(item.last)}</td>
        <td>{seriesStart(dataset, item.key, timezone)}</td><td>{item.latestTime !== null ? formatTimestamp(item.latestTime, timezone) : '—'}</td>
        <td className="cell-number">{item.samplesPerSecond === null ? '—' : item.samplesPerSecond >= 1 ? `${formatNumber(item.samplesPerSecond, 2)}/s` : `${formatNumber(item.samplesPerSecond * 60, 2)}/min`}</td>
      </tr>)}
    </tbody></table></div>
    <p className="ts-footnote">{dataset.invalidTimestamps > 0 && `${dataset.invalidTimestamps} rows had unparseable timestamps. `}{dataset.duplicates > 0 && `${dataset.duplicates} duplicate samples collapsed (last value wins). `}{dataset.nullValues > 0 && `${dataset.nullValues} null measurements excluded from statistics.`}</p>
  </div>;
}

const seriesStart = (dataset: TimeSeriesDataset, key: string, timezone: NaiveTimezone) => {
  const series = dataset.series.find((item) => item.key === key);
  return series && series.times.length ? formatTimestamp(series.times[0], timezone) : '—';
};
