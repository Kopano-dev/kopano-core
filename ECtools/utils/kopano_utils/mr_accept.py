#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later

import collections
from datetime import datetime, timedelta
import os
import sys
import kopano

from MAPI.Tags import PR_DISPLAY_TYPE_EX, PR_EMS_AB_ROOM_CAPACITY

RECURRENCE_AVAILABILITY_RANGE = 180  # days
DT_EQUIPMENT = 8

# XXX ZCP-9901 still relevant without outlook?


def capacity(user):  # XXX pyko?
    """ equipment resources can be overbooked up to N times """

    capacity = os.getenv('KOPANO_TEST_CAPACITY')
    if capacity:
        return int(capacity)

    disptype = user.get(PR_DISPLAY_TYPE_EX)
    capacity = user.get(PR_EMS_AB_ROOM_CAPACITY)

    if disptype == DT_EQUIPMENT and capacity:
        return capacity
    else:
        return 1


def conflict_occurrences(user, item):
    """Item occurrences which overlap (too much) with calendar.

    Args:
        user (User): kopano user object.
        item (Item): kopano item object.

    Returns:
        List[Item]: conflicts list.
    """
    start = item.start + timedelta(days=-1)
    end = start + timedelta(RECURRENCE_AVAILABILITY_RANGE)

    # Determine occurrences which might conflict.
    item_occs = list(item.occurrences(start, end))
    cal_item = item.meetingrequest.calendar_item
    cal_occs = [
        occ for occ in user.calendar.occurrences(start, end) if occ.item != cal_item
    ]
    user_capacity = capacity(user)
    occs = cal_occs + item_occs

    conflicts = []

    # Check based on the datetime range.
    for occ in occs:
        if occ.item is item:
            continue

        # Check time range.
        if (item.start > occ.end and item.end > occ.end) or \
           (item.start < occ.start and item.end < occ.start):
            continue
        conflicts.append(occ)

        # Check capacity threshold.
        if len(conflicts) >= user_capacity:
            break

    if len(conflicts) >= user_capacity:
        return [occ.item for occ in conflicts]
    return []


def conflict_message(occurrences):
    lines = ['The requested time slots are unavailable on the following dates:', '']
    for occ in occurrences:
        lines.append('%s - %s' % (occ.start, occ.end))
    return '\n'.join(lines)


def accept(username, server, entryid=None):
    user = server.user(username)
    autoaccept = user.autoaccept

    if entryid:
        items = [user.item(entryid)]
    else:
        items = []
        for item in user.inbox:
            mr = item.meetingrequest
            if (mr.is_request and mr.response_requested and not mr.processed) or (mr.is_cancellation and not mr.processed):
                items.append(item)

    for item in items:
        mr = item.meetingrequest

        if mr.is_request:
            decline_message = None

            if not autoaccept.recurring and item.recurring:
                decline_message = "Recurring meetings are not allowed"

            elif not autoaccept.conflicts:
                conflicts = conflict_occurrences(user, item)
                if conflicts:
                    decline_message = conflict_message(conflicts)

            if decline_message:
                mr.decline(message=decline_message)
            else:
                mr.accept(add_bcc=True)

        elif mr.is_cancellation:
            mr.process_cancellation(delete=True)

    now = datetime.now()
    try:
        user.freebusy.publish(now - timedelta(7), now + timedelta(180))
    except kopano.errors.NotFoundError as e:
        print("Unable to publish freebusy information: " + str(e), file=sys.stderr)


def main():
    args = sys.argv[1:]
    username, config_file = args[:2]

    entryid = args[2] if len(args) > 2 else None
    config = kopano.Config(filename=config_file)
    server = kopano.server(config=config)

    accept(username, server, entryid)


if __name__ == '__main__':  # pragma: no cover
    main()
