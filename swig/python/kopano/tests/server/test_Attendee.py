from datetime import datetime


def test_str(user, appointment):
    appointment.to = user
    appointment.subject = 'daily'
    appointment.start = datetime(2018, 7, 7, 9)
    appointment.end = datetime(2018, 7, 7, 9, 30)

    assert str(next(appointment.attendees())) == 'Attendee({})'.format(user.name)
