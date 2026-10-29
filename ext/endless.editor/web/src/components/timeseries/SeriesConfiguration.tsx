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

import type { SqlColumn, VisualizationCapabilities } from '../../models/sql';
import type { TimeSeriesDetection, TimeSeriesMapping, VisualizationConfig } from '../../timeseries/model';
import { INTERVAL_SECONDS } from '../../timeseries/model';

interface SeriesConfigurationProps {
  columns: SqlColumn[]; detection: TimeSeriesDetection; mapping: TimeSeriesMapping | null; config: VisualizationConfig;
  capabilities: VisualizationCapabilities; onChange: (patch: Partial<VisualizationConfig>) => void;
}

const intervalLabel = (seconds: number) => seconds < 60 ? `${seconds} s` : seconds < 3600 ? `${seconds / 60} min` : `${seconds / 3600} h`;

/** Column mapping and rendering options; every change is persisted by the owner. */
export function SeriesConfiguration({ columns, detection, mapping, config, capabilities, onChange }: SeriesConfigurationProps) {
  const current: TimeSeriesMapping = mapping ?? { timeColumn: detection.timeColumns[0] ?? columns[0]?.name ?? '', valueColumns: [], groupColumn: null };
  const setMapping = (patch: Partial<TimeSeriesMapping>) => onChange({ mapping: { ...current, ...patch } });
  const toggleValue = (name: string, checked: boolean) => setMapping({ valueColumns: checked ? [...current.valueColumns, name] : current.valueColumns.filter((item) => item !== name) });
  const toggleRight = (name: string, checked: boolean) => onChange({ rightAxis: checked ? [...config.rightAxis, name] : config.rightAxis.filter((item) => item !== name) });
  const byName = new Map(detection.columns.map((profile) => [profile.name, profile]));
  const describe = (name: string) => { const p = byName.get(name); return p?.isTime ? 'time' : p?.isNumeric ? 'number' : p?.isGroup ? 'group' : ''; };

  return <div className="ts-config" aria-label="Series configuration">
    <h3>Series configuration</h3>
    <div className="ts-config-grid">
      <label>X axis
        <select value={current.timeColumn} onChange={(event) => setMapping({ timeColumn: event.target.value, valueColumns: current.valueColumns.filter((item) => item !== event.target.value) })}>
          {columns.map((column) => <option key={column.name} value={column.name}>{column.name}{describe(column.name) ? ` · ${describe(column.name)}` : ''}</option>)}
        </select>
      </label>
      <label>Group by
        <select value={current.groupColumn ?? ''} onChange={(event) => setMapping({ groupColumn: event.target.value || null, valueColumns: current.valueColumns.filter((item) => item !== event.target.value) })}>
          <option value="">None</option>
          {columns.filter((column) => column.name !== current.timeColumn).map((column) => <option key={column.name} value={column.name}>{column.name}{describe(column.name) ? ` · ${describe(column.name)}` : ''}</option>)}
        </select>
      </label>
      <label>Aggregation
        <select value={config.aggregation} onChange={(event) => onChange({ aggregation: event.target.value as VisualizationConfig['aggregation'] })}>
          <option value="none">Raw samples</option><option value="avg">Average</option><option value="sum">Sum</option>
          <option value="min">Minimum</option><option value="max">Maximum</option><option value="count">Count</option>
        </select>
      </label>
      <label>Interval
        <select value={config.intervalSeconds} disabled={config.aggregation === 'none'} onChange={(event) => onChange({ intervalSeconds: Number(event.target.value) })}>
          {INTERVAL_SECONDS.map((seconds) => <option key={seconds} value={seconds}>{intervalLabel(seconds)}</option>)}
        </select>
      </label>
    </div>
    <fieldset className="ts-config-values">
      <legend>Y axis</legend>
      <div className="ts-value-list">
        {columns.filter((column) => column.name !== current.timeColumn && column.name !== current.groupColumn).map((column) => {
          const selected = current.valueColumns.includes(column.name);
          return <div key={column.name} className={`ts-value-item${selected ? ' selected' : ''}`}>
            <label><input type="checkbox" checked={selected} onChange={(event) => toggleValue(column.name, event.target.checked)} /> {column.name}<small>{describe(column.name) || column.type || ''}</small></label>
            {selected && <label className="ts-right-toggle" title="Plot on the right-hand axis"><input type="checkbox" checked={config.rightAxis.includes(column.name)} onChange={(event) => toggleRight(column.name, event.target.checked)} /> right axis</label>}
          </div>;
        })}
      </div>
    </fieldset>
    <div className="ts-config-grid">
      <label>Server-side aggregation
        <select value={config.serverSide ? 'server' : 'client'} disabled={!capabilities.serverSideAggregation || config.aggregation === 'none'} onChange={(event) => onChange({ serverSide: event.target.value === 'server' })}>
          <option value="client">In browser</option><option value="server">{capabilities.serverSideAggregation ? 'Database (query wrapped)' : 'Not supported by plugin'}</option>
        </select>
      </label>
      <label>Incremental polling
        <select value={config.incremental ? 'on' : 'off'} disabled={!capabilities.incrementalQueries} onChange={(event) => onChange({ incremental: event.target.value === 'on' })}>
          <option value="on">{capabilities.incrementalQueries ? 'Fetch new rows only' : 'Not supported by plugin'}</option><option value="off">Replace full window</option>
        </select>
      </label>
      <label>Max rendered points<input type="number" min={100} max={200_000} step={100} value={config.maxPoints} onChange={(event) => onChange({ maxPoints: Math.max(100, Number(event.target.value) || 100) })} /></label>
      <label>Downsampling
        <select value={config.downsample} onChange={(event) => onChange({ downsample: event.target.value as VisualizationConfig['downsample'] })}>
          <option value="minmax">Min/max (keeps spikes)</option><option value="lttb">LTTB (shape)</option>
        </select>
      </label>
      <label>Retained samples<input type="number" min={1000} max={5_000_000} step={1000} value={config.maxRetainedPoints} onChange={(event) => onChange({ maxRetainedPoints: Math.max(1000, Number(event.target.value) || 1000) })} /></label>
      <label>Naive timestamps are
        <select value={config.naiveTimezone} onChange={(event) => onChange({ naiveTimezone: event.target.value as VisualizationConfig['naiveTimezone'] })}>
          <option value="local">Local time</option><option value="utc">UTC</option>
        </select>
      </label>
      <label>Display time zone
        <select value={config.displayTimezone} onChange={(event) => onChange({ displayTimezone: event.target.value as VisualizationConfig['displayTimezone'] })}>
          <option value="local">Local time</option><option value="utc">UTC</option>
        </select>
      </label>
      <label>Y axis range<span className="ts-range-inputs">
        <input type="number" placeholder="auto min" value={config.yMin ?? ''} onChange={(event) => onChange({ yMin: event.target.value === '' ? null : Number(event.target.value) })} />
        <input type="number" placeholder="auto max" value={config.yMax ?? ''} onChange={(event) => onChange({ yMax: event.target.value === '' ? null : Number(event.target.value) })} />
      </span></label>
    </div>
    {config.mapping && <button type="button" className="secondary-button" onClick={() => onChange({ mapping: null })}>Use detected columns</button>}
  </div>;
}
