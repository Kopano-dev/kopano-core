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
    def import_props(self, parent, mapiobj, embedded=False):
        props2 = []
        attach_method = subnode_nid = None
        for k, v in parent.pc.props.items():
            propid, proptype, value = k, v.wPropType, v.value
            if proptype in (PT_CURRENCY, PT_MV_CURRENCY, PT_ACTIONS, PT_SRESTRICT, PT_SVREID):
                continue # unsupported by parser

            nameid = self.propid_nameid.get(propid)
            if nameid:
                propid = PROP_ID(mapiobj.GetIDsFromNames([MAPINAMEID(*nameid)], 0)[0])

            if propid == (PR_SUBJECT>>16) and value and ord(value[0]) == 0x01:
                value = value[2:] # \x01 plus another char indicates normalized-subject-prefix-length
            elif proptype == PT_SYSTIME:
                value = MAPI.Time.FileTime(value)
            elif propid == PR_ATTACH_METHOD >> 16:
                attach_method = value
            elif propid == PR_ATTACH_DATA_OBJ >> 16 and len(value) == 4: # XXX why not 4?
                subnode_nid = struct.unpack('I', value)[0]

            if isinstance(parent, pst.Attachment) or embedded or PROP_TAG(proptype, propid) != PR_ATTACH_NUM: # work around webapp bug? KC-390
                props2.append(SPropValue(PROP_TAG(proptype, propid), value))

        if attach_method == ATTACH_EMBEDDED_MSG and subnode_nid is not None:
            submessage = pst.Message(subnode_nid, self.ltp, self.nbd, parent)
            submapiobj = mapiobj.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY)
            self.import_props(submessage, submapiobj, embedded=True)
            self.import_attachments(submessage, submapiobj)
            self.import_recipients(submessage, submapiobj)

        mapiobj.SetProps(props2)
        mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def import_attachments(self, message, mapiobj):
        for attachment in message.subattachments:
            attachment = message.get_attachment(attachment)
            (id_, attachment2) = mapiobj.CreateAttach(None, 0)
            self.import_props(attachment, attachment2)

    def import_recipients(self, message, mapiobj):
        recipients = [] # XXX groups etc?
        for r in message.subrecipients:
            user = None
            key = None
            if r.ObjectType == 6 and r.DisplayType == 0:
                if r.AddressType == 'EX' or not r.AddressType: # missing addr type observed in the wild
                    key = r.DisplayName or r.EmailAddress
                elif r.AddressType == 'ZARAFA':
                    key = r.EmailAddress or r.DisplayName
            if key:
                try:
                    user = self.server.user(email=key) # XXX using email arg for name/fullname/email
                except kopano.NotFoundError:
                    if key not in self.unresolved:
                        self.log.warning("could not resolve user '%s'" % key)
                        self.unresolved.add(key)
            props = []
            if r.RecipientType is not None:
                props.append(SPropValue(PR_RECIPIENT_TYPE, r.RecipientType))
            if user or r.AddressType is not None:
                props.append(SPropValue(PR_ADDRTYPE_W, u'ZARAFA' if user else r.AddressType))
            if user or r.DisplayName is not None:
                props.append(SPropValue(PR_DISPLAY_NAME_W, user.fullname if user else r.DisplayName))
            if r.DisplayType is not None:
                props.append(SPropValue(PR_DISPLAY_TYPE, r.DisplayType))
            if user or r.EmailAddress:
                props.append(SPropValue(PR_EMAIL_ADDRESS_W, user.name if user else r.EmailAddress))
            if user:
                props.append(SPropValue(PR_ENTRYID, user.userid.decode('hex')))
            recipients.append(props)
        mapiobj.ModifyRecipients(0, recipients)
        mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

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
                        self.log.debug("importing message '%s'" % (message.Subject or ''))
                        message2 = folder2.create_item()
                        self.import_props(message, message2.mapiobj)
                        self.import_attachments(message, message2.mapiobj)
                        self.import_recipients(message, message2.mapiobj)
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
        pst.set_log(self.log, self.stats)
        self.unresolved = set()
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
                    writer.writerow([_encode(path), _encode(message.Subject or '')])

def main():
    parser = kopano.parser('cflskpUPu', usage='kopano-migration-pst PATH [-u NAME]')
    parser.add_option('', '--stats', dest='stats', action='store_true', help='list folders for PATH')
    parser.add_option('', '--index', dest='index', action='store_true', help='list items for PATH')
    parser.add_option('', '--import-root', dest='import_root', action='store', help='import under specific folder', metavar='PATH')

    options, args = parser.parse_args()
    options.service = False

    if not args or (bool(options.stats or options.index) == bool(options.users)):
        parser.print_help()
        sys.exit(1)

    if options.stats or options.index:
        show_contents(args, options)
    else:
        Service('migration-pst', options=options, args=args).start()

if __name__ == '__main__':
    main()
