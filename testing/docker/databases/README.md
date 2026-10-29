# SqlStudio database containers

This Compose project provides nine isolated database servers for manual and compatibility testing, including QuestDB, ClickHouse, InfluxDB, and TimescaleDB for real-time analytics and time-series workloads:

| Service | Image | Host port |
| --- | --- | ---: |
| `mysql-8-0` | `mysql:8.0` | `33080` |
| `mysql-8-2` | `mysql:8.2` | `33082` |
| `mysql-latest` | `mysql:latest` | `33060` |
| `mariadb-10-11` | `mariadb:10.11` | `33011` |
| `mariadb-latest` | `mariadb:latest` | `33070` |
| `questdb` | `questdb/questdb:latest` | `9000` (HTTP/Web Console), `8812` (PostgreSQL wire) |
| `clickhouse` | `clickhouse/clickhouse-server:latest` | `18123` (HTTP), `19000` (native TCP) |
| `influxdb` | `influxdb:2` | `8086` (HTTP/Web UI) |
| `timescaledb` | `timescale/timescaledb:latest-pg17` | `5433` (PostgreSQL) |

All services use official images directly; no local Dockerfiles or build steps are needed.

The `mysql-8-0` service follows the newest published 8.0.x image. Use `docker compose pull mysql-8-0` to refresh its image before starting it. It has its own persistent volume and does not share data with the other servers.

MariaDB 10.11 is included as the common long-term-support compatibility target. The `latest` tags intentionally move when new stable releases are published.

## Start the servers

From this directory:

```powershell
Copy-Item .env.example .env
docker compose pull
docker compose up -d
docker compose ps
```

To start only one server:

```powershell
docker compose pull mysql-8-0
docker compose up -d --wait mysql-8-0
```

The default MySQL and MariaDB application credentials are:

- Database: `sqlstudio`
- User: `sqlstudio`
- Password: `sqlstudio`
- Root password: `root`

Change these development-only defaults in `.env` before exposing any port beyond localhost.

Example connection to MySQL 8.0.x:

```text
Host: 127.0.0.1
Port: 33080
User: sqlstudio
Password: sqlstudio
Default schema: sqlstudio
```

## Real-time databases

Start just the real-time databases, waiting for their health checks:

```powershell
docker compose pull questdb clickhouse influxdb timescaledb
docker compose up -d --wait questdb clickhouse influxdb timescaledb
```

All four use published images directly, independent persistent volumes, and localhost-only published ports. Configure their ports and development credentials in `.env` using the `QUESTDB_*`, `CLICKHOUSE_*`, `INFLUXDB_*`, and `TIMESCALEDB_*` variables in `.env.example`.

### QuestDB

- Web Console, REST SQL API, and HTTP line-protocol ingestion: <http://127.0.0.1:9000>
- PostgreSQL wire endpoint: `127.0.0.1:8812`, database `qdb`
- Default user/password for both HTTP and PostgreSQL wire: `sqlstudio` / `sqlstudio`
- The internal health endpoint on port `9003` is not published to the host.

Test a SQL query over HTTP (adjust credentials and ports if changed in `.env`):

```powershell
curl.exe --fail --user sqlstudio:sqlstudio --get "http://127.0.0.1:9000/exec" --data-urlencode "query=SELECT 1"
```

QuestDB speaks the PostgreSQL wire protocol, not the MySQL protocol. Its database name is `qdb`, rather than the MySQL/MariaDB default schema.

### ClickHouse

- HTTP SQL endpoint: <http://127.0.0.1:18123>
- Native TCP endpoint: `127.0.0.1:19000`
- Default database: `sqlstudio`
- Default user/password: `sqlstudio` / `sqlstudio`

Test a SQL query over HTTP:

```powershell
curl.exe --fail --user sqlstudio:sqlstudio "http://127.0.0.1:18123/?database=sqlstudio" --data-binary "SELECT 1"
```

Or use the native client inside the container (this reads the configured credentials):

```powershell
docker compose exec clickhouse sh -c 'clickhouse-client --user "$CLICKHOUSE_USER" --password "$CLICKHOUSE_PASSWORD" --database "$CLICKHOUSE_DB" --query "SELECT 1"'
```

### InfluxDB

- Web UI and HTTP API: <http://127.0.0.1:8086>
- Default user/password: `sqlstudio` / `sqlstudio`
- Default organization and bucket: `sqlstudio`
- Default development API token: `sqlstudio-dev-token`

The image is explicitly kept on InfluxDB **2.x**, which includes the web UI, Flux queries, and bucket/token setup. It is not InfluxDB 3. Both data and CLI configuration have persistent volumes.

The user, organization, bucket, and token are initialized only on the first start with empty storage. Changing the initialization variables does not update an existing installation. Change credentials through the UI/API instead. Replace the development token and password before exposing the service beyond localhost.

Test authenticated bucket access (adjust the token if changed in `.env`):

```powershell
curl.exe --fail --header "Authorization: Token sqlstudio-dev-token" "http://127.0.0.1:8086/api/v2/buckets?org=sqlstudio"
```

### TimescaleDB

- PostgreSQL endpoint: `127.0.0.1:5433`
- Default database: `sqlstudio`
- Default user/password: `sqlstudio` / `sqlstudio`

The image tracks TimescaleDB releases for PostgreSQL **17**, avoiding implicit PostgreSQL major-version changes on an existing data volume. The image enables the `timescaledb` extension in the initial database. PostgreSQL initialization credentials apply only when the data volume is empty.

Test the extension using the bundled PostgreSQL client:

```powershell
docker compose exec timescaledb sh -c 'psql --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" --command "\dx timescaledb"'
```

These containers provide database services only; they do not add new database drivers to SqlStudio. Use compatible clients or the HTTP APIs. Existing MySQL connections cannot connect to these endpoints.

## Stop or reset

Stop the containers while preserving their databases:

```powershell
docker compose down
```

Delete the containers and all database data/configuration volumes:

```powershell
docker compose down --volumes
```

The volume-removal command permanently deletes the databases stored by this Compose project.
