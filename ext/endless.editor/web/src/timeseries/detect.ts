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

import type { SqlCell, SqlColumn } from '../models/sql';
import type { ColumnProfile, TimeSeriesDetection, TimeSeriesMapping } from './model';
import { parseTimestamp } from './timestamps';

const SAMPLE_ROWS = 500;
const MAX_GROUP_CARDINALITY = 50;
const TIME_NAME_HINT = /(^|_)(ts|time|timestamp|date|datetime|epoch|created|updated|recorded)(_|$)|_at$/i;

export const toNumber = (value: SqlCell): number | null => {
  if (typeof value === 'number') return Number.isFinite(value) ? value : null;
  if (typeof value === 'boolean') return value ? 1 : 0;
  if (typeof value === 'string' && value.trim() !== '') {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : null;
  }
  return null;
};

function sampleValues(rows: SqlCell[][], index: number): SqlCell[] {
  const values: SqlCell[] = [];
  const step = Math.max(1, Math.floor(rows.length / SAMPLE_ROWS));
  for (let i = 0; i < rows.length && values.length < SAMPLE_ROWS; i += step) {
    const value = rows[i]?.[index];
    if (value !== null && value !== undefined) values.push(value);
  }
  return values;
}

/**
 * Inspect metadata first and fall back to sampling values. Column names are only a tie-breaker
 * for numeric columns that could be epoch timestamps; they never decide on their own.
 */
export function profileColumns(columns: SqlColumn[], rows: SqlCell[][]): ColumnProfile[] {
  return columns.map((column, index) => {
    const kind = column.kind;
    const sample = sampleValues(rows, index);
    const distinct = new Set(sample.map((value) => typeof value === 'object' && value !== null ? `bin:${value.size}` : String(value))).size;
    let isTime = kind === 'timestamp' || kind === 'date';
    let isNumeric = kind === 'number';
    let evidence: ColumnProfile['evidence'] = isTime || isNumeric ? 'metadata' : 'none';
    if (!isTime && !isNumeric && sample.length > 0 && (kind === undefined || kind === 'string' || kind === 'other' || kind === 'boolean')) {
      const strings = sample.filter((value): value is string => typeof value === 'string');
      if (strings.length === sample.length && strings.every((value) => parseTimestamp(value) !== null)) { isTime = true; evidence = 'sampled'; }
      else if (sample.every((value) => toNumber(value) !== null)) { isNumeric = true; evidence = 'sampled'; }
    }
    if (isNumeric && !isTime && TIME_NAME_HINT.test(column.name) && sample.length > 0
        && sample.every((value) => typeof value === 'number' && Math.abs(value) >= 1e9 && Number.isInteger(value))) {
      isTime = true; evidence = 'sampled';
    }
    const lowCardinality = sample.length > 1 && distinct > 1 && distinct <= Math.min(MAX_GROUP_CARDINALITY, Math.ceil(sample.length / 2));
    const integerLike = isNumeric && sample.every((value) => Number.isInteger(toNumber(value)));
    const isGroup = !isTime && lowCardinality && (!isNumeric || integerLike);
    return { name: column.name, index, isTime, isNumeric, isGroup, evidence, cardinality: distinct };
  });
}

export function suggestMapping(profiles: ColumnProfile[]): TimeSeriesMapping | null {
  const time = profiles.find((p) => p.isTime && p.evidence === 'metadata') ?? profiles.find((p) => p.isTime);
  if (!time) return null;
  const group = profiles.find((p) => p.isGroup && !p.isNumeric && p.name !== time.name) ?? null;
  const values = profiles.filter((p) => p.isNumeric && !p.isTime && p.name !== time.name && p.name !== group?.name).map((p) => p.name);
  if (values.length === 0) {
    // Only integer columns remain and one of them looked like a group; plot it instead of grouping by it.
    const fallback = profiles.filter((p) => p.isNumeric && !p.isTime && p.name !== time.name).map((p) => p.name);
    return fallback.length ? { timeColumn: time.name, valueColumns: fallback, groupColumn: null } : null;
  }
  return { timeColumn: time.name, valueColumns: values, groupColumn: group?.name ?? null };
}

export function detectTimeSeries(columns: SqlColumn[], rows: SqlCell[][]): TimeSeriesDetection {
  const profiles = profileColumns(columns, rows);
  const suggested = rows.length > 0 ? suggestMapping(profiles) : null;
  return {
    candidate: suggested !== null, columns: profiles, suggested,
    timeColumns: profiles.filter((p) => p.isTime).map((p) => p.name),
    numericColumns: profiles.filter((p) => p.isNumeric && !p.isTime).map((p) => p.name),
    groupColumns: profiles.filter((p) => p.isGroup).map((p) => p.name),
  };
}

export function validateMapping(mapping: TimeSeriesMapping | null, columns: SqlColumn[]): TimeSeriesMapping | null {
  if (!mapping) return null;
  const names = new Set(columns.map((column) => column.name));
  if (!names.has(mapping.timeColumn)) return null;
  const valueColumns = mapping.valueColumns.filter((name) => names.has(name) && name !== mapping.timeColumn);
  const groupColumn = mapping.groupColumn && names.has(mapping.groupColumn) && mapping.groupColumn !== mapping.timeColumn ? mapping.groupColumn : null;
  return valueColumns.length ? { timeColumn: mapping.timeColumn, valueColumns, groupColumn } : null;
}

const LEADING_COMMENTS = /^(?:\s|--[^\n]*\n|\/\*[\s\S]*?\*\/)+/;
const READ_ONLY = /^\(*\s*(select|with|show|explain|describe|desc|table|values)\b/i;

/** Mirrors the backend guard: only these statements may be re-executed by live polling. */
export const isReadOnlyStatement = (sql: string): boolean => READ_ONLY.test((sql.trim() + '\n').replace(LEADING_COMMENTS, ''));
