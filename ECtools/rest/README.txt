STARTING

in a production environment, kopano-rest is deployed in combination with
konnect and the provider kopano-mfr script.

for convenience/development, it's possible to start kopano-rest using
gunicorn3 (or any WSGI server):

gunicorn3 'kopano_rest:RestAPI()'

to profile the last request (results in kopano.pstats):

gunicorn3 -c wsgi_profiler_conf.py 'kopano_rest:RestAPI()'
