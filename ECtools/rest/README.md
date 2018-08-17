# Kopano-REST

Kopano-REST provides a general REST web service for Kopano. It aims to be largely compatible with Microsoft Graph. See `COMPAT.MD` for compatibility details.

In a production environment, Kopano-REST is deployed together with konnect, kopano-apid and the provided kopano-mfr script.

Kopano-REST requires python3.

## Running

    python3 kopano-mfr.py

## Parameters

The `--socket-path` parameter specifies where kopano-mfr should create its UNIX sockets (default `/var/run/kopano').

The `--workers` parameter specifies how many worker processes to utilize (default 8).

The `--insecure` parameter disables checking of SSL certificates for subscription webhooks.

The `--enable-auth-basic` parameter enables basic authentication (by default on bearer authentication is enabled).

The `--with-metrics` parameter adds an additional worker process to collect usage metrics (using Prometheus).

The `--metrics-listen` parameter specifies where the metrics worker can be reached.

## Development

Kopano-REST consists of separate WSGI applications. The kopano-mfr scripts runs both together, in a scalable way.

During development, it is sometimes easier to use the applications directly, and without needing access tokens (so using basic authentication).

    gunicorn3 'kopano_rest:RestAPI()'
    gunicorn3 'kopano_rest:NotifyAPI()'

In fact, gunicorn is not even necessary, but comes with many useful options, such as automatic reloading. The following also works:

    python3 -m kopano_rest [REST_PORT=8000, NOTIFY_PORT=8001]
