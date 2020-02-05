#!@PYTHON@
# SPDX-License-Identifier: AGPL-3.0-or-later
from datetime import datetime, timedelta
import sys

import kopano

def main():
    username, config_file, entryid = sys.argv[1:]

    config = kopano.Config(filename=config_file)
    server = kopano.server(config=config)

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

    try:
        user.freebusy.publish(now - timedelta(7), now + timedelta(180))
    except kopano.errors.NotFoundError as e:
        print("Unable to publish freebusy information: " + str(e), file=sys.stderr)

if __name__ == '__main__':
    main() # pragma: no cover
