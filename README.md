# mosquitto-pgsql-auth

Authentication plugin for the [Mosquitto](https://mosquitto.org/) MQTT broker that delegates client authentication to a PostgreSQL database. When a client connects with a username and password, the plugin runs a configurable parameterized SQL query against your database. If the query returns at least one row the client is granted access; otherwise the connection is rejected. The query runs as `$1 = username`, `$2 = password`, so it works with any schema - plain-text passwords, bcrypt hashes via `pgcrypto`, application-level user tables, or join-based role checks.

libpq is statically linked into the `.so`, so the plugin has no runtime dependencies beyond what Mosquitto itself requires.

## Configuration

Add the following to `mosquitto.conf`:

```
plugin /path/to/mosquitto_pgsql_auth.so
allow_anonymous false
```

The plugin is configured entirely through environment variables passed to the Mosquitto process:

| Variable      | Description                                                     |
|---------------|-----------------------------------------------------------------|
| `PG_HOST`     | PostgreSQL server hostname or IP address                        |
| `PG_PORT`     | PostgreSQL server port (typically `5432`)                       |
| `PG_DATABASE` | Database name                                                   |
| `PG_USERNAME` | Database user the plugin connects as                            |
| `PG_PASSWORD` | Password for the database user                                  |
| `PG_QUERY`    | SQL query to run - must use `$1` (username) and `$2` (password) |

All six variables are required. The plugin will refuse to start and log an error if any are missing.

## Schema

This plugin only executes the query you provide - it does not create or manage any database schema. You are responsible for setting up the table structure and populating it before starting Mosquitto.

If you are running PostgreSQL via Docker, place your schema as a `.sql` file in the container's `/docker-entrypoint-initdb.d/` directory and it will be executed automatically on first startup:

```yaml
services:
  postgres:
    image: postgres:17
    volumes:
      - ./init.sql:/docker-entrypoint-initdb.d/init.sql
```

## Example queries

**Plain-text passwords** - simplest possible schema:

```sql
-- Schema
CREATE TABLE mqtt_users (
    username TEXT PRIMARY KEY,
    password TEXT NOT NULL
);

-- PG_QUERY
SELECT 1 FROM mqtt_users WHERE username = $1 AND password = $2;
```

**Hashed passwords with pgcrypto** - passwords stored as bcrypt hashes:

```sql
-- Schema
CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE mqtt_users (
    username TEXT PRIMARY KEY,
    password_hash TEXT NOT NULL
);

-- Insert a user: INSERT INTO mqtt_users VALUES ('alice', crypt('secret', gen_salt('bf')));

-- PG_QUERY
SELECT 1 FROM mqtt_users WHERE username = $1 AND password_hash = crypt($2, password_hash);
```

**Role-based access** - only users with a specific role may connect:

```sql
-- PG_QUERY
SELECT 1 FROM mqtt_users
WHERE username = $1
  AND password = $2
  AND role = 'mqtt_client';
```

**Multiple tables** - authenticate human users and IoT devices from separate tables:

```sql
-- PG_QUERY
SELECT 1 FROM users      WHERE username  = $1 AND password = $2
UNION ALL
SELECT 1 FROM iot_devices WHERE device_id = $1 AND password = $2
LIMIT 1;
```
