#include <mosquitto/broker_plugin.h>
#include <libpq-fe.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    PGconn         *conn;
    char           *query;
    pthread_mutex_t lock;
} plugin_state_t;

MOSQUITTO_PLUGIN_DECLARE_VERSION(5);

static int cb_basic_auth(int event, void *event_data, void *userdata);

int mosquitto_plugin_init(mosquitto_plugin_id_t *identifier, void **userdata,
                          struct mosquitto_opt *options, int option_count)
{
    (void)options;
    (void)option_count;

    const char *pg_username = getenv("PG_USERNAME");
    const char *pg_password = getenv("PG_PASSWORD");
    const char *pg_host     = getenv("PG_HOST");
    const char *pg_port     = getenv("PG_PORT");
    const char *pg_database = getenv("PG_DATABASE");
    const char *pg_query    = getenv("PG_QUERY");

#define REQUIRE_ENV(name, var) \
    if (!(var)) { \
        mosquitto_log_printf(MOSQ_LOG_ERR, "pgsql-auth: " name " environment variable not set"); \
        return MOSQ_ERR_INVAL; \
    }
    REQUIRE_ENV("PG_USERNAME", pg_username)
    REQUIRE_ENV("PG_PASSWORD", pg_password)
    REQUIRE_ENV("PG_HOST",     pg_host)
    REQUIRE_ENV("PG_PORT",     pg_port)
    REQUIRE_ENV("PG_DATABASE", pg_database)
    REQUIRE_ENV("PG_QUERY",    pg_query)
#undef REQUIRE_ENV

    plugin_state_t *state = calloc(1, sizeof(plugin_state_t));
    if (!state) return MOSQ_ERR_NOMEM;

    if (pthread_mutex_init(&state->lock, NULL) != 0) {
        free(state);
        return MOSQ_ERR_NOMEM;
    }

    state->query = strdup(pg_query);
    if (!state->query) {
        pthread_mutex_destroy(&state->lock);
        free(state);
        return MOSQ_ERR_NOMEM;
    }

    const char *keywords[] = {
        "host", "port", "dbname", "user", "password", "application_name", NULL
    };
    const char *values[] = {
        pg_host, pg_port, pg_database, pg_username, pg_password,
        "mosquitto-pgsql-auth", NULL
    };

    state->conn = PQconnectdbParams(keywords, values, 0);
    if (PQstatus(state->conn) != CONNECTION_OK) {
        mosquitto_log_printf(MOSQ_LOG_ERR, "pgsql-auth: connection failed: %s",
                             PQerrorMessage(state->conn));
        PQfinish(state->conn);
        free(state->query);
        free(state);
        return MOSQ_ERR_UNKNOWN;
    }

    mosquitto_plugin_set_info(identifier, "mosquitto-pgsql-auth", "1.0.0");

    int rc = mosquitto_callback_register(identifier, MOSQ_EVT_BASIC_AUTH,
                                         cb_basic_auth, NULL, state);
    if (rc) {
        PQfinish(state->conn);
        pthread_mutex_destroy(&state->lock);
        free(state->query);
        free(state);
        return rc;
    }

    *userdata = state;
    mosquitto_log_printf(MOSQ_LOG_INFO, "pgsql-auth: initialized, connected to %s:%s/%s",
                         pg_host, pg_port, pg_database);
    return MOSQ_ERR_SUCCESS;
}

int mosquitto_plugin_cleanup(void *userdata, struct mosquitto_opt *options, int option_count)
{
    (void)options;
    (void)option_count;

    plugin_state_t *state = userdata;
    if (!state) return MOSQ_ERR_SUCCESS;

    if (state->conn) PQfinish(state->conn);
    pthread_mutex_destroy(&state->lock);
    free(state->query);
    free(state);
    return MOSQ_ERR_SUCCESS;
}

static int cb_basic_auth(int event, void *event_data, void *userdata)
{
    (void)event;

    const struct mosquitto_evt_basic_auth *auth = event_data;
    plugin_state_t *state = userdata;

    if (!auth->username || !auth->password) return MOSQ_ERR_AUTH;

    pthread_mutex_lock(&state->lock);

    /* Recover from a dropped connection before querying */
    if (PQstatus(state->conn) != CONNECTION_OK) {
        PQreset(state->conn);
        if (PQstatus(state->conn) != CONNECTION_OK) {
            mosquitto_log_printf(MOSQ_LOG_ERR, "pgsql-auth: reconnect failed: %s",
                                 PQerrorMessage(state->conn));
            pthread_mutex_unlock(&state->lock);
            return MOSQ_ERR_UNKNOWN;
        }
    }

    /* $1 = username, $2 = password — use parameterized query to prevent injection */
    const char *params[2] = { auth->username, auth->password };
    PGresult *res = PQexecParams(state->conn, state->query,
                                  2, NULL, params, NULL, NULL, 0);

    pthread_mutex_unlock(&state->lock);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        mosquitto_log_printf(MOSQ_LOG_ERR, "pgsql-auth: query error: %s",
                             PQerrorMessage(state->conn));
        PQclear(res);
        return MOSQ_ERR_UNKNOWN;
    }

    int authenticated = (PQntuples(res) > 0);
    PQclear(res);
    return authenticated ? MOSQ_ERR_SUCCESS : MOSQ_ERR_AUTH;
}
