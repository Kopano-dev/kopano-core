#!/usr/bin/python

# display calendar occurrences in familiar calendar widget (slow as we don't filter on range yet)

# usage: ./goocal.py username

import pygtk
import gtk
import datetime
import sys
import goocalendar
import kopano

class ZCalendar:
    def __init__(self):
        self.window = gtk.Window(gtk.WINDOW_TOPLEVEL)
        event_store = goocalendar.EventStore()
        calendar = goocalendar.Calendar(event_store)
        for occurrence in kopano.User(sys.argv[1]).calendar.occurrences():
            event = goocalendar.Event(caption=occurrence.subject, start=occurrence.start, end=occurrence.end)
            event_store.add(event)
        calendar.show()
        self.window.add(calendar)
        self.window.show()

if __name__ == "__main__":
    ZCalendar()
    gtk.main()
