<!--
Copyright (c) 2026 dev4fun. All rights reserved.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2.0,
as published by the Free Software Foundation.

This program is designed to work with certain software (including
but not limited to OpenSSL) that is licensed under separate terms, as
designated in a particular file or component or in included license
documentation.  The authors of MySQL hereby grant you an additional
permission to link the program and your derivative works with the
separately licensed software that they have either included with
the program or referenced in the documentation.
This program is distributed in the hope that it will be useful,  but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
the GNU General Public License, version 2.0, for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
-->

# Endless Control

A shared React interface hosted in native desktop windows on Windows, macOS, and Linux. The frontend is built once; each host loads the static `web/dist` output with its platform's webview.

| Platform | Host | Webview |
| --- | --- | --- |
| Windows | C# / WinForms | Microsoft Edge WebView2 |
| macOS | Swift / AppKit | WKWebView |
| Linux | C / GTK 4 | WebKitGTK 6 |

## Build the React app

### PowerShell build script (all platforms)

Use PowerShell 7 (`pwsh`) on Windows, macOS, or Linux:

```powershell
# Build React and the native host for the current operating system (Release).
pwsh ./build.ps1

# Build, start the React development server, then open the native app.
pwsh ./build.ps1 -Start

# Build/start React only, without opening a native window.
pwsh ./build.ps1 -Target React -Start

# Build React only.
pwsh ./build.ps1 -Target React

# Build only the native host using the existing web/dist bundle.
pwsh ./build.ps1 -Target Native -Configuration Debug

# Explicit platform; run each command on that operating system.
pwsh ./build.ps1 -Platform Windows
pwsh ./build.ps1 -Platform macOS
pwsh ./build.ps1 -Platform Linux

# Reinstall locked frontend dependencies before building.
pwsh ./build.ps1 -InstallDependencies
```

The script works from any current directory when invoked by its path. It runs `npm ci` automatically if `web/node_modules` is absent, checks required build tools, and stops on a failed command. `All` means React plus the native host for the current OS, not cross-compiling three operating systems. Use separate Windows/macOS/Linux machines or CI runners to build all three hosts. Native prerequisites are listed below.

With `-Start`, builds finish first, then `scripts/start-dev.ps1` starts Vite at `http://127.0.0.1:5173`, waits for a successful response, and opens the native host. An existing notebook development server is reused. A different app occupying the port or a startup failure stops the launch. The host receives `ENDLESS_CONTROL_WEB_URL` and loads the development server with hot reload instead of its static bundle. All three native hosts support this environment variable; without it they load the built files as before. `-Target Native -Start` skips rebuilding React and opens the native app against Vite; `-Target React -Start` starts only Vite. The SQL API and database must be started separately.

The React server runs in the background and remains available after closing the native window. New server logs are written to `build/logs/vite.log` and `build/logs/vite-error.log`; the script prints its PID, which you can stop with `Stop-Process -Id <PID>`. Windows build and development startup were verified; macOS/Linux require verification on their respective operating systems.

Windows output is `hosts/windows/bin/<Configuration>/net10.0-windows/WindowsHost.exe`, with React copied alongside it. macOS output is `hosts/macos/.build/<configuration>/endless-control-mac`. Linux output is `build/linux/<configuration>/endless-control`. For macOS/Linux, set `ENDLESS_CONTROL_WEB_ROOT` to the absolute `web/dist` path when launching. Close the Windows app before rebuilding the same configuration if its executable is locked.

Requires Node.js 20.19+ or 22.12+.

```sh
npm --prefix web install
npm --prefix web run build
```

The output uses relative asset URLs so it can be loaded directly from disk by each native host.

## Run a host

Run commands from the repository root after building the React app.

### Windows

Requires the .NET 10 SDK and the Microsoft Edge WebView2 Runtime.

```powershell
dotnet run --project hosts/windows/WindowsHost.csproj
```

The project copies `web/dist` into the app output directory. WebView2 NuGet references provide the host SDK; the Evergreen WebView2 Runtime must be installed on the machine running the app.

### macOS

Requires Xcode Command Line Tools and Swift 5.9 or newer. The app uses Apple's built-in WKWebView.

```sh
swift run --package-path hosts/macos
```

### Linux

Requires GTK 4, WebKitGTK 6.0, Meson, and Ninja. On Debian/Ubuntu, install `libgtk-4-dev`, `libwebkitgtk-6.0-dev`, `meson`, and `ninja-build`.

```sh
meson setup build/linux hosts/linux
meson compile -C build/linux
./build/linux/endless-control
```

If the bundle lives elsewhere, set `ENDLESS_CONTROL_WEB_ROOT` to the directory containing `index.html` before launching the macOS or Linux host. Windows loads the bundle copied beside its executable.

## Development

To work on the React page in a browser with hot reload:

```sh
npm --prefix web run dev
```

The native hosts load the production bundle, so rebuild it after frontend changes before relaunching a host.


## SQL notebook

The app opens on a **Connections** home: saved connections are shown as tiles grouped by database engine, each with an engine logo watermark from `web/src/assets/watermarks/` (MySQL, PostgreSQL, SQL Server, and a generic database icon for engines without a dedicated logo). A plugin selects its logo through its `watermark` name; connections whose engine has no plugin fall back to their `type`, so an `mssql` entry already shows the SQL Server mark. Clicking a tile asks for the password and opens the SQL notebook for that connection; the toolbar's back arrow or the rail's Connections button closes the session and returns home. **Add connection** opens a dialog listing every loaded connector plugin; **Import XML** reads a SqlStudio / MySQL Workbench `connections.xml` (GRT `db.mgmt.Connection` entries).

The prompt supports command recall with Up on the first line and Down on the last line. Down past the newest command restores the unfinished draft. Editing a recalled command creates a draft without changing the executed history. Modified arrows and selections keep their normal editing behavior. SQL syntax highlighting is shown both while typing and in executed statements (keywords, quoted text, comments, numbers, identifiers, and variables). This is highlighting, not SQL validation or schema autocomplete.

The active frontend is now a SQL-only React/TypeScript notebook. Executed statements stay read-only above one editable prompt. Ctrl+Enter or Cmd+Enter runs the prompt; while a query is pending the next prompt accepts a draft, execution and Clear are disabled, and a grid-shaped skeleton appears. Results, warnings, and errors remain attached to their statement.

### Connections, plugins, and connection.json

- **Connector plugins** live in `backend/plugins/`. Every `*_connector.py` file there is imported at backend startup; it must define a module-level `plugin` instance of `app.plugins.base.ConnectorPlugin` (id, display name, default port, watermark, driver package, `connect`, `describe_columns`, `error_details`). Shipped plugins: `mysql_connector.py` (PyMySQL; also serves `mariadb`) and `postgresql_connector.py` (psycopg 3). A plugin whose driver is not installed still appears in the Add dialog, disabled, with the install hint. Broken plugin files are logged and skipped. `ENDLESS_PLUGINS_DIR` overrides the folder.
- **connection.json** (default `backend/connection.json`, override with `ENDLESS_CONNECTIONS_FILE`; gitignored) stores `{ "version": 1, "connections": [...] }` with id, name, type, host, port, username, database, default flag, lastConnected, and non-secret options such as serverVersion/useSSL. **Passwords are never written**; they are requested when a tile is opened and held in memory only for that session's reconnects.
- **First start**: when connection.json does not exist the backend imports SqlStudio's `connections.xml` from `%APPDATA%\MySQL\SqlStudio` (Windows), `~/Library/Application Support/MySQL/SqlStudio` (macOS) or `~/.config/MySQL/SqlStudio` (Linux), or the path in `SQLSTUDIO_CONNECTIONS_XML`. If nothing is found it seeds one "Local development" entry from the DB_* environment variables below. Imports keep the GRT connection id, so re-importing the same file never duplicates entries; `password` parameters are dropped and unknown drivers are skipped.
- API: `GET /api/plugins`, `GET/POST /api/connections`, `DELETE /api/connections/{id}`, `POST /api/connections/import` (`{ "xml": "..." }`), and `POST /api/sessions` now takes `{ "connectionId", "password" }`. Connection failures return 503 with the driver's message (for example *Access denied*), unknown ids 404, unsupported engines 400.

### Start the backend (PowerShell, from the repository root)

Requires Python 3.10+ and an existing MySQL, MariaDB, or PostgreSQL server.

~~~powershell
python -m venv backend/.venv
backend/.venv/Scripts/python -m pip install -r backend/requirements.txt
# Optional: seed connection used only when connection.json does not exist yet.
$env:DB_HOST = '127.0.0.1'
$env:DB_PORT = '3306'
$env:DB_USERNAME = 'root'
$env:DB_DATABASE = 'sakila'
backend/.venv/Scripts/python -m uvicorn app.main:app --app-dir backend --host 127.0.0.1 --port 8000
~~~

Python packages: FastAPI, Uvicorn (standard extras), PyMySQL (RSA authentication extras), and psycopg (binary). MySQL and MariaDB share the PyMySQL plugin. The backend is a separate process; the native hosts do not launch it automatically.

Environment overrides are DB_TYPE, DB_HOST, DB_PORT, DB_USERNAME, DB_DATABASE, and DB_RESULT_LIMIT (default 1000 rows per result set); they only shape the seed connection and the row limit — day-to-day connections come from connection.json. DB_PASSWORD is used by the opt-in live test only.

### Start the frontend (second terminal, repository root)

~~~powershell
npm --prefix web install
npm --prefix web run dev
~~~

Open http://127.0.0.1:5173. The API defaults to http://127.0.0.1:8000; VITE_API_URL can override it at frontend build/start time. Build with npm --prefix web run build (includes TypeScript checking). Tailwind 4 uses the official Vite plugin; theme tokens live in web/src/console.css. No remote fonts or mock database results are loaded by the application.

### Architecture and important files

- web/src/context/SqlConsoleContext.tsx owns reducer state, the connection list, session lifecycle (open/close/retry with the in-memory password), and the synchronous duplicate-execution guard. React StrictMode reuses the pending requests.
- web/src/actions/sqlConsoleActions.ts performs async work through web/src/api/sqlApi.ts and dispatches explicit actions.
- web/src/reducers/sqlConsoleReducer.ts updates immutable execution entries; clearing history does not reset the database session.
- web/src/models/sql.ts defines connection, plugin, session, execution, result, and error contracts.
- web/src/components/connections/ holds the connections home (engine groups, tiles, watermarks), the add-connection and password dialogs; web/src/components/ui/Modal.tsx is the shared dialog shell. web/src/components/sql-console/ contains the console, immutable history blocks, and replaceable textarea prompt. web/src/components/results/ renders dynamic tables, command summaries, errors, and skeletons.
- backend/app/api/connections.py exposes the plugin list and connection CRUD/import; backend/app/api/sql.py exposes POST /api/sessions, POST /api/sessions/{id}/execute, and DELETE /api/sessions/{id}. GET /api/health reports API availability, not database connectivity.
- backend/app/plugins/ defines the ConnectorPlugin contract and the loader; backend/plugins/ contains the engine plugins. backend/app/database/ holds the connection store, the connections.xml importer, one physical connection per session, its execution lock, and the session registry.
- backend/app/services/sql_executor.py executes SQL in a worker thread, consumes every result, caps retained rows, and normalizes errors through the session's plugin. backend/app/models/responses.py handles request models and JSON-safe cell values.

The reference notebook was studied at D:/develop/_mysql_/shell-plugins/gui/frontend: inline execution contexts, result presentation, and the next prompt informed the behavior. Its class-based architecture was not copied. Obsolete App.jsx/main.jsx files were removed because extensionless imports could resolve to the old dashboard.

### Database session semantics

Each notebook creates one server-side session ID with one physical driver connection (PyMySQL or psycopg). Every command using that ID is serialized under that session's lock. USE, user variables, temporary tables, and explicit transactions therefore survive across commands. No automatic reconnect or retry silently replays SQL or loses transaction state.

Connections start with autocommit enabled, matching an interactive SQL client. Ordinary writes commit immediately. START TRANSACTION, COMMIT, ROLLBACK, and SET autocommit work as normal SQL; the executor does not commit after every command. Closing a connection rolls back an outstanding transaction according to database behavior.

Results use an unbuffered cursor on MySQL and are drained in batches even after the display limit is reached; only the first DB_RESULT_LIMIT rows per result set are retained. This avoids unread-result failures on the next query. psycopg's default client-side cursor buffers the full result before the limit is applied. Decimal and unsafe BIGINT values are strings to preserve precision; binary values carry a size placeholder. CALL result sets are all displayed. PyMySQL does not expose SQLSTATE in its normal error interface, so sqlState is null for MySQL; PostgreSQL errors report their SQLSTATE as both code and sqlState.

A real provider unmount/page close attempts to delete the session. Backend shutdown closes all registered connections. Page-close delivery is best-effort; an abruptly killed browser may leave a connection registered until backend shutdown. Frontend history lives in memory and reload starts a new notebook. Run one backend worker: the registry is process-local.

### Verification

~~~powershell
npm --prefix web test
backend/.venv/Scripts/python -m pip install -r backend/requirements-dev.txt
Push-Location backend
.venv/Scripts/python -m unittest discover -s tests -v
# Optional: run against the configured real server.
$env:SQL_LIVE_TEST = '1'
.venv/Scripts/python -m unittest discover -s tests -p test_live_database.py -v
Pop-Location
~~~

The live check uses temporary tables and connection-local variables against a connection seeded from the DB_* variables (DB_PASSWORD supplies the password). It checks USE, variable persistence, UPDATE, rollback, inline errors, truncation, and a subsequent query; it does not modify the sakila.actor table. Run it once with each MySQL/MariaDB configuration to verify both servers.

Implementation verification: production build passed, twelve backend unit/API tests (executor, plugin loader, connection store, XML import, connections API, session API) and eleven frontend interaction tests passed. The connections home, add-connection dialog, engine grouping, watermarks, XML auto-import, and the password prompt were checked in a browser against the running backend; a wrong password surfaced the MySQL *Access denied* message with a Reconnect action. A PostgreSQL round trip has not yet been run against a live server.

### Current scope

One SQL statement per execution; semicolon-separated scripts and client DELIMITER commands are not implemented. Stored procedures may return multiple results. The display limit bounds row count, not individual cell size or database execution cost; draining a huge result can still take time. Cancellation, paging, durable history, session expiration, schema browsing, editing saved connections, SSL options, OS credential storage, and multiple tabs remain future work. The API is a local developer service without authentication; run it on loopback.

Driver behavior reference: https://pymysql.readthedocs.io/en/latest/modules/cursors.html
Tailwind integration reference: https://tailwindcss.com/docs/installation/using-vite
