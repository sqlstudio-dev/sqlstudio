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

import clickhouseLogo from '../../assets/watermarks/clickhouse.png';
import databaseLogo from '../../assets/watermarks/database.png';
import influxdbLogo from '../../assets/watermarks/influxdb.png';
import mssqlLogo from '../../assets/watermarks/mssql.png';
import mysqlLogo from '../../assets/watermarks/mysql.png';
import postgresqlLogo from '../../assets/watermarks/postgresql.png';
import questdbLogo from '../../assets/watermarks/questdb.png';
import timescaledbLogo from '../../assets/watermarks/timescaledb.png';

// Engine logos drawn faintly on connection tiles. A plugin picks one by name;
// anything unknown falls back to the generic database icon.
const logos: Record<string, string> = {
  mysql: mysqlLogo, mariadb: mysqlLogo, dolphin: mysqlLogo,
  postgresql: postgresqlLogo, postgres: postgresqlLogo, elephant: postgresqlLogo,
  mssql: mssqlLogo, sqlserver: mssqlLogo,
  questdb: questdbLogo, quest: questdbLogo,
  influxdb: influxdbLogo, influx: influxdbLogo,
  timescaledb: timescaledbLogo, timescale: timescaledbLogo,
  clickhouse: clickhouseLogo,
  database: databaseLogo,
};

export function Watermark({ kind }: { kind: string }) {
  const key = kind.toLowerCase();
  return <img className={`tile-watermark watermark-${key in logos ? key : 'database'}`} src={logos[key] ?? databaseLogo} alt="" aria-hidden="true" draggable={false} />;
}
