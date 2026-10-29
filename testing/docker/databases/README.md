# SqlStudio database containers

This Compose project provides five isolated database servers for manual and compatibility testing:

| Service | Image | Host port |
| --- | --- | ---: |
| `mysql-8-0` | `mysql:8.0` | `33080` |
| `mysql-8-2` | `mysql:8.2` | `33082` |
| `mysql-latest` | `mysql:latest` | `33060` |
| `mariadb-10-11` | `mariadb:10.11` | `33011` |
| `mariadb-latest` | `mariadb:latest` | `33070` |

The `mysql-8-0` service follows the newest published 8.0.x image. Use `docker compose build --pull mysql-8-0` to refresh its base image before starting it. It has its own persistent volume and does not share data with the other servers.

MariaDB 10.11 is included as the common long-term-support compatibility target. The `latest` tags intentionally move when new stable releases are published.

## Start the servers

From this directory:

```powershell
Copy-Item .env.example .env
docker compose up --build -d
docker compose ps
```

To start only one server:

```powershell
docker compose build --pull mysql-8-0
docker compose up -d --wait mysql-8-0
```

The default application credentials are:

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

## Stop or reset

Stop the containers while preserving their databases:

```powershell
docker compose down
```

Delete the containers and all five database volumes:

```powershell
docker compose down --volumes
```

The volume-removal command permanently deletes the databases stored by this Compose project.
