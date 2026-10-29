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

import { cleanup, render } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { ConnectionTile } from '../src/components/connections/ConnectionTile';
import { Watermark } from '../src/components/connections/Watermark';
import type { ConnectionProfile, ConnectorPlugin } from '../src/models/sql';

afterEach(cleanup);

describe('Connection watermarks', () => {
  it.each([
    ['mysql', 'mysql'], ['mariadb', 'mysql'], ['dolphin', 'mysql'],
    ['postgresql', 'postgresql'], ['postgres', 'postgresql'], ['elephant', 'postgresql'],
    ['mssql', 'mssql'], ['sqlserver', 'mssql'],
    ['questdb', 'questdb'], ['quest', 'questdb'], ['QuestDB', 'questdb'],
    ['influxdb', 'influxdb'], ['influx', 'influxdb'], ['InfluxDB', 'influxdb'],
    ['timescaledb', 'timescaledb'], ['timescale', 'timescaledb'], ['TimescaleDB', 'timescaledb'],
    ['clickhouse', 'clickhouse'], ['ClickHouse', 'clickhouse'],
    ['database', 'database'], ['unknown-engine', 'database'],
  ])('renders %s with the %s logo', (kind, logo) => {
    const { container } = render(<Watermark kind={kind} />);
    const image = container.querySelector('img');
    expect(image?.getAttribute('src')).toBe(`/src/assets/watermarks/${logo}.png`);
    expect(image?.getAttribute('alt')).toBe('');
    expect(image?.getAttribute('aria-hidden')).toBe('true');
    expect(image?.getAttribute('draggable')).toBe('false');
    expect(image?.classList.contains('tile-watermark')).toBe(true);
    if (kind === 'unknown-engine') expect(image?.classList.contains('watermark-database')).toBe(true);
  });

  it.each(['questdb', 'influxdb', 'timescaledb', 'clickhouse'])('uses the %s connection type when no plugin is loaded', (type) => {
    const connection: ConnectionProfile = {
      id: type, name: type, type, host: 'localhost', port: 9000, username: '', database: '',
      default: false, lastConnected: null, options: {},
    };
    const { container } = render(<ConnectionTile connection={connection} onOpen={vi.fn()} onRemove={vi.fn()} />);
    expect(container.querySelector('img')?.getAttribute('src')).toBe(`/src/assets/watermarks/${type}.png`);
  });

  it('uses the plugin watermark when one is provided', () => {
    const connection: ConnectionProfile = {
      id: 'metrics', name: 'metrics', type: 'custom-engine', host: 'localhost', port: 5432, username: '', database: '',
      default: false, lastConnected: null, options: {},
    };
    const plugin: ConnectorPlugin = {
      id: 'custom-engine', name: 'Metrics', watermark: 'timescale', defaultPort: 5432, driver: 'custom',
      aliases: [], available: true, unavailableReason: null,
    };
    const { container } = render(<ConnectionTile connection={connection} plugin={plugin} onOpen={vi.fn()} onRemove={vi.fn()} />);
    expect(container.querySelector('img')?.getAttribute('src')).toBe('/src/assets/watermarks/timescaledb.png');
  });
});
