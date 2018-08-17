#!@PYTHON@
# SPDX-License-Identifier: AGPL-3.0-or-later
from datetime import datetime, timedelta
import sys
import kopano

if sys.hexversion >= 0x03000000:
    def _decode(s):
        return s
else: # pragma: no cover
    def _decode(s):
        return s.decode(getattr(sys.stdin, 'encoding', 'utf8') or 'utf8')

def main():
    username, config, entryid = [_decode(arg) for arg in sys.argv[1:]]

    server = kopano.Server()
    user = server.user(username)
    item = user.item(entryid)
    mr = item.meetingrequest

    if mr.is_request:
        mr.accept(tentative=True, response=False)
    elif mr.is_response:
        mr.process_response()
    elif mr.is_cancellation:
        mr.process_cancellation()

    now = datetime.now()
    user.freebusy.publish(now - timedelta(7), now + timedelta(180))

if __name__ == '__main__':
    main() # pragma: no cover
