import datetime

from .resource import (
    Resource, _tzdate
)

class ReminderResource(Resource):
    fields = {
        'eventId': lambda occ: occ.eventid,
        'changeKey': lambda occ: occ.item.changekey,
        'eventSubject': lambda occ: occ.subject,
        'eventStartTime': lambda req, occ: _tzdate(occ.start, req),
        'eventEndTime': lambda req, occ: _tzdate(occ.end, req),
        'eventLocation': lambda occ: occ.location,
        'reminderFireTime': lambda req, occ: _tzdate(occ.start-datetime.timedelta(minutes=occ.reminder_minutes), req),
    }

