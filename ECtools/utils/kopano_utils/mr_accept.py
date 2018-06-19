#!@PYTHON@
import collections
from datetime import datetime, timedelta
import sys
import kopano

from MAPI.Tags import PR_DISPLAY_TYPE_EX, PR_EMS_AB_ROOM_CAPACITY

RECURRENCE_AVAILABILITY_RANGE = 180 # days
DT_EQUIPMENT = 8

# XXX ZCP-9901 still relevant without outlook?

if sys.hexversion >= 0x03000000:
    def _decode(s):
        return s
else: # pragma: no cover
    def _decode(s):
        return s.decode(getattr(sys.stdin, 'encoding', 'utf8') or 'utf8')

def capacity(user): # XXX pyko?
    """ equipment resources can be overbooked up to N times """

    disptype = user.get(PR_DISPLAY_TYPE_EX)
    capacity = user.get(PR_EMS_AB_ROOM_CAPACITY)

    if disptype == DT_EQUIPMENT and capacity:
        return capacity
    else:
        return 1

class Marker(object): # XXX kill?
    def __init__(self, occurrence):
        self.occurrence = occurrence

def conflict_occurrences(user, item):
    """ item occurrences which overlap (too much) with calendar """

    start = item.start
    end = start + timedelta(RECURRENCE_AVAILABILITY_RANGE)

    # determine occurrences which might conflict
    item_occs = list(item.occurrences(start, end))
    cal_occs = list(user.calendar.occurrences(start, end))
    cal_item = item.meetingrequest.calendar_item
    cal_occs = [occ for occ in cal_occs if occ.item != cal_item]

    # create start/end markers for each occurrence
    dt_markers = collections.defaultdict(list)
    for o in item_occs + cal_occs:
        marker = Marker(o)
        if o.start <= o.end:
            dt_markers[o.start].append(marker)
            dt_markers[o.end].append(marker)

    # loop over sorted markers, maintaining running set
    max_overlap = capacity(user)
    conflict_markers = set()
    running = set()
    for day in sorted(dt_markers):
        for marker in dt_markers[day]:
            if marker in running:
                running.remove(marker)
            else:
                running.add(marker)

        # if too much overlap, check if item is involved
        if len(running) > max_overlap:
            for marker in running:
                if marker.occurrence.item is item:
                    conflict_markers.add(marker)

    return [m.occurrence for m in conflict_markers]

def conflict_message(occurrences):
    lines  = ['The requested time slots are unavailable on the following dates:', '']
    for occ in occurrences:
        lines.append('%s - %s' % (occ.start, occ.end))
    return '\n'.join(lines)

def main():
    args = [_decode(arg) for arg in sys.argv[1:]]
    username, config = args[:2]

    server = kopano.Server()
    user = server.user(username)
    autoaccept = user.autoaccept

    if len(args) > 2:
        items = [user.item(args[2])]
    else:
        items = []
        for item in user.inbox:
            mr = item.meetingrequest
            if((mr.is_request and mr.response_requested and not mr.processed) or \
               (mr.is_cancellation and not mr.processed)):
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
    user.freebusy.publish(now - timedelta(7), now + timedelta(180))

if __name__ == '__main__':
    main()
