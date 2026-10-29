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
import type { ConnectionInput, ConnectorPlugin } from '../../models/sql';
import { Modal } from '../ui/Modal';

interface AddConnectionDialogProps { plugins: ConnectorPlugin[]; onSubmit: (input: ConnectionInput) => Promise<unknown>; onClose: () => void }

export function AddConnectionDialog({ plugins, onSubmit, onClose }: AddConnectionDialogProps) {
  const first = plugins.find((plugin) => plugin.available) ?? plugins[0];
  const [form, setForm] = useState<ConnectionInput>({ name: '', type: first?.id ?? '', host: '127.0.0.1', port: first?.defaultPort ?? 0, username: '', database: '' });
  const [portEdited, setPortEdited] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const selected = plugins.find((plugin) => plugin.id === form.type);
  const update = (patch: Partial<ConnectionInput>) => setForm((current) => ({ ...current, ...patch }));
  const selectEngine = (type: string) => {
    const plugin = plugins.find((item) => item.id === type);
    update({ type, ...(portEdited || !plugin ? {} : { port: plugin.defaultPort }) });
  };
  const submit = async (event: FormEvent) => {
    event.preventDefault();
    if (saving) return;
    setSaving(true); setError(null);
    try { await onSubmit({ ...form, name: form.name.trim() || `${selected?.name ?? form.type} @ ${form.host}` }); onClose(); }
    catch (cause) { setError(cause instanceof Error ? cause.message : 'Could not save the connection.'); }
    finally { setSaving(false); }
  };

  return <Modal title="Add connection" description="Connection details are saved to connection.json. Passwords are requested when you connect." onClose={onClose}>
    <form className="dialog-form" onSubmit={submit} noValidate>
      <label>Database engine
        <select value={form.type} onChange={(event) => selectEngine(event.target.value)} required>
          {plugins.length === 0 && <option value="">No connector plugins loaded</option>}
          {plugins.map((plugin) => <option key={plugin.id} value={plugin.id} disabled={!plugin.available}>{plugin.name}{plugin.available ? '' : ' (driver missing)'}</option>)}
        </select>
      </label>
      {selected && !selected.available && <p className="form-note warning-text">{selected.unavailableReason ?? `Install ${selected.driver} to enable ${selected.name}.`}</p>}
      <label>Name<input value={form.name} placeholder={selected ? `${selected.name} @ ${form.host}` : 'Connection name'} maxLength={120} onChange={(event) => update({ name: event.target.value })} /></label>
      <div className="form-row">
        <label>Host<input value={form.host} required maxLength={255} onChange={(event) => update({ host: event.target.value })} /></label>
        <label>Port<input type="number" min={1} max={65535} value={form.port || ''} required onChange={(event) => { setPortEdited(true); update({ port: Number(event.target.value) }); }} /></label>
      </div>
      <div className="form-row">
        <label>Username<input value={form.username} maxLength={128} autoComplete="off" onChange={(event) => update({ username: event.target.value })} /></label>
        <label>{form.type === 'postgresql' || form.type === 'questdb' ? 'Database' : 'Default schema'}<input value={form.database} placeholder={form.type === 'questdb' ? 'qdb' : undefined} maxLength={128} onChange={(event) => update({ database: event.target.value })} /></label>
      </div>
      {error && <p className="form-error" role="alert">{error}</p>}
      <div className="dialog-actions">
        <button type="button" className="secondary-button" onClick={onClose}>Cancel</button>
        <button type="submit" className="run-button" disabled={saving || !selected?.available || !form.host.trim() || !(form.port >= 1 && form.port <= 65535)}>{saving ? 'Saving…' : 'Save connection'}</button>
      </div>
    </form>
  </Modal>;
}
