STARTING

in a production environment, kopano-rest is deployed in combination with
konnect, kopano-apid and the provided kopano-mfr script.

for convenience, it's possible to start kopano-rest as follows:

python3 -m kopano_rest [REST_PORT=8000, NOTIFY_PORT=8001]

or as follows:

gunicorn3 'kopano_rest:RestAPI()'
gunicorn3 'kopano_rest:NotifyAPI()'

to profile requests (output in kopano.pstats):

gunicorn3 -c wsgi_profiler_conf.py 'kopano_rest:RestAPI()'
