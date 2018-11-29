# SPDX-License-Identifier: CC0-1.0
import cProfile
import pstats
import io
import logging
import os
import time

# this code was written by Max Klymyshn and has been put into the public domain according to:
# https://creativecommons.org/publicdomain/zero/1.0/

# as described here:
# https://medium.com/@maxmaxmaxmax/measuring-performance-of-python-based-apps-using-gunicorn-and-cprofile-ee4027b2be41

PROFILE_LIMIT = int(os.environ.get("PROFILE_LIMIT", 30))
PROFILER = bool(int(os.environ.get("PROFILER", 1)))

print("""
# ** USAGE:
$ PROFILE_LIMIT=100 gunicorn -c ./wsgi_profiler_conf.py wsgi
# ** TIME MEASUREMENTS ONLY:
$ PROFILER=0 gunicorn -c ./wsgi_profiler_conf.py wsgi
""")


def profiler_enable(worker, req):
    worker.profile = cProfile.Profile()
    worker.profile.enable()
    worker.log.info("PROFILING %d: %s", worker.pid, req.uri)


def profiler_summary(worker, req):
    s = io.StringIO()
    worker.profile.disable()
    ps = pstats.Stats(worker.profile, stream=s)
    ps.dump_stats('kopano_rest.pstats')

    ps = ps.sort_stats('cumulative')
    ps.print_stats(PROFILE_LIMIT)

    logging.error("\n[%d] [INFO] [%s] URI %s", worker.pid, req.method, req.uri)
    logging.error("[%d] [INFO] %s", worker.pid, s.getvalue())


def pre_request(worker, req):
    worker.start_time = time.time()
    if PROFILER is True:
        profiler_enable(worker, req)


def post_request(worker, req, *args):
    total_time = time.time() - worker.start_time
    logging.error("\n[%d] [INFO] [%s] Load Time: %.3fs\n",
        worker.pid, req.method, total_time)
    if PROFILER is True:
        profiler_summary(worker, req)
