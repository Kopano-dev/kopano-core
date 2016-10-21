#!/usr/bin/python
import time
import pst
import kopano
from MAPI.Util import *
import MAPI.Time

class Service(kopano.Service):

    def import_props(self, props, message2):
        props2 = []
        for k, v in props.items():
            propid, proptype, value = k, v.wPropType, v.value
            if proptype == PT_SYSTIME: # XXX into pyko?
                value = MAPI.Time.unixtime(time.mktime(value.timetuple()))
            props2.append(SPropValue(PROP_TAG(proptype,propid), value))
        message2.mapiobj.SetProps(props2)
        message2.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def import_attachments(self, message, message2):
        for attachment in message.subattachments:
            attachment = message.get_attachment(attachment)
            attachment2 = message2.create_attachment(attachment.Filename, attachment.data) # XXX other methods
            self.import_props(attachment.pc.props, attachment2)

    def import_recipients(self, message, message2):
        recipients = [] # XXX entryid?
        for r in message.subrecipients:
            recipients.append([
                SPropValue(PR_RECIPIENT_TYPE, r.RecipientType),
                SPropValue(PR_DISPLAY_NAME_W, r.DisplayName),
                SPropValue(PR_ADDRTYPE_W, r.AddressType),
                SPropValue(PR_EMAIL_ADDRESS_W, r.EmailAddress),
            ])
        message2.mapiobj.ModifyRecipients(0, recipients)
        message2.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def import_pst(self, pst, user): # XXX named props, embedded msgs?
        for folder in pst.folder_generator():
            path = folder.path.replace('\\', '/')[1:] # XXX escaping
            self.log.info("importing folder '%s'" % path)
            folder2 = user.folder(path, create=True)
            for message in pst.message_generator(folder):
                self.log.debug("importing message '%s'" % message.Subject)
                message2 = folder2.create_item()
                self.import_props(message.pc.props, message2)
                self.import_attachments(message, message2)
                self.import_recipients(message, message2)

    def main(self):
        for arg in self.args:
            self.log.info("importing file '%s'" % arg)
            p = pst.PST(arg)
            for name in self.options.users:
                self.log.info("importing to user '%s'" % name)
                self.import_pst(p, kopano.user(name))

def main():
    parser = kopano.parser('cflskpUPu', usage='kopano-pst PATH -u NAME')

#    parser.add_option('', '--restore-root', dest='restore_root', help='restore under specific folder', metavar='PATH')
#    parser.add_option('', '--stats', dest='stats', action='store_true', help='list folders for PATH')
#    parser.add_option('', '--index', dest='index', action='store_true', help='list items for PATH')
#    parser.add_option('', '--recursive', dest='recursive', action='store_true', help='backup/restore folders recursively')

    options, args = parser.parse_args()
    options.service = False

    assert args, 'please specify path(s) to .pst file(s)'
    assert options.users, 'please specify user(s) to import to'

    Service('pst', options=options, args=args).start()

if __name__ == '__main__':
    main()
