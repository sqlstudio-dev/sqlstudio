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

import { useState, type FormEvent } from 'react';
import { KeyRound } from 'lucide-react';
import type { ConnectionProfile } from '../../models/sql';
import { Modal } from '../ui/Modal';

interface ConnectDialogProps { connection: ConnectionProfile; onConnect: (password: string) => void; onClose: () => void }

export function ConnectDialog({ connection, onConnect, onClose }: ConnectDialogProps) {
  const [password, setPassword] = useState('');
  const submit = (event: FormEvent) => { event.preventDefault(); onConnect(password); };
  return <Modal title={`Connect to ${connection.name}`} description="The password is used for this session only and is never written to disk." onClose={onClose}>
    <form className="dialog-form" onSubmit={submit}>
      <dl className="connect-summary">
        <dt>Server</dt><dd>{connection.host}:{connection.port}</dd>
        <dt>User</dt><dd>{connection.username || '—'}</dd>
        <dt>{connection.type === 'postgresql' ? 'Database' : 'Schema'}</dt><dd>{connection.database || '—'}</dd>
      </dl>
      <label>Password<input type="password" value={password} autoComplete="current-password" onChange={(event) => setPassword(event.target.value)} /></label>
      <div className="dialog-actions">
        <button type="button" className="secondary-button" onClick={onClose}>Cancel</button>
        <button type="submit" className="run-button"><KeyRound size={13} /> Connect</button>
      </div>
    </form>
  </Modal>;
}
