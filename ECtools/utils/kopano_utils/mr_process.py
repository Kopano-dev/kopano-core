#!@PYTHON@
# SPDX-License-Identifier: AGPL-3.0-or-later
from datetime import datetime, timedelta
import sys

import kopano


def get_sender(item):
    '''
    Retrieves the sender from either PR_SENDER_* (item.sender) or when a
    delegate has approved the meeting request on behalf of someone
    the PR_SENT_REPRESENTING_* properties are used.
    '''

    if item.from_.entryid != item.sender.entryid:
        fromname = "'{}' on behalf of '{}'".format(item.sender.name, item.from_.name)
    else:
        fromname = "'{}'".format(item.sender.name)

    return fromname


def main():
    username, config_file, entryid = sys.argv[1:]

    config = kopano.Config(filename=config_file)
    server = kopano.server(config=config)

    user = server.user(username)
    item = user.item(entryid)
    mr = item.meetingrequest

    if mr.is_request:
        server.log.debug("Accepting meeting request tentative from %s", get_sender(item))
        mr.accept(tentative=True, response=False)
    elif mr.is_response:
        server.log.debug("Processing meeting request response from %s", get_sender(item))
        mr.process_response()
    elif mr.is_cancellation:
        server.log.debug("Processing meeting request cancellation from %s", get_sender(item))
        mr.process_cancellation()

    now = datetime.now()

    try:
        server.log.debug("Updating freebusy")
        user.freebusy.publish(now - timedelta(7), now + timedelta(180))
    except kopano.errors.NotFoundError as e:
        server.log.error("Unable to publish freebusy information: %s", str(e))

if __name__ == '__main__':
    main() # pragma: no cover
