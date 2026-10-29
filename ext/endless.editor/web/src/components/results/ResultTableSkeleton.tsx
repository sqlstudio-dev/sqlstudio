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

import { LoadingProgress } from '../ui/LoadingProgress';

export function ResultTableSkeleton({ label = 'Query result loading', chart = false }: { label?: string; chart?: boolean }) {
  return <div className="result-skeleton">
    <LoadingProgress label={label} />
    {chart ? <div className="chart-skeleton" aria-hidden="true">
      {[0, 1, 2, 3, 4].map((line) => <div className="skeleton" key={line} />)}
    </div> : <div className="skeleton-table" aria-hidden="true">
      {[0, 1, 2, 3, 4].map((row) => <div className="skeleton-table-row" key={row}>
        {[0, 1, 2, 3].map((column) => <div key={column} className="skeleton" />)}
      </div>)}
    </div>}
  </div>;
}