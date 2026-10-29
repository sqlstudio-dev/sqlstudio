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

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { AreaChart, BarChart3, ChartLine, ChartScatter, CircleAlert, Table2, Sigma, BarChartHorizontal } from 'lucide-react';
import { noVisualizationCapabilities, type SqlResultSet, type VisualizationCapabilities } from '../../models/sql';
import { useSqlConsole } from '../../hooks/useSqlConsole';
import { aggregateDataset, sliceDataset, totalPoints } from '../../timeseries/dataset';
import { detectTimeSeries, validateMapping } from '../../timeseries/detect';
import { downsampleDataset } from '../../timeseries/downsample';
import { configKey, loadVisualizationConfig, saveVisualizationConfig } from '../../timeseries/config';
import { TIME_RANGE_MS, type ChartType, type TimeSeries, type VisualizationConfig } from '../../timeseries/model';
import { histogram } from '../../timeseries/statistics';
import { useLiveQuery } from '../../timeseries/useLiveQuery';
import { LiveToolbar } from './LiveToolbar';
import { SeriesConfiguration } from './SeriesConfiguration';
import { StatisticsPanel } from './StatisticsPanel';
import { TimeSeriesChart, type TimeSeriesChartHandle } from './TimeSeriesChart';

type Tab = 'grid' | 'chart' | 'stats' | 'visualize';
const CHART_TYPES: { value: ChartType; label: string; icon: typeof ChartLine }[] = [
  { value: 'line', label: 'Line', icon: ChartLine }, { value: 'area', label: 'Area', icon: AreaChart }, { value: 'bar', label: 'Bar', icon: BarChart3 },
  { value: 'scatter', label: 'Scatter', icon: ChartScatter }, { value: 'histogram', label: 'Histogram', icon: BarChartHorizontal },
];

interface ResultTabsProps { executionId: string; sql: string; result: SqlResultSet; children: React.ReactNode }

/** Query result panel: the existing grid plus time-series, statistics, and visualisation tabs. */
export function ResultTabs({ executionId, sql, result, children }: ResultTabsProps) {
  const { state } = useSqlConsole();
  const connection = state.activeConnection;
  const capabilities = useMemo<VisualizationCapabilities>(() => {
    const plugin = state.plugins.find((item) => connection && (item.id === connection.type || item.aliases.includes(connection.type)));
    return plugin?.capabilities ?? noVisualizationCapabilities;
  }, [state.plugins, connection]);
  // Sessions are bound to a connection; a different connection must not reuse this result's live loop.
  const sessionId = state.session && connection && state.session.connectionId === connection.id ? state.session.sessionId : null;
  const key = useMemo(() => configKey(connection?.id ?? 'local', sql), [connection?.id, sql]);
  const [config, setConfig] = useState<VisualizationConfig>(() => loadVisualizationConfig(key));
  const [tab, setTab] = useState<Tab>('grid');
  const [configOpen, setConfigOpen] = useState(false);
  const [fullscreen, setFullscreen] = useState(false);
  const panelRef = useRef<HTMLDivElement>(null);
  const chartRef = useRef<TimeSeriesChartHandle>(null);
  const updateConfig = useCallback((patch: Partial<VisualizationConfig>) => setConfig((current) => { const next = { ...current, ...patch }; saveVisualizationConfig(key, next); return next; }), [key]);

  const detection = useMemo(() => detectTimeSeries(result.columns, result.rows), [result]);
  const mapping = useMemo(() => validateMapping(config.mapping, result.columns) ?? detection.suggested, [config.mapping, result.columns, detection]);
  const live = useLiveQuery({ sessionId, sql, columns: result.columns, rows: result.rows, mapping, config, capabilities });

  const windowed = useMemo(() => {
    const dataset = live.state.dataset;
    if (config.timeRange === 'all' || dataset.end === null) return dataset;
    return sliceDataset(dataset, dataset.end - TIME_RANGE_MS[config.timeRange], null);
  }, [live.state.dataset, config.timeRange]);
  const aggregated = useMemo(() => config.aggregation !== 'none' && !live.state.serverAggregated ? aggregateDataset(windowed, config.intervalSeconds * 1000, config.aggregation) : windowed, [windowed, config.aggregation, config.intervalSeconds, live.state.serverAggregated]);
  const rendered = useMemo(() => downsampleDataset(aggregated, config.maxPoints, config.downsample), [aggregated, config.maxPoints, config.downsample]);
  const histogramSeries = useMemo<TimeSeries[]>(() => config.chartType !== 'histogram' ? [] : aggregated.series.map((series) => {
    const { edges, counts } = histogram(series.values, 30);
    return { ...series, times: edges.slice(0, counts.length), values: counts };
  }), [aggregated, config.chartType]);

  useEffect(() => {
    const onChange = () => setFullscreen(document.fullscreenElement === panelRef.current);
    document.addEventListener('fullscreenchange', onChange);
    return () => document.removeEventListener('fullscreenchange', onChange);
  }, []);
  const toggleFullscreen = () => {
    if (document.fullscreenElement) void document.exitFullscreen?.();
    else void panelRef.current?.requestFullscreen?.();
  };
  const exportPng = () => {
    const url = chartRef.current?.exportPng();
    if (!url) return;
    const link = document.createElement('a'); link.href = url; link.download = `time-series-${executionId.slice(0, 8)}.png`; link.click();
  };
  const toggleSeries = useCallback((seriesKey: string, visible: boolean) => updateConfig({ hiddenSeries: visible ? config.hiddenSeries.filter((item) => item !== seriesKey) : [...config.hiddenSeries, seriesKey] }), [config.hiddenSeries, updateConfig]);

  const visualTabs = Boolean(mapping);
  const chartHeight = fullscreen ? Math.max(320, window.innerHeight - 200) : 320;
  const chart = (type: ChartType) => <TimeSeriesChart ref={chartRef} series={type === 'histogram' ? histogramSeries : rendered.series} chartType={type} rightAxis={config.rightAxis} hiddenSeries={config.hiddenSeries}
    timezone={config.displayTimezone} timeAxis={type !== 'histogram'} xLabel={type === 'histogram' ? 'bin' : mapping?.timeColumn} yMin={config.yMin} yMax={config.yMax} height={chartHeight} onToggleSeries={toggleSeries} syncKey={executionId} />;
  const configure = <SeriesConfiguration columns={result.columns} detection={detection} mapping={mapping} config={config} capabilities={capabilities} onChange={updateConfig} />;
  const needsMapping = <div className="ts-empty"><CircleAlert size={15} /><div><strong>No time-series columns detected</strong><span>Pick a timestamp column and at least one numeric column below to plot this result.</span></div></div>;

  return <div className={`result-tabs${fullscreen ? ' fullscreen' : ''}`} ref={panelRef}>
    <div className="tab-strip" role="tablist" aria-label="Query result views">
      {([['grid', 'Data Grid', Table2], ['chart', 'Time Series', ChartLine], ['stats', 'Statistics', Sigma], ['visualize', 'Visualize', BarChart3]] as const).map(([id, label, Icon]) =>
        <button key={id} type="button" role="tab" aria-selected={tab === id} className={`tab-button${tab === id ? ' active' : ''}${id !== 'grid' && !visualTabs ? ' muted' : ''}`} onClick={() => setTab(id)} title={id !== 'grid' && !visualTabs ? 'Select columns manually to enable' : undefined}>
          <Icon size={13} /> {label}{id === 'chart' && live.state.status === 'running' && <i className="live-dot" aria-label="live" />}
        </button>)}
    </div>
    {tab === 'grid' && children}
    {tab !== 'grid' && <section className="ts-panel" aria-label={tab === 'chart' ? 'Time series' : tab === 'stats' ? 'Statistics' : 'Visualization'}>
      <LiveToolbar live={live} config={config} onChange={updateConfig} renderedPoints={totalPoints(tab === 'visualize' && config.chartType === 'histogram' ? { ...rendered, series: histogramSeries } : rendered)} totalPoints={totalPoints(aggregated)}
        configOpen={configOpen || !mapping} onToggleConfig={() => setConfigOpen((open) => !open)} fullscreen={fullscreen} onToggleFullscreen={toggleFullscreen}
        onExport={exportPng} onResetZoom={() => chartRef.current?.resetZoom()} showChartTools={tab !== 'stats'} />
      {live.state.lastError && <div className="ts-error" role="alert"><CircleAlert size={14} /> {live.state.lastError}{live.state.status === 'error' && ' — live updates paused; press play to retry.'}</div>}
      {!mapping ? needsMapping : tab === 'chart' ? chart(config.chartType === 'histogram' ? 'line' : config.chartType)
        : tab === 'stats' ? <StatisticsPanel dataset={aggregated} timezone={config.displayTimezone} status={live.state.status} lastUpdated={live.state.lastUpdated} hiddenSeries={config.hiddenSeries} />
        : <>
          <div className="chart-type-strip" role="radiogroup" aria-label="Chart type">
            {CHART_TYPES.map(({ value, label, icon: Icon }) => <button key={value} type="button" role="radio" aria-checked={config.chartType === value} className={`chip-button${config.chartType === value ? ' active' : ''}`} onClick={() => updateConfig({ chartType: value })}><Icon size={13} /> {label}</button>)}
          </div>
          {chart(config.chartType)}
        </>}
      {mapping && rendered.series.length === 0 && tab !== 'stats' && <div className="empty-result">No samples in the selected range.</div>}
      {live.state.generatedSql && <details className="ts-generated"><summary>Plugin-generated SQL ({live.state.fetchMode})</summary><pre>{live.state.generatedSql}</pre></details>}
      {(configOpen || !mapping) && configure}
    </section>}
  </div>;
}
