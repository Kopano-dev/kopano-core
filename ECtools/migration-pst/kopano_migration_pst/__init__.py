#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-or-later
from .version import __version__

import codecs
import csv
import time
import sys

from MAPI.Util import *
import MAPI.Time
import kopano
from kopano import log_exc

from . import pst

if sys.hexversion >= 0x03000000:
    def _encode(s):
        return s
else: # pragma: no cover
    def _encode(s):
        return s.encode(sys.stdout.encoding or 'utf8')

PSETID_Archive = DEFINE_GUID(0x72e98ebc, 0x57d2, 0x4ab5, 0xb0, 0xaa, 0xd5, 0x0a, 0x7b, 0x53, 0x1c, 0xb9)

def recip_prop(propid, value):
    # PST not generated with full unicode?
    if isinstance(value, bytes):
        return SPropValue(PROP_TAG(PT_STRING8, propid), value)
    else:
        return SPropValue(PROP_TAG(PT_UNICODE, propid), value)

def rev_cp1252(value):
    # PST not generated with full unicode flag?
    if isinstance(value, bytes):
        return value.decode('cp1252')
    else:
        return value

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
                propid = PROP_ID(mapiobj.GetIDsFromNames([MAPINAMEID(*nameid)], MAPI_CREATE)[0])
                if nameid[0] == PSETID_Archive:
                    continue

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
            self.import_attachments(submessage, submapiobj)
            self.import_recipients(submessage, submapiobj)
            self.import_props(submessage, submapiobj, embedded=True)

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
                value = u'ZARAFA' if user else r.AddressType
                props.append(recip_prop(PROP_ID(PR_ADDRTYPE), value))
            if user or r.DisplayName is not None:
                value = user.fullname if user else r.DisplayName
                props.append(recip_prop(PROP_ID(PR_DISPLAY_NAME), value))
            if r.DisplayType is not None:
                props.append(SPropValue(PR_DISPLAY_TYPE, r.DisplayType))
            if user or r.EmailAddress:
                value = user.name if user else r.EmailAddress
                props.append(recip_prop(PROP_ID(PR_EMAIL_ADDRESS), value))
            if user:
                props.append(SPropValue(PR_ENTRYID, codecs.decode(user.userid.encode('ascii'), 'hex')))
            elif r.EntryID is not None:
                props.append(SPropValue(PR_ENTRYID, r.EntryID))
            recipients.append(props)
        mapiobj.ModifyRecipients(0, recipients)

    def import_pst(self, p, store):
        folders = p.folder_generator()
        root_path = rev_cp1252(next(folders).path) # skip root
        for folder in folders:
            with log_exc(self.log, self.stats):
                path = rev_cp1252(folder.path[len(root_path)+1:])
                if self.options.folders and \
                   path.lower() not in [f.lower() for f in self.options.folders]:
                    continue
                self.log.info("importing folder '%s'" % path)
                if self.options.import_root:
                    path = self.options.import_root + '/' + path
                folder2 = store.folder(path, create=True)
                if self.options.clean_folders:
                    folder2.empty()
                if folder.ContainerClass:
                    folder2.container_class = folder.ContainerClass
                for message in p.message_generator(folder):
                    with log_exc(self.log, self.stats):
                        self.log.debug("importing message '%s'" % (rev_cp1252(message.Subject or '')))
                        message2 = folder2.create_item(save=False)
                        self.import_attachments(message, message2.mapiobj)
                        self.import_recipients(message, message2.mapiobj)
                        self.import_props(message, message2.mapiobj)
                        self.stats['messages'] += 1

    def get_named_property_map(self, p):
        propid_nameid = {}
        for nameid in p.messaging.nameid_entries:
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
            try:
                p = pst.PST(arg)
            except pst.PSTException:
                self.log.error("'%s' is not a valid PST file", arg)
                continue
            self.nbd, self.ltp = p.nbd, p.ltp
            self.propid_nameid = self.get_named_property_map(p)
            for name in self.options.users:
                self.log.info("importing to user '%s'" % name)
                self.import_pst(p, self.server.user(name).store)
            for guid in self.options.stores:
                self.log.info("importing to store '%s'" % guid)
                self.import_pst(p, self.server.store(guid))

        self.log.info('imported %d items in %.2f seconds (%.2f/sec, %d errors)' %
            (self.stats['messages'], time.time()-t0, self.stats['messages']/(time.time()-t0), self.stats['errors']))


def show_contents(args, options):
    writer = csv.writer(sys.stdout)
    for arg in args:
        p = pst.PST(arg)
        folders = p.folder_generator()
        root_path = rev_cp1252(next(folders).path) # skip root
        for folder in folders:
            path = rev_cp1252(folder.path[len(root_path)+1:])
            if options.folders and path.lower() not in [f.lower() for f in options.folders]:
                continue
            if options.stats:
                writer.writerow([_encode(path), folder.ContentCount])
            elif options.index:
                for message in p.message_generator(folder):
                    writer.writerow([_encode(path), _encode(rev_cp1252(message.Subject or ''))])

def main():
    parser = kopano.parser('cflskpUPuS', usage='kopano-migration-pst PATH [-u NAME]')
    parser.add_option('', '--stats', dest='stats', action='store_true', help='list folders for PATH')
    parser.add_option('', '--index', dest='index', action='store_true', help='list items for PATH')
    parser.add_option('', '--import-root', dest='import_root', action='store', help='import under specific folder', metavar='PATH')
    parser.add_option('', '--clean-folders', dest='clean_folders', action='store_true', default=False, help='empty folders before import (dangerous!)', metavar='PATH')

    options, args = parser.parse_args()
    options.service = False

    if not args or (bool(options.stats or options.index) == bool(options.users or options.stores)):
        parser.print_help()
        sys.exit(1)

    if options.stats or options.index:
        show_contents(args, options)
    else:
        Service('migration-pst', options=options, args=args).start()

if __name__ == '__main__':
    main() # pragma: no cover
