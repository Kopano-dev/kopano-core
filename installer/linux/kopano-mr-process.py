#!/usr/bin/env python
from datetime import datetime, timedelta
import sys
import kopano

def main():
    username, config, entryid = [arg.decode('utf8') for arg in sys.argv[1:]]

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
    main()
