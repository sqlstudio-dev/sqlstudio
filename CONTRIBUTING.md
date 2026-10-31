# Contributing

Thanks for contributing to SqlStudio.

This project is independently maintained by dev4fun. Contributions, bug reports,
and feature ideas are handled through the GitHub repository.

## Before You Start

- Use [GitHub Discussions](https://github.com/dante-d4f/sqlstudio/discussions) for questions, design discussion,
  and general feedback.
- Use [GitHub Issues](https://github.com/dante-d4f/sqlstudio/issues) to report bugs or request features.
- Search existing issues and discussions before opening a new one.

## Submitting Changes

1. Fork the repository and create a branch from `develop`.
2. Keep changes focused and clearly scoped.
3. Add or update tests when your change affects behavior.
4. Verify the relevant build and tests pass before submitting.
5. Open a pull request on GitHub against the `develop` branch.

## Native SQL Editor Tests

The Windows runner saves Google Test XML results under
`testing\test-suite\TestResults\<Platform>\<Configuration>\last-test-results.xml`,
including failed runs. To rerun only suites that failed in the previous run:

```powershell
.\testing\test-suite\compile.ps1 -Action test -SkipBuild -RerunFailed
```

This reruns whole suites, including suite setup/teardown failures, not just failed
test cases. A passing previous run does not launch tests. Missing or invalid
reports produce an explicit error. Each execution replaces the previous results;
listing tests leaves them unchanged. Failed-suite names are also saved immediately
to `failed-suites.txt` in the same directory. If a later test crashes or the run
is interrupted before XML is written, `-RerunFailed` uses those recorded failures.
Tests interrupted before reporting a failure cannot be inferred. History is kept
outside the build output so rebuilding does not remove it.
While recording output, the runner preserves colored Google Test status markers:
green for success/progress, red for failures, and yellow for skips.
Use `--gtest_color=no` (or `GTEST_COLOR=no`) to disable these colors.
Do not combine `-RerunFailed` with `--gtest_filter` or `--gtest_list_tests`.
Custom reports save history when using `--gtest_output=xml:<file.xml>`; other
output formats still run using console failure history with a warning. Relative XML
paths are based on `testing\test-suite`. Google Test reports only the last
iteration when repeating tests, so failure history has that same limitation.
Runner regression checks (no build required):
`powershell -NoProfile -File .\testing\test-suite\compile.tests.ps1`.

On Windows, run the schema-tree and metadata-fetching unit tests with:

```powershell
.\testing\test-suite\compile.ps1 -Action test -SkipBuild -TestArgs '--gtest_filter=SQL_Editor_FormTest.*:Live_Schema_TreeTest.*'
```

Build the native test executable first when sources have changed. `SQL_Editor_FormTest`
uses an injected metadata session with canned query results: it exercises the real
schema, column, index, trigger and foreign-key processing without connecting to MySQL,
creating databases, loading application runtime XML, or waiting for background work.
It does not cover live connector behavior or the complete SQL editor UI.
The fixture initializes GRT's notification center itself, so the suite can run
alone without another suite initializing the application first. `-SkipBuild`
does not load the Visual Studio build environment when running tests.

For model-document integration tests, use:

```powershell
.\testing\test-suite\compile.ps1 -Action test -SkipBuild -TestArgs '--gtest_filter=SqlStudio_model_document_integration_testsTest.*'
```

These tests require the built application runtime resources. Their fixture and
runtime paths are resolved before model operations can change the working directory,
and the test context creates the output directory for saved models. The model
fixtures used by this suite use the SqlStudio format marker and `studio.*` class
names; this does not add support for opening legacy MySQL Workbench documents.

For diagram undo/redo integration tests, use the same runner with
`--gtest_filter=UndoRedoForDiagramActionsInSqlStudioTest.*`. Each test opens
`testing\test-suite\data\studio\undo_test_model1.mwb` and releases its application
test context afterward. These tests also require the built runtime resources,
but do not require a MySQL connection.

For SQL-to-model diff/apply tests, use
`--gtest_filter=db_mysql_pluginTest.*`. The suite parses SQL and applies changes
to the `all_objects.mwb` model fixture in memory. It requires the built application
runtime resources, but does not connect to MySQL or execute SQL against a server.

For forward-engineering SQL export tests, use
`--gtest_filter=ForwardEngineerTest.*`. These tests initialize the application
runtime, load SqlStudio model fixtures, and compare generated SQL with the checked-in
expected scripts without connecting to MySQL.
The export target is pinned to MySQL 8.0, matching the expected index-visibility
syntax. Comparisons normalize CRLF to LF but otherwise require exact SQL output.

For offline SQL synchronization regressions, use
`--gtest_filter=db_mysql_plugin_testTest.*`. These tests check generated DDL
operations, routine delimiters, view placeholders, and in-memory model changes.
They require built application runtime resources but no MySQL connection.
The fixture explicitly targets MySQL 8.0.16 so saved application preferences
cannot change version-dependent index-generation expectations.
They no longer execute SQL or reverse-engineer a server, so they do not prove
server acceptance or schema round-trip equivalence.

For SQL parser catalog regressions, use `--gtest_filter=MySqlParserTest.*`.
Fixture SQL is normalized from CRLF to LF before parsing so Git checkout settings
do not affect stored definitions. Catalog comparisons remain exact; SQL-definition
failures report the member path, lengths, and first differing byte instead of SQL.
These tests do not write generated catalog XML into the fixture directories.

For SQL editor context-help tests, use `--gtest_filter=SqlEditorHelpTest.*`.
Topic entries use inclusive lower and exclusive upper version bounds. The main
cases target MySQL 8.0.0, with additional checks on both sides of that boundary.

For offline connector-contract tests, use `--gtest_filter=DbcConnectionTest.*`.
An instance-local native driver factory supplies strict Connector/C++ mocks to the
real connection manager. These tests cover option conversion, session startup,
initialization callbacks, connection ownership, wrapper forwarding, and error
propagation without using database environment variables or connecting to MySQL.
They do not verify server transactions, live connection IDs, or KILL behavior.
Keep the test-only connector mock signatures aligned with the bundled interfaces;
methods added by newer connector releases deliberately omit `override`.

`--gtest_filter=DbcGeneralTest.*` also runs offline. It checks ordered statement,
metadata, prepared-statement, and rollback calls through the connection manager
with strict connector mocks. The canned results do not verify MySQL schema,
metadata, or transaction behavior.

`--gtest_filter=DbcMetadataTest.*` runs offline using the same initialized fixture.
It checks catalog result ownership, product and driver metadata, and exception
propagation with strict mocks. `getCatalogs()` is exercised rather than treated as
unimplemented. Real getter values are checked by `DbcMetadataIntegrationTest.*`;
the offline tests do not validate a live connector's metadata.

To run those checks against a real, isolated MySQL test server on Windows:

```powershell
# Configure DB_HOST, DB_PORT, DB_USER and DB_PASSWORD for the test server first.
$env:SQLSTUDIO_DBC_LIVE_TESTS = '1'
.\testing\test-suite\compile.ps1 -Action test -SkipBuild -TestArgs '--gtest_filter=DbcGeneralIntegrationTest.*:DbcMetadataIntegrationTest.*'
Remove-Item Env:\SQLSTUDIO_DBC_LIVE_TESTS
```

The Windows runner and the top-level CMake test target use `DB_HOST`, `DB_PORT`,
`DB_USER`, and `DB_PASSWORD` (defaulting to `localhost`, `3306`, `root`, and an
empty password). The legacy `testing/test-suite` CMake target uses the equivalent
`TEST_DB_HOST`, `TEST_DB_PORT`, `TEST_DB_USER`, and `TEST_DB_PASSWORD` variables.
Both integration suites are skipped unless explicitly enabled; connection and SQL
errors fail the tests once enabled. The account needs permission to create and drop
schemas and tables for the general integration suite. Its tests create a uniquely
named `sqlstudio_dbc_<uuid>` schema and drop only that schema during teardown;
an interrupted process may leave it behind. The metadata integration suite is read-only.

Additional offline regressions can be selected with these suite filters:

- `CompactSidebar.*:HomeWelcomeLayout.*`: resource-backed layout checks, including
  Windows DPI/zoom and first-paint geometry. Run from the test-suite directory
  (the runner does this automatically); images resolve to the repository assets.
- `GRTCppModulesTest.*:GRTWrapperStandaloneTest.*`: module registration and wrapper
  contracts. Each module test registers its own prerequisites and runs independently.
- `SqlStringTest.*:SyncDiffTest.*:TableEditorInsertsBackendTest.*`: SQL formatting,
  catalog synchronization, and model INSERT storage. Sync comparisons normalize
  CRLF but compare the entire script; failures report sizes instead of SQL text.
- `WbModuleTestsForSqlStudioTest.*`: OS-name matching against the current
  application support list, not historical platform support.

## C/C++ Formatting

Use **clang-format 20.1.8**, matching the version pinned for CI:

```sh
python -m pip install -r tools/format-requirements.txt
python tools/check_format.py
```

On Windows (or anywhere `pwsh` is available), `run-clang-format.ps1` in the
repository root installs the pinned formatter into a cached virtual environment
on first use and then runs the same check:

```powershell
.\run-clang-format.ps1                          # changed and untracked files
.\run-clang-format.ps1 -Base origin/develop     # this branch, like CI
.\run-clang-format.ps1 -All                     # every non-ignored file
.\run-clang-format.ps1 -Base origin/develop -Fix  # reformat in place
.\run-clang-format.ps1 --fix-format             # same as -Fix
```

Both the script and `tools/check_format.py` also accept the GNU-style options
`--base REF`, `--all` and `--fix-format` (alias `--fix`).

The check covers whole C/C++ files changed since `HEAD` (staged and unstaged),
plus untracked files. To include committed changes on your branch, use:

```sh
python tools/check_format.py --base origin/develop
```

Pull requests run the same check against their target branch. Existing files
outside the change are not checked. Fix a reported file with
`clang-format -i path/to/file.cpp`, then rerun the check. Point your editor at the
same formatter version; pass `--clang-format /path/to/clang-format` to the check
if the executable is not on PATH.

`.clang-format-ignore` excludes vendored dependencies and generated/build
output. The `build/` directory also contains maintained sources, so it is not
excluded wholesale. For now, it also temporarily excludes every maintained source
folder (`backend/`, `frontend/`, `library/`, `modules/`, `plugins/`, `res/`,
`testing/`, `tools/`). To enforce formatting in a folder, remove its line. This check targets C/C++ files; Objective-C and Objective-C++
files are not included. Keep include ordering and per-file pointer alignment as
configured, and avoid unrelated repository-wide formatting changes.

## Build and Package Checks

Every pull request to `develop` or `master`, and every push to them, runs the
**Build** workflow (`.github/workflows/build.yml`). For each supported Linux
distribution (Ubuntu 22.04, 24.04, 26.04 and 26.10, Debian 12 and 13) it:

1. builds SqlStudio in a clean container of that distribution, because each one
   ships different library versions (libpython, GTK, ...),
2. packages the result as a `.deb` with `build/linux/make_deb.sh`,
3. installs the package in another clean container and checks that every
   binary and library resolves its shared libraries, that the bundled
   pyodbc module (used by the migration plugins) imports and that the bundled
   MySQL client connects to a MySQL server over TLS,
4. checks the package with that distribution's `lintian` (errors fail),
5. starts the application on a virtual display (`build/linux/smoke_test.sh`)
   and fails if it crashes, shows no main window, logs a Python traceback, or
   reports the distribution as unsupported.

On macOS (Apple Silicon, macOS 26 and later) the workflow builds
`SqlStudio.app` with Xcode, copies every library it loads into it
(`build/mac/bundle_app.py`) and packages it as a `.dmg`. A separate job
installs the `.dmg` on a fresh runner with Homebrew and the system-wide Python
hidden, checks that all library references stay inside the app, that the
signature is intact and that pyodbc imports, and runs the smoke test
(`build/mac/smoke_test.sh`).

On Windows (x64) the workflow builds the whole solution with the v143 toolset
and packages it twice: as a portable `.zip` that carries the Visual C++
runtime (`build/zip/make_zip.ps1`), and as an `.msi` built from that `.zip`
(`build/msi`). A separate job unpacks the `.zip` on a fresh runner with the
runner's Python taken off `PATH`, checks that every DLL resolves
(`build/windows/check_dll_deps.ps1`) and that pyodbc imports, and runs the
smoke test (`build/windows/smoke_test.ps1`). It then installs the `.msi`
silently, runs the same checks on the installed copy, and checks that an
uninstall removes it.

The packages and the smoke test results (screenshot and logs) are attached to
the workflow run as artifacts for 14 days. To try a package by hand, see
[testing/docker/linux-desktop](testing/docker/linux-desktop/README.md).

The third-party libraries (MySQL client, Connector/C++, GDAL, ANTLR runtime,
...) take a long time to build, so they are cached per distribution and
rebuilt only when `build/linux/list.txt`, `build/linux/build_3rdparty.sh` or
`build/linux/install_deps.sh` change (on Windows: `build/windows/libs.txt`,
`build/windows/build_3rdparty.ps1` or the toolset). A pull request can use caches saved by
its own earlier runs or on its target branch, so the first run after such a
change takes longer.

A daily `Cleanup` workflow deletes the caches of closed pull requests and the
artifacts of older runs of each pull request, keeping only the newest one. It
can also be started by hand from the Actions tab; by default it then only
lists what it would delete.

## Pull Request Guidelines

- Describe the problem and the change clearly.
- Link the related issue when applicable.
- Include screenshots for UI changes when helpful.
- Avoid unrelated refactoring in the same pull request.

## Discussions and Support

Project discussion happens in [GitHub Discussions](https://github.com/dante-d4f/sqlstudio/discussions).
If you are unsure whether something is a bug, feature request, or design topic,
start there.

## License

By submitting a contribution, you agree that your work will be distributed under
the same license as this project.
