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

import { defaultVisualizationConfig, type VisualizationConfig } from './model';

const PREFIX = 'endless.visualization:';

/** Stable key per connection and statement so preferences follow the query, not the tab instance. */
export function configKey(connectionId: string, sql: string): string {
  let hash = 0x811c9dc5;
  const text = sql.replace(/\s+/g, ' ').trim();
  for (let i = 0; i < text.length; i++) { hash ^= text.charCodeAt(i); hash = Math.imul(hash, 0x01000193) >>> 0; }
  return `${PREFIX}${connectionId}:${hash.toString(16)}`;
}

const storage = (): Storage | null => { try { return typeof localStorage === 'undefined' ? null : localStorage; } catch { return null; } };

export function loadVisualizationConfig(key: string): VisualizationConfig {
  try {
    const raw = storage()?.getItem(key);
    if (!raw) return defaultVisualizationConfig;
    const parsed = JSON.parse(raw) as Partial<VisualizationConfig>;
    return parsed.version === 1 ? { ...defaultVisualizationConfig, ...parsed } : defaultVisualizationConfig;
  } catch { return defaultVisualizationConfig; }
}

export function saveVisualizationConfig(key: string, config: VisualizationConfig): void {
  try { storage()?.setItem(key, JSON.stringify(config)); } catch { /* quota or privacy mode: preferences stay in memory */ }
}
