# MySQL compatibility

SqlStudio recognizes MySQL 5.6, 5.7, 8.0–8.4, 9.0–9.7, 26.7, and 26.10.
The calendar version `26.7.0` is handled as a three-component server version;
it must not be truncated to a single-digit major version.

The newer release lines use the MySQL 8.0 grammar with the incremental syntax
support listed below. This provides compatibility for common queries and table
editing, but does not implement every SQL statement or data type introduced
after 8.0.
Administration configuration catalogs, complete schema synchronization, and
dump/restore compatibility require separate validation for each server release.
Use server-compatible MySQL client tools for dump and restore.

The connection warning and feature gates share the C++ release policy in
`base::MySQLSymbolInfo::isSupportedServerVersion`. Keep the Python policy in
`studio.utils.Version.is_supported_mysql_version` synchronized with it.
Unknown release lines and MariaDB are not automatically classified as supported
MySQL servers.

## Incremental grammar support

| Syntax | Minimum server version |
| --- | --- |
| `JSON_VALUE` with `RETURNING`, `ON EMPTY`, and `ON ERROR` | 8.0.21 |
| `JSON_VALUE ... RETURNING YEAR` | 8.0.22 |
| `SHOW REPLICA STATUS`, `SHOW REPLICAS`, `START/STOP/RESET REPLICA` | 8.0.22 |
| `CHANGE REPLICATION SOURCE TO` and common `SOURCE_*` options | 8.0.23 |
| `INTERSECT` and `EXCEPT`, including `ALL`/`DISTINCT` | 8.0.31 |
| `SHOW BINARY LOG STATUS`, `RESET BINARY LOGS AND GTIDS` | 8.2.0 |

`INTERSECT` has higher precedence than `UNION` and `EXCEPT`. Parenthesized
queries can override this grouping. `ON EMPTY` must precede `ON ERROR` in
`JSON_VALUE`; its return type is checked by a dedicated grammar rule.

The lexer and parser enforce these version boundaries. Keyword tables preserve
the distinction between reserved words (such as `INTERSECT`) and nonreserved
words (such as `REPLICA`, `RETURNING`, and `SOURCE_HOST`). Completion and
identifier quoting use the version-specific tables. Syntax coloring shows the
implemented modern vocabulary; syntax validation uses the exact server version.

Modify `library/parsers/grammars/MySQLLexer.g4` and `MySQLParser.g4`, then build
with Java and the configured ANTLR 4.13.2 JAR. CMake regenerates the C++ lexer,
parser, listeners, and visitors. Keep incremental keyword entries in
`res/server/keyword_list80_updates.h` synchronized with lexer introduction
versions. Generated parser files are build artifacts.

MySQL 26.10 release recognition and local grammar tests are included; live
26.10 server validation is still pending (`mysql:26.10.0` was unavailable
on Docker Hub when checked). Its `VERSION()` interface retains
the three-component compatibility version ([release notes](https://dev.mysql.com/doc/refman/26.10/en/mysql-nutshell.html)).

This increment does not add JSON duality view annotations, `VECTOR`, `QUALIFY`, `TABLESAMPLE`, every newer
replication option, or full rejection of syntax removed from newer servers.
Their presence in a server keyword catalog alone is not sufficient to define
their grammar or feature availability.

References: [set operations](https://dev.mysql.com/doc/refman/8.0/en/set-operations.html),
[JSON_VALUE](https://dev.mysql.com/doc/refman/8.0/en/json-search-functions.html#function_json-value),
[replication source options](https://dev.mysql.com/doc/refman/8.0/en/change-replication-source-to.html),
and [MySQL 8.2 syntax changes](https://dev.mysql.com/doc/relnotes/mysql/8.4/en/news-8-2-0.html).

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
8.2, 8.4, 9.7, 26.7, and 26.10. Grammar tests additionally cover feature introduction
boundaries, set-operation precedence, malformed statements, nonreserved
identifiers, and replication statement classification.
They do not require a running GUI.

The optional live test uses the same Connector/C++ dependency as the application.
It checks authentication, query execution, JSON, temporary-table creation,
alter/update/delete, metadata queries used by the schema browser, EXPLAIN JSON,
and parsing the server's `SHOW CREATE TABLE` output. Its table is local to the
test connection and disappears when that connection closes.
It also compares set-operation and `JSON_VALUE` results with the live server.
Replication checks only issue read-only `SHOW` statements; an insufficient
privilege error is accepted, but a syntax error fails the check. Replication
configuration and reset statements are tested only by the local parser.

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
5. Check highlighting and completion for SELECT, JSON_VALUE, INTERSECT/EXCEPT,
   CTEs, window functions, and modern replication statements.
6. Open Server Status. MySQL 8.0.22 and newer use `SHOW REPLICA STATUS` with
   `Replica_IO_State` and `Source_Host`; older servers use the legacy names.

Full desktop interaction is separate from the headless automated checks.
