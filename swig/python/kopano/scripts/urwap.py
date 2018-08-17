#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-only

# 100-line "graphical" email client

# usage: ./urwap.py -h

from itertools import islice
import sys
import urwid
import kopano


FOOT1 = 'J=down, K=up, R=reply, D=delete, N=refresh, Q=quit'
FOOT2 = 'Ctrl-X=send, Ctrl-Y=ESC=cancel'
PALETTE = [
    ('focus', 'black', 'dark cyan'),
    ('headfoot', 'black', 'dark cyan'),
]

class MailBox(urwid.ListBox):
    def __init__(self, folder, mail):
        self.folder = folder
        self.mail = mail
        self.body = urwid.SimpleListWalker([])
        super(MailBox, self).__init__(self.body)
        self.refresh()

    def refresh(self):
        self.items = list(islice(self.folder.items(), 0, 100)) # load max 100 items
        self.body[:] = [urwid.AttrMap(MailHeader(item), None, 'focus') for item in self.items]
        if self.items:
            self.set_focus(0)
            self.mail.set_item(self.items[0])

    def keypress(self, size, input):
        focus_widget, idx = self.get_focus()
        if focus_widget and input in ('t', 'up') and idx > 0:
            self.set_focus(idx-1)
            self.mail.set_item(self.items[idx-1])
        elif focus_widget and input in ('h', 'down') and idx < len(self.body)-1:
            self.set_focus(idx+1)
            self.mail.set_item(self.items[idx+1])
        elif focus_widget and input == 'r':
            self.mail.set_reply()
            self.cols.set_focus(1)
            self.footer.set_text(('headfoot', FOOT2))
        elif focus_widget and input == 'd':
            self.folder.delete(self.items[idx])
            del self.items[idx]
            del self.body[idx]
            self.mail.set_item(self.items[min(idx, len(self.items)-1)] if self.items else None)
        elif input == 'n':
            self.refresh()
        elif input in ('q', 'ctrl d'):
            sys.exit()

class Mail(urwid.BoxWidget):
    def __init__(self, outbox):
        self.outbox = outbox
        self.contents = urwid.SimpleListWalker([urwid.Text('')])
        self.listbox = urwid.ListBox(self.contents)
        self.edit = False

    def set_item(self, item):
        self.item = item
        self.contents[0] = urwid.Text('\n'.join((item.text or u'').splitlines()) if item else '')
        self.edit = False

    def set_reply(self):
        field = urwid.Edit(multiline=True)
        field.set_edit_text('\n\n'+'\n'.join('> '+l for l in (self.item.text or u'').splitlines())+'\n\n')
        self.contents[0] = field
        self.edit = True
    
    def render(self, size, focus=False):
        return self.listbox.render(size, focus)

    def keypress(self, size, key):
        if self.edit and key in ('ctrl x', 'ctrl y', 'esc'):
            if key == 'ctrl x':
                self.outbox.create_item(subject='Re: '+self.item.subject, to=self.item.sender.email, text=self.contents[0].text).send()
            self.set_item(self.item)
            self.cols.set_focus(0)
            self.footer.set_text(('headfoot', FOOT1))
            return key
        return self.listbox.keypress(size, key)

class MailHeader(urwid.Text):
    def __init__(self, item):
        self.item = item
        super(MailHeader, self).__init__(item.sender.name+'\n  '+item.subject[:30])

def main():
    server = kopano.Server()
    user = kopano.Server().user(server.options.auth_user)
    header = urwid.Text('inbox')
    footer = urwid.Text(FOOT1)
    mail = Mail(user.store.outbox)
    mailbox = MailBox(user.store.inbox, mail)
    columns = urwid.Columns([('fixed', 32, mailbox), mail], 1)
    frame = urwid.Frame(urwid.AttrWrap(columns, 'body'), header=urwid.AttrMap(header, 'headfoot'), footer=urwid.AttrMap(footer, 'headfoot'))
    mailbox.cols = mail.cols = columns
    mailbox.footer = mail.footer = footer
    urwid.MainLoop(frame, PALETTE).run()

if __name__ == '__main__':
    main()
