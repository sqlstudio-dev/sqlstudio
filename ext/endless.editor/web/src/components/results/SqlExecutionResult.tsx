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

import { CircleAlert, CircleCheck } from 'lucide-react';
import type { SqlExecution, SqlSingleResult } from '../../models/sql';
import { ResultTabs } from '../timeseries/ResultTabs';
import { ResultTableSkeleton } from './ResultTableSkeleton';
import { SqlResultTable } from './SqlResultTable';

const duration = (value: number) => Math.max(0, Math.round(value)) + ' ms';

function Result({ execution, result }: { execution: SqlExecution; result: SqlSingleResult }) {
  return <div className="execution-output">
    {result.type === 'resultset' ? <>
      <ResultTabs executionId={execution.id} sql={execution.sql} result={result}><SqlResultTable result={result} /></ResultTabs>
      <div className="output-status"><CircleCheck size={14} />
        {result.returnedRows} {result.returnedRows === 1 ? 'row' : 'rows'} returned
        {result.truncated && <span className="warning-text"> · result limited to {result.rows.length} rows</span>}
        <span className="status-duration">{duration(result.executionTimeMs)}</span>
      </div>
    </> : <div className="command-result"><CircleCheck size={15} />
      <div><strong>{result.message ?? 'Query OK'}</strong><span>{result.affectedRows} {result.affectedRows === 1 ? 'row' : 'rows'} affected</span></div>
      <time>{duration(result.executionTimeMs)}</time>
    </div>}
    {result.warningCount > 0 && <div className="output-status warning-text">{result.warningCount} warning(s). Run SHOW WARNINGS for details.</div>}
  </div>;
}

export function SqlExecutionResult({ execution }: { execution: SqlExecution }) {
  if (execution.status === 'pending') return <div className="execution-output"><ResultTableSkeleton /><div className="output-status muted">Running query…</div></div>;
  if (execution.status === 'error') {
    const error = execution.error!;
    return <>
      {execution.partialResults?.map((result, index) => <Result key={index} execution={execution} result={result} />)}
      <div className="execution-output"><div className="error-panel" role="alert">
        <div className="error-title"><CircleAlert size={15} /> Query failed</div><div className="error-message">{error.message}</div>
        <div className="error-meta">{error.code != null && <span>Code {error.code}</span>}{error.sqlState && <span>SQL state {error.sqlState}</span>}<span>{duration(error.executionTimeMs)}</span></div>
      </div></div>
    </>;
  }
  const result = execution.result!;
  return result.type === 'batch'
    ? <>{result.results.map((item, index) => <Result key={index} execution={execution} result={item} />)}<div className="execution-output output-status muted">Total: {duration(result.executionTimeMs)}</div></>
    : <Result execution={execution} result={result} />;
}
