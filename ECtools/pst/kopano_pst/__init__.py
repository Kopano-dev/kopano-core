#!/usr/bin/python
import csv
import time
import sys

from MAPI.Util import *
import MAPI.Time
import kopano
from kopano import log_exc

import pst

def _encode(s):
    return s.encode(sys.stdout.encoding or 'utf8')

class Service(kopano.Service):
    def import_props(self, parent, mapiobj): # XXX fully recurse
        props2 = []
        for k, v in parent.pc.props.items():
            propid, proptype, value = k, v.wPropType, v.value
            if proptype == PT_SYSTIME:
                value = MAPI.Time.unixtime(time.mktime(value.timetuple()))
            nameid = self.propid_nameid.get(propid)
            if nameid:
                propid = PROP_ID(mapiobj.GetIDsFromNames([MAPINAMEID(*nameid)], 0)[0])
            if propid == PR_ATTACH_DATA_OBJ >> 16 and len(value) == 4: # XXX why not 4
                subnode_nid = struct.unpack('I', value)[0]
                message = pst.Message(subnode_nid, self.ltp, self.nbd, parent)
                message2 = mapiobj.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY)
                subprops = []
                for l, w in message.pc.props.items():
                    value = w.value
                    if w.wPropType == PT_SYSTIME:
                        value = MAPI.Time.unixtime(time.mktime(value.timetuple()))
                    subprops.append(SPropValue(PROP_TAG(w.wPropType, l), value))
                message2.SetProps(subprops)
                message2.SaveChanges(KEEP_OPEN_READWRITE)
            else:
                props2.append(SPropValue(PROP_TAG(proptype, propid), value))
        mapiobj.SetProps(props2)
        mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def import_attachments(self, message, message2):
        for attachment in message.subattachments:
            attachment = message.get_attachment(attachment)
            (id_, attachment2) = message2.mapiobj.CreateAttach(None, 0)
            self.import_props(attachment, attachment2)

    def import_recipients(self, message, message2):
        recipients = [] # XXX group etc entryid?, exchange user?
        for r in message.subrecipients:
            props = [
                SPropValue(PR_RECIPIENT_TYPE, r.RecipientType),
                SPropValue(PR_DISPLAY_NAME_W, r.DisplayName),
                SPropValue(PR_ADDRTYPE_W, r.AddressType),
            ]
            if r.EmailAddress:
                props.append(SPropValue(PR_EMAIL_ADDRESS_W, r.EmailAddress))
            if r.AddressType == 'ZARAFA' and r.ObjectType==6 and not '@' in r.EmailAddress: # XXX broken props?
                user = self.server.user(r.EmailAddress)
                props.append(SPropValue(PR_ENTRYID, user.userid.decode('hex')))
            recipients.append(props)
        message2.mapiobj.ModifyRecipients(0, recipients)
        message2.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def import_pst(self, pst, user):
        for folder in pst.folder_generator():
            with log_exc(self.log, self.stats):
                path = folder.path[1:]
                if (path+'/').startswith('Top of Outlook data file/'):
                    path = path[25:]
                if not path:
                    continue
                if self.options.folders and path not in self.options.folders:
                    continue
                self.log.info("importing folder '%s'" % path)
                if self.options.import_root:
                    path = self.options.import_root + '/' + path
                folder2 = user.folder(path, create=True)
                if folder.ContainerClass:
                    folder2.container_class = folder.ContainerClass
                for message in pst.message_generator(folder):
                    with log_exc(self.log, self.stats):
                        self.log.debug("importing message '%s'" % message.Subject)
                        message2 = folder2.create_item()
                        self.import_props(message, message2.mapiobj)
                        self.import_attachments(message, message2)
                        self.import_recipients(message, message2)
                        self.stats['messages'] += 1

    def get_named_property_map(self, pst):
        propid_nameid = {}
        for nameid in pst.messaging.nameid_entries:
            propid_nameid[nameid.NPID] = (
                nameid.guid,
                MNID_STRING if nameid.N==1 else MNID_ID,
                nameid.name if nameid.N==1 else nameid.dwPropertyID
            )
        return propid_nameid

    def main(self):
        self.stats = {'messages': 0, 'errors': 0}
        t0 = time.time()
        for arg in self.args:
            self.log.info("importing file '%s'" % arg)
            p = pst.PST(arg)
            self.nbd, self.ltp = p.nbd, p.ltp
            self.propid_nameid = self.get_named_property_map(p)
            for name in self.options.users:
                self.log.info("importing to user '%s'" % name)
                self.import_pst(p, self.server.user(name))
        self.log.info('imported %d items in %.2f seconds (%.2f/sec, %d errors)' %
            (self.stats['messages'], time.time()-t0, self.stats['messages']/(time.time()-t0), self.stats['errors']))


def show_contents(args, options):
    writer = csv.writer(sys.stdout)
    for arg in args:
        p = pst.PST(arg)
        for folder in p.folder_generator():
            path = folder.path[1:]
            if (path+'/').startswith('Top of Outlook data file/'): # XXX copy-paste
                path = path[25:]
            if not path:
                continue
            if options.folders and path not in options.folders:
                continue
            if options.stats:
                writer.writerow([_encode(path), folder.ContentCount])
            elif options.index:
                for message in p.message_generator(folder):
                    writer.writerow([_encode(path), _encode(message.Subject)])

def main():
    parser = kopano.parser('cflskpUPu', usage='kopano-pst PATH -u NAME')
    parser.add_option('', '--stats', dest='stats', action='store_true', help='list folders for PATH')
    parser.add_option('', '--index', dest='index', action='store_true', help='list items for PATH')
    parser.add_option('', '--import-root', dest='import_root', action='store', help='list items for PATH', metavar='PATH')

    options, args = parser.parse_args()
    options.service = False

    assert args, 'please specify path(s) to .pst file(s)'
    if not (options.stats or options.index):
        assert options.users, 'please specify user(s) to import to'

    if options.stats or options.index:
        show_contents(args, options)
    else:
        Service('pst', options=options, args=args).start()

if __name__ == '__main__':
    main()
