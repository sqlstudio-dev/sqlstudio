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

export type SqlExecutionStatus = 'pending' | 'success' | 'error';
export type SqlCell = string | number | boolean | null | { binary: true; size: number };
export type SqlColumnKind = 'timestamp' | 'date' | 'time' | 'number' | 'boolean' | 'string' | 'binary' | 'other';

export interface SqlColumn { name: string; type?: string; kind?: SqlColumnKind }
export interface SqlResultSet {
  type: 'resultset'; columns: SqlColumn[]; rows: SqlCell[][]; returnedRows: number;
  truncated: boolean; executionTimeMs: number; warningCount: number;
}
export interface SqlCommandResult {
  type: 'command'; affectedRows: number; executionTimeMs: number; warningCount: number; message?: string;
}
export interface SqlError { message: string; code?: number | string | null; sqlState?: string | null; executionTimeMs: number }
export interface SqlExecution {
  id: string; sql: string; startedAt: string; finishedAt?: string; status: SqlExecutionStatus;
  result?: SqlResult; error?: SqlError; partialResults?: SqlSingleResult[];
}
export interface SqlSession {
  sessionId: string; connectionId: string; name: string; database?: string | null; host: string; port: number;
  username: string; type: string;
}
export interface ConnectionProfile {
  id: string; name: string; type: string; host: string; port: number; username: string; database: string;
  default: boolean; lastConnected: number | null; options: Record<string, string | number | boolean>;
}
export interface ConnectionInput { name: string; type: string; host: string; port: number; username: string; database: string }
export type LiveQueryMode = 'none' | 'polling' | 'streaming';
export interface VisualizationCapabilities {
  timeSeries: boolean; supportedLiveModes: LiveQueryMode[]; serverSideAggregation: boolean; timeBucketing: boolean;
  incrementalQueries: boolean; latestValueQueries: boolean; queryCancellation: boolean;
}
export const noVisualizationCapabilities: VisualizationCapabilities = {
  timeSeries: true, supportedLiveModes: [], serverSideAggregation: false, timeBucketing: false,
  incrementalQueries: false, latestValueQueries: false, queryCancellation: false,
};
export interface ConnectorPlugin {
  id: string; name: string; defaultPort: number; watermark: string; driver: string; aliases: string[]; available: boolean;
  unavailableReason: string | null; capabilities?: VisualizationCapabilities;
}
export type TimeSeriesAggregation = 'avg' | 'sum' | 'min' | 'max' | 'count';
export interface TimeSeriesQueryRequest {
  sql: string; timeColumn: string; valueColumns?: string[]; groupColumn?: string | null; aggregation?: TimeSeriesAggregation;
  intervalSeconds?: number | null; since?: string | null; until?: string | null; sinceExclusive?: boolean; limit?: number | null;
}
export interface TimeSeriesQueryResponse { sql: string; result: SqlExecutionResponse }
export interface ConnectionImportResult { imported: number; duplicates: number; skipped: number; connections: ConnectionProfile[] }
export type SqlSingleResult = SqlResultSet | SqlCommandResult;
export type SqlResult = SqlSingleResult | { type: 'batch'; results: SqlSingleResult[]; executionTimeMs: number };
export type SqlExecutionResponse = SqlResult | { type: 'error'; error: SqlError; partialResults?: SqlSingleResult[] };