import logging

import kopano


def test_logexc():
    log = logging.getLogger('test')

    stats = {}
    with kopano.log_exc(log, stats):
        kaboom
    assert stats['errors'] == 1
