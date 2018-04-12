STARTING

in a production environment, kopano-rest is deployed in combination with
konnect, kopano-apid and the provided kopano-mfr script.

for convenience, it's possible to start kopano-rest as follows (no
notification/subscription support as of now):

python3 -m kopano_rest

or as follows:

gunicorn3 'kopano_rest:RestAPI()'

to profile requests (output in kopano.pstats):

gunicorn3 -c wsgi_profiler_conf.py 'kopano_rest:RestAPI()'
