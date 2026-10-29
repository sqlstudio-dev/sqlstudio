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

import type { SqlCell } from '../models/sql';
import type { NaiveTimezone } from './model';

// ISO 8601 / SQL date-time: date, optional time, optional fraction, optional Z or ±HH[:MM] offset.
const ISO = /^(\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2})(?::(\d{2})(?:[.,](\d{1,9}))?)?)?\s*(Z|[+-]\d{2}(?::?\d{2})?)?$/i;

/** Classify a numeric epoch by magnitude: seconds, milliseconds, microseconds, or nanoseconds. */
export function epochToMs(value: number): number {
  const magnitude = Math.abs(value);
  if (magnitude < 1e11) return value * 1000;
  if (magnitude < 1e14) return value;
  if (magnitude < 1e17) return value / 1000;
  return value / 1_000_000;
}

const zoneOffsetMinutes = (zone: string): number => {
  if (zone.toUpperCase() === 'Z') return 0;
  const digits = zone.slice(1).replace(':', '');
  const minutes = Number(digits.slice(0, 2)) * 60 + (digits.length > 2 ? Number(digits.slice(2, 4)) : 0);
  return zone[0] === '-' ? -minutes : minutes;
};

/**
 * Parse a database cell into epoch milliseconds. Offset-aware strings are absolute; naive strings
 * are interpreted in `naive` (the database wall-clock) so values display as the server stored them.
 */
export function parseTimestamp(value: SqlCell, naive: NaiveTimezone = 'local'): number | null {
  if (value === null || typeof value === 'boolean' || typeof value === 'object') return null;
  if (typeof value === 'number') return Number.isFinite(value) ? epochToMs(value) : null;
  const text = value.trim();
  const match = ISO.exec(text);
  if (!match) return null;
  const [, y, mo, d, h = '0', mi = '0', s = '0', fraction = '', zone] = match;
  const ms = fraction ? Math.round(Number('0.' + fraction) * 1000) : 0;
  const parts = [Number(y), Number(mo) - 1, Number(d), Number(h), Number(mi), Number(s), ms] as const;
  if (parts[1] < 0 || parts[1] > 11 || parts[2] < 1 || parts[2] > 31 || parts[3] > 24 || parts[4] > 59 || parts[5] > 60) return null;
  if (zone) return Date.UTC(...parts) - zoneOffsetMinutes(zone) * 60_000;
  return naive === 'utc' ? Date.UTC(...parts) : new Date(...parts).getTime();
}

/** True when the string carries an explicit offset, i.e. the database returned an absolute instant. */
export const hasExplicitOffset = (value: string): boolean => /(?:Z|[+-]\d{2}(?::?\d{2})?)$/i.test(value.trim());

const pad = (n: number, width = 2) => String(n).padStart(width, '0');

export function formatTimestamp(ms: number, zone: NaiveTimezone = 'local', withMillis = false): string {
  const date = new Date(ms);
  const get = zone === 'utc'
    ? [date.getUTCFullYear(), date.getUTCMonth() + 1, date.getUTCDate(), date.getUTCHours(), date.getUTCMinutes(), date.getUTCSeconds(), date.getUTCMilliseconds()]
    : [date.getFullYear(), date.getMonth() + 1, date.getDate(), date.getHours(), date.getMinutes(), date.getSeconds(), date.getMilliseconds()];
  const [y, mo, d, h, mi, s, milli] = get;
  return `${y}-${pad(mo)}-${pad(d)} ${pad(h)}:${pad(mi)}:${pad(s)}` + (withMillis ? '.' + pad(milli, 3) : '');
}

export function formatDuration(ms: number): string {
  if (!Number.isFinite(ms) || ms < 0) return '—';
  if (ms < 1000) return `${Math.round(ms)} ms`;
  if (ms < 60_000) return `${(ms / 1000).toFixed(ms < 10_000 ? 1 : 0)} s`;
  if (ms < 3_600_000) return `${Math.round(ms / 60_000)} min`;
  if (ms < 86_400_000) return `${(ms / 3_600_000).toFixed(1)} h`;
  return `${(ms / 86_400_000).toFixed(1)} d`;
}
