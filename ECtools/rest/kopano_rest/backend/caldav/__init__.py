# SPDX-License-Identifier: AGPL-3.0-or-later

from .calendar import CalendarResource

class Resource(object):
    def __init__(self, options):
        self.options = options

EventResource = Resource
