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

import { useEffect, useImperativeHandle, useMemo, useRef, type Ref } from 'react';
import uPlot, { type AlignedData, type Options, type Series } from 'uplot';
import 'uplot/dist/uPlot.min.css';
import type { ChartType, NaiveTimezone, TimeSeries } from '../../timeseries/model';
import { formatNumber } from '../../timeseries/statistics';
import { formatTimestamp } from '../../timeseries/timestamps';

export const SERIES_COLORS = ['#4fc1ff', '#ce9178', '#b5cea8', '#c586c0', '#dcdcaa', '#9cdcfe', '#f48771', '#6a9955', '#d7ba7d', '#569cd6'];

export interface TimeSeriesChartHandle { exportPng: () => string | null; resetZoom: () => void }
export interface TimeSeriesChartProps {
  ref?: Ref<TimeSeriesChartHandle>;
  series: TimeSeries[]; chartType: ChartType; rightAxis: string[]; hiddenSeries: string[]; timezone: NaiveTimezone;
  /** `true` for time on X; histograms and numeric scatter pass `false`. */
  timeAxis?: boolean; xLabel?: string; yMin: number | null; yMax: number | null; height?: number;
  onToggleSeries?: (key: string, visible: boolean) => void; syncKey?: string;
}

const cssVar = (name: string, fallback: string) => {
  if (typeof getComputedStyle === 'undefined') return fallback;
  const value = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  return value || fallback;
};

/**
 * Ctrl/Cmd+wheel zooms the X axis around the cursor and Shift+wheel pans; a plain wheel keeps
 * scrolling the notebook unless the panel is full screen. Double-click resets (uPlot built-in).
 */
function wheelZoomPlugin(): uPlot.Plugin {
  return { hooks: { ready: (u) => {
    const over = u.over;
    over.addEventListener('wheel', (event) => {
      const fullscreen = typeof document !== 'undefined' && document.fullscreenElement !== null && document.fullscreenElement.contains(over);
      if (!event.ctrlKey && !event.metaKey && !event.shiftKey && !fullscreen) return;
      event.preventDefault();
      const { left } = u.cursor;
      if (left === undefined || left < 0) return;
      const xs = u.data[0];
      const dataMin = xs[0]; const dataMax = xs[xs.length - 1];
      if (dataMin === undefined || dataMax === undefined) return;
      const xMin = u.scales.x.min!; const xMax = u.scales.x.max!; const range = xMax - xMin;
      if (event.shiftKey) {
        const shift = Math.max(-(xMin - dataMin), Math.min(dataMax - xMax, range * (event.deltaY > 0 ? 0.15 : -0.15)));
        if (shift !== 0) u.setScale('x', { min: Math.round(xMin + shift), max: Math.round(xMax + shift) });
        return;
      }
      const factor = event.deltaY < 0 ? 0.8 : 1.25;
      const cursorX = u.posToVal(left, 'x');
      const ratio = (cursorX - xMin) / range;
      const nextRange = Math.max(range * factor, 10);
      const min = Math.max(dataMin, Math.round(cursorX - nextRange * ratio));
      const max = Math.min(dataMax, Math.round(cursorX + nextRange * (1 - ratio)));
      if (min <= dataMin && max >= dataMax) u.setData(u.data, true);
      else u.setScale('x', { min, max });
    }, { passive: false });
  } } };
}

export function alignSeries(series: TimeSeries[]): AlignedData {
  if (series.length === 0) return [[]];
  const joined = uPlot.join(series.map((item) => [item.times, item.values] as AlignedData));
  return joined as AlignedData;
}

export function TimeSeriesChart({ ref, series, chartType, rightAxis, hiddenSeries, timezone, timeAxis = true, xLabel, yMin, yMax, height = 320, onToggleSeries, syncKey }: TimeSeriesChartProps) {
  const container = useRef<HTMLDivElement>(null);
  const plot = useRef<uPlot | null>(null);
  const data = useMemo(() => alignSeries(series), [series]);
  const hiddenKey = hiddenSeries.join('|'); const rightKey = rightAxis.join('|');
  const structure = useMemo(() => series.map((item) => item.key).join('|') + '::' + chartType + '::' + rightKey + '::' + timeAxis + '::' + timezone, [series, chartType, rightKey, timeAxis, timezone]);

  useImperativeHandle(ref, () => ({
    exportPng: () => plot.current ? plot.current.ctx.canvas.toDataURL('image/png') : null,
    resetZoom: () => { const u = plot.current; if (u) u.setData(u.data, true); },
  }), []);

  // Rebuild only when the series structure changes; data refreshes keep the instance and its zoom.
  useEffect(() => {
    const element = container.current;
    if (!element) return;
    plot.current?.destroy(); plot.current = null;
    if (series.length === 0) return;
    const fg = cssVar('--chart-fg', '#9d9d9d'); const grid = cssVar('--chart-grid', '#333337'); const text = cssVar('--chart-text', '#d4d4d4');
    const anyRight = series.some((item) => rightAxis.includes(item.column));
    const seriesOptions: Series[] = [
      { label: xLabel ?? (timeAxis ? 'time' : 'value'), value: (_u, raw) => raw === null || raw === undefined ? '—' : timeAxis ? formatTimestamp(raw, timezone, true) : formatNumber(raw) },
      ...series.map((item, index): Series => {
        const color = SERIES_COLORS[index % SERIES_COLORS.length];
        const base: Series = { label: item.label, stroke: color, width: 1.5, scale: rightAxis.includes(item.column) ? 'y2' : 'y', show: !hiddenSeries.includes(item.key),
          spanGaps: false, value: (_u, raw) => formatNumber(raw as number | null) };
        if (chartType === 'area') return { ...base, fill: color + '33' };
        if (chartType === 'bar') return { ...base, paths: uPlot.paths.bars!({ size: [0.7, 60] }), fill: color + 'aa', points: { show: false } };
        if (chartType === 'scatter') return { ...base, paths: () => null, points: { show: true, size: 5, fill: color } };
        if (chartType === 'histogram') return { ...base, paths: uPlot.paths.bars!({ size: [0.95, 200], align: 1 }), fill: color + 'aa', points: { show: false } };
        return base;
      }),
    ];
    const options: Options = {
      width: Math.max(320, element.clientWidth), height, padding: [10, anyRight ? 8 : 16, 0, 8],
      cursor: { drag: { x: true, y: false, setScale: true }, sync: syncKey ? { key: syncKey } : undefined, points: { size: 7 } },
      legend: { live: true },
      scales: { x: { time: timeAxis }, y: { auto: yMin === null && yMax === null, range: yMin === null && yMax === null ? undefined : (_u, min, max) => [yMin ?? min, yMax ?? max] }, y2: { auto: true } },
      axes: [
        { stroke: fg, grid: { stroke: grid, width: 1 }, ticks: { stroke: grid, width: 1 }, font: '11px Consolas, monospace', labelFont: '11px Consolas, monospace',
          values: timeAxis ? (_u, splits) => splits.map((split, i) => {
            const date = new Date(split); const prev = i > 0 ? new Date(splits[i - 1]) : null;
            const get = timezone === 'utc' ? { d: date.getUTCDate(), h: date.getUTCHours(), m: date.getUTCMinutes(), s: date.getUTCSeconds(), ms: date.getUTCMilliseconds() } : { d: date.getDate(), h: date.getHours(), m: date.getMinutes(), s: date.getSeconds(), ms: date.getMilliseconds() };
            const dayChanged = !prev || (timezone === 'utc' ? prev.getUTCDate() : prev.getDate()) !== get.d;
            const time = get.ms ? `${String(get.h).padStart(2, '0')}:${String(get.m).padStart(2, '0')}:${String(get.s).padStart(2, '0')}.${String(get.ms).padStart(3, '0')}` : get.s ? `${String(get.h).padStart(2, '0')}:${String(get.m).padStart(2, '0')}:${String(get.s).padStart(2, '0')}` : `${String(get.h).padStart(2, '0')}:${String(get.m).padStart(2, '0')}`;
            return dayChanged ? formatTimestamp(split, timezone).slice(0, 10) + '\n' + time : time;
          }) : undefined },
        { stroke: fg, grid: { stroke: grid, width: 1 }, ticks: { stroke: grid, width: 1 }, font: '11px Consolas, monospace', scale: 'y', size: 60, values: (_u, splits) => splits.map((v) => formatNumber(v, 2)) },
        ...(anyRight ? [{ stroke: fg, grid: { show: false }, ticks: { stroke: grid, width: 1 }, font: '11px Consolas, monospace', scale: 'y2', side: 1, size: 60, values: (_u: uPlot, splits: number[]) => splits.map((v) => formatNumber(v, 2)) }] : []),
      ],
      series: seriesOptions,
      plugins: timeAxis ? [wheelZoomPlugin()] : [],
      hooks: { setSeries: [(u, index, opts) => { if (index !== null && index > 0 && 'show' in opts && onToggleSeries) onToggleSeries(series[index - 1].key, Boolean(u.series[index].show)); }] },
      // uPlot renders epoch seconds by default; our data is milliseconds.
      ms: 1,
      tzDate: timezone === 'utc' ? (ts) => uPlot.tzDate(new Date(ts), 'Etc/UTC') : undefined,
    };
    const instance = new uPlot(options, data, element);
    plot.current = instance;
    (element as HTMLDivElement & { uplot?: uPlot }).uplot = instance;
    element.style.setProperty('--chart-text', text);
    const observer = typeof ResizeObserver === 'undefined' ? null : new ResizeObserver(() => {
      if (plot.current && element.clientWidth > 0) plot.current.setSize({ width: element.clientWidth, height });
    });
    observer?.observe(element);
    return () => { observer?.disconnect(); instance.destroy(); if (plot.current === instance) plot.current = null; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [structure, height, syncKey]);

  // Data updates preserve the current zoom window unless the user has not zoomed yet.
  useEffect(() => {
    const u = plot.current;
    if (!u || series.length === 0) return;
    const xs = data[0];
    const zoomed = xs.length > 1 && u.scales.x.min !== undefined && u.scales.x.max !== undefined && (u.scales.x.min > u.data[0][0]! || u.scales.x.max < u.data[0][u.data[0].length - 1]!);
    u.setData(data, !zoomed);
  }, [data, series.length]);

  useEffect(() => {
    const u = plot.current;
    if (!u) return;
    series.forEach((item, index) => { const show = !hiddenSeries.includes(item.key); if (u.series[index + 1] && u.series[index + 1].show !== show) u.setSeries(index + 1, { show }); });
  }, [hiddenKey, hiddenSeries, series]);

  return <div ref={container} className="ts-chart" data-testid="time-series-chart" data-series={series.length} />;
}
