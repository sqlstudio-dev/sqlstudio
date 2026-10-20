# MySQL compatibility

SqlStudio recognizes MySQL 5.6, 5.7, 8.0–8.4, 9.0–9.7, and 26.7.
The calendar version `26.7.0` is handled as a three-component server version;
it must not be truncated to a single-digit major version.

The newer release lines use the existing MySQL 8.0 SQL vocabulary and syntax
highlighter. This provides compatibility for common queries and table editing,
but does not implement every SQL statement or data type introduced after 8.0.
Administration configuration catalogs, complete schema synchronization, and
dump/restore compatibility require separate validation for each server release.
Use server-compatible MySQL client tools for dump and restore.

The connection warning and feature gates share the C++ release policy in
`base::MySQLSymbolInfo::isSupportedServerVersion`. Keep the Python policy in
`studio.utils.Version.is_supported_mysql_version` synchronized with it.
Unknown release lines and MariaDB are not automatically classified as supported
MySQL servers.

## Automated checks

Using an already configured build with its normal dependency bundle:

```sh
cmake -S . -B build/output -DBUILD_TESTING=ON
cmake --build build/output --target mysql-compatibility-tests -j4
build/output/testing/mysql-compatibility-tests
python3 -m unittest discover -s testing/python -p test_mysql_versions.py -v
```

If a bundled GTest CMake package reports a duplicated, nonexistent include path,
configure with `-DCMAKE_DISABLE_FIND_PACKAGE_GTest=ON` to use the existing
pkg-config/manual discovery fallback.

The focused native tests cover release recognition, feature gates, SQL
vocabulary, calendar-version comments, and common SQL statements at versions
8.2, 8.4, 9.7, and 26.7.
They do not require a running GUI.

The optional live test uses the same Connector/C++ dependency as the application.
It checks authentication, query execution, JSON, temporary-table creation,
alter/update/delete, metadata queries used by the schema browser, EXPLAIN JSON,
and parsing the server's `SHOW CREATE TABLE` output. Its table is local to the
test connection and disappears when that connection closes.

Start the development containers as described in
[docker/databases/README.md](docker/databases/README.md), then run:

```sh
SQLSTUDIO_MYSQL_TEST_URI=tcp://127.0.0.1:33082 \
  build/output/testing/mysql-compatibility-tests
SQLSTUDIO_MYSQL_TEST_URI=tcp://127.0.0.1:33060 \
  build/output/testing/mysql-compatibility-tests
```

The live test defaults to the development user/password/schema `sqlstudio`.
Override these with `SQLSTUDIO_MYSQL_TEST_USER`,
`SQLSTUDIO_MYSQL_TEST_PASSWORD`, and `SQLSTUDIO_MYSQL_TEST_SCHEMA`.
The test prints the actual server version because the `mysql-latest` container
tag can change.

The live Connector/C++ smoke test passed on Linux against MySQL **8.2.0** and
**26.7.0**. The 26.7 development account used `caching_sha2_password`.
These results cover the automated operations above, not full GUI or
administration certification.

## GUI verification

After rebuilding, test both the 8.2 and 26.7 connections:

1. Test Connection and open the SQL editor; neither should show the unsupported
   server warning.
2. Expand schemas, tables, columns, indexes, procedures, and functions.
3. Execute a SELECT and edit a result row in a disposable table.
4. Open Alter Table, preview and apply a column change, and refresh the schema.
5. Check highlighting and completion for SELECT, JSON functions, CTEs, and window
   functions.
6. Open Server Status. MySQL 8.0.22 and newer use `SHOW REPLICA STATUS` with
   `Replica_IO_State` and `Source_Host`; older servers use the legacy names.

Full desktop interaction is separate from the headless automated checks.
