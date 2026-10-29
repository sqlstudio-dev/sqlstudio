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

import { Ban, Download, Maximize2, Minimize2, Pause, Play, RefreshCw, Settings2, Square, ZoomOut } from 'lucide-react';
import type { LiveQueryController } from '../../timeseries/useLiveQuery';
import { REFRESH_SECONDS, type TimeRange, type VisualizationConfig } from '../../timeseries/model';
import { formatTimestamp } from '../../timeseries/timestamps';
import { LoadingProgress } from '../ui/LoadingProgress';

interface LiveToolbarProps {
  live: LiveQueryController; config: VisualizationConfig; onChange: (patch: Partial<VisualizationConfig>) => void;
  renderedPoints: number; totalPoints: number; configOpen: boolean; onToggleConfig: () => void;
  fullscreen: boolean; onToggleFullscreen: () => void; onExport?: () => void; onResetZoom?: () => void; showChartTools?: boolean;
}

const RANGES: { value: TimeRange; label: string }[] = [
  { value: '5m', label: 'Last 5 min' }, { value: '15m', label: 'Last 15 min' }, { value: '1h', label: 'Last hour' }, { value: '6h', label: 'Last 6 h' },
  { value: '24h', label: 'Last 24 h' }, { value: '7d', label: 'Last 7 days' }, { value: 'all', label: 'All data' },
];
const statusLabel: Record<LiveQueryController['state']['status'], string> = { idle: 'IDLE', running: 'LIVE', paused: 'PAUSED', error: 'ERROR', stopped: 'STOPPED' };

export function LiveToolbar({ live, config, onChange, renderedPoints, totalPoints, configOpen, onToggleConfig, fullscreen, onToggleFullscreen, onExport, onResetZoom, showChartTools = true }: LiveToolbarProps) {
  const { state } = live;
  const running = state.status === 'running';
  const pollable = live.availableModes.includes('polling');
  return <div className="ts-toolbar" role="toolbar" aria-label="Time series controls">
    <label className="ts-select">Range<select value={config.timeRange} onChange={(event) => onChange({ timeRange: event.target.value as TimeRange })}>
      {RANGES.map((range) => <option key={range.value} value={range.value}>{range.label}</option>)}</select></label>
    <label className="ts-select">Mode<select value={config.liveMode} onChange={(event) => onChange({ liveMode: event.target.value as VisualizationConfig['liveMode'] })} title={pollable ? undefined : 'Only read-only SELECT statements can run live'}>
      {live.availableModes.map((mode) => <option key={mode} value={mode}>{mode === 'manual' ? 'Manual' : mode === 'polling' ? 'Polling' : 'Streaming'}</option>)}</select></label>
    {config.liveMode !== 'manual' && <label className="ts-select">Refresh<select value={config.refreshSeconds} onChange={(event) => onChange({ refreshSeconds: Number(event.target.value) })}>
      {REFRESH_SECONDS.filter((seconds) => seconds > 0).map((seconds) => <option key={seconds} value={seconds}>{seconds} s</option>)}</select></label>}
    <span className="ts-actions">
      <button type="button" className="icon-button" aria-label="Refresh now" title="Run the query again" disabled={!live.canRefresh || state.busy} onClick={() => void live.refresh()}><RefreshCw size={14} className={state.busy ? 'spin' : undefined} /></button>
      {config.liveMode !== 'manual' && (running
        ? <button type="button" className="icon-button" aria-label="Pause live updates" title="Pause" onClick={live.pause}><Pause size={14} /></button>
        : <button type="button" className="icon-button" aria-label={state.status === 'paused' || state.status === 'error' ? 'Resume live updates' : 'Start live updates'} title="Start" disabled={!live.canRefresh} onClick={state.status === 'paused' || state.status === 'error' ? live.resume : live.start}><Play size={14} /></button>)}
      {config.liveMode !== 'manual' && state.status !== 'idle' && state.status !== 'stopped' && <button type="button" className="icon-button" aria-label="Stop live updates" title="Stop" onClick={live.stop}><Square size={13} /></button>}
      {state.busy && <button type="button" className="icon-button" aria-label="Cancel running query" title="Cancel" onClick={() => void live.cancel()}><Ban size={14} /></button>}
    </span>
    <span className={`ts-status ts-status-${state.status}`} aria-live="polite">{config.liveMode === 'manual' && state.status === 'idle' ? 'MANUAL' : statusLabel[state.status]}</span>
    <span className="ts-meta">
      {state.lastUpdated ? `updated ${formatTimestamp(state.lastUpdated, config.displayTimezone)}` : 'from executed result'}
      {' · '}{renderedPoints.toLocaleString()} of {totalPoints.toLocaleString()} points rendered · {state.dataset.sourceRows.toLocaleString()} rows
      {state.truncated && <span className="warning-text"> · database result truncated</span>}
      {state.skipped > 0 && ` · ${state.skipped} overlapping polls skipped`}
    </span>
    <span className="ts-actions ts-actions-right">
      {showChartTools && onResetZoom && <button type="button" className="icon-button" aria-label="Reset zoom" title="Reset zoom · drag to zoom, Ctrl+wheel to zoom at cursor, Shift+wheel to pan, double-click to reset" onClick={onResetZoom}><ZoomOut size={14} /></button>}
      {showChartTools && onExport && <button type="button" className="icon-button" aria-label="Export chart image" title="Export PNG" onClick={onExport}><Download size={14} /></button>}
      <button type="button" className="icon-button" aria-label={fullscreen ? 'Exit full screen' : 'Full screen'} title="Full screen" onClick={onToggleFullscreen}>{fullscreen ? <Minimize2 size={14} /> : <Maximize2 size={14} />}</button>
      <button type="button" className={`icon-button${configOpen ? ' active' : ''}`} aria-label="Series configuration" aria-pressed={configOpen} title="Configure series" onClick={onToggleConfig}><Settings2 size={14} /></button>
    </span>
    {state.busy && state.dataset.sourceRows > 0 && <LoadingProgress label="Refreshing query data" />}
  </div>;
}
