import datetime

from .resource import (
    Resource, _date
)

# TODO prefer-timezone header
class ReminderResource(Resource):
    fields = {
        'eventId': lambda occ: occ.eventid,
        'changeKey': lambda occ: occ.item.changekey,
        'eventSubject': lambda occ: occ.subject,
        'eventStartTime': lambda occ: {'dateTime': _date(occ.start, True), 'timeZone': 'UTC'},
        'eventEndTime': lambda occ: {'dateTime': _date(occ.end, True), 'timeZone': 'UTC'},
        'eventLocation': lambda occ: occ.location,
        'reminderFireTime': lambda occ: {'dateTime': _date(occ.start-datetime.timedelta(minutes=occ.reminder_minutes), True), 'timeZone': 'UTC'}
    }

