#!/usr/bin/env python
import sys
import kopano

def main():
    username, cfg, entryid = [arg.decode('utf8') for arg in sys.argv[1:]]
    user = kopano.user(username)
    item = user.item(entryid)

    mr = item.meetingrequest

    if mr.is_request:
        mr.accept(tentative=True, response=False)

    elif mr.is_response:
        mr.process_response()

    elif mr.is_cancellation:
        mr.process_cancellation()

    # XXX publish freebusy

if __name__ == '__main__':
    main()

