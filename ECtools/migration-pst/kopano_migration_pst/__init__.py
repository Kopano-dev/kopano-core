#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from .version import __version__

import codecs
import csv
import datetime
import json
import time
import sys

from collections import defaultdict

from MAPI.Util import *
from MAPI.Struct import MAPIErrorNetworkError
from MAPI.Tags import (PS_COMMON, PS_ARCHIVE,
                       PR_SENDER_EMAIL_ADDRESS_W, PR_SENDER_ENTRYID,
                       PR_SENDER_NAME_W, PR_SENDER_ADDRTYPE_W,
                       PR_SENDER_SEARCH_KEY,
                       PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_NAME_W,
                       PR_SENT_REPRESENTING_SEARCH_KEY, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W,
                       PR_SENT_REPRESENTING_ADDRTYPE_W,
                       PR_RECEIVED_BY_ENTRYID, PR_RECEIVED_BY_NAME_W, PR_RECEIVED_BY_SEARCH_KEY,
                       PR_RECEIVED_BY_EMAIL_ADDRESS_W, PR_RECEIVED_BY_ADDRTYPE_W,
                       PR_RCVD_REPRESENTING_ENTRYID, PR_RCVD_REPRESENTING_NAME_W,
                       PR_RCVD_REPRESENTING_SEARCH_KEY, PR_RCVD_REPRESENTING_EMAIL_ADDRESS_W,
                       PR_RCVD_REPRESENTING_ADDRTYPE_W)
import MAPI.Time
import kopano
from kopano import log_exc

from kopano.pidlid import (
    PidLidDistributionListMembers,
    PidLidDistributionListOneOffMembers
)

from . import pst

WRAPPED_ENTRYID_PREFIX = codecs.decode('00000000C091ADD3519DCF11A4A900AA0047FAA4', 'hex')
WRAPPED_EID_TYPE_MASK = 0xf
WRAPPED_EID_TYPE_CONTACT = 3
WRAPPED_EID_TYPE_PERSONAL_DISTLIST = 4

REMINDER_SIGNALTIME_ID = 0x8560
REMINDER_SET_ID = 0x8503

NOW = datetime.datetime.utcnow()

SENDER_PROPS = {
    'entryid': PR_SENDER_ENTRYID,
    'name': PR_SENDER_NAME_W,
    'searchkey': PR_SENDER_SEARCH_KEY,
    'email': PR_SENDER_EMAIL_ADDRESS_W, # PidTagSenderSmtpAddress
    'addrtype': PR_SENDER_ADDRTYPE_W,
    'smtp': pst.PropIdEnum.PidTagSentRepresentingSmtpAddress,
}

SENT_REPRESENTING_PROPS = {
    'entryid': PR_SENT_REPRESENTING_ENTRYID,
    'name': PR_SENT_REPRESENTING_NAME_W,
    'searchkey': PR_SENT_REPRESENTING_SEARCH_KEY,
    'email': PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, # PidTagSentRepresentingSmtpAddress
    'addrtype': PR_SENT_REPRESENTING_ADDRTYPE_W,
    'smtp': pst.PropIdEnum.PidTagSenderSmtpAddress,
}

RECEIVED_PROPS = {
    'entryid': PR_RECEIVED_BY_ENTRYID,
    'name': PR_RECEIVED_BY_NAME_W,
    'searchkey': PR_RECEIVED_BY_SEARCH_KEY,
    'email': PR_RECEIVED_BY_EMAIL_ADDRESS_W, # PidTagReceivedBySmtpAddress
    'addrtype': PR_RECEIVED_BY_ADDRTYPE_W,
    'smtp': pst.PropIdEnum.PidTagReceivedBySmtpAddress,
}

RECEIVED_REPR_PROPS = {
    'entryid': PR_RCVD_REPRESENTING_ENTRYID,
    'name': PR_RCVD_REPRESENTING_NAME_W,
    'searchkey': PR_RCVD_REPRESENTING_SEARCH_KEY,
    'email': PR_RCVD_REPRESENTING_EMAIL_ADDRESS_W, # PidTagReceivedRepresentingSmtpAddress
    'addrtype': PR_RCVD_REPRESENTING_ADDRTYPE_W,
    'smtp': pst.PropIdEnum.PidTagReceivedRepresentingSmtpAddress,
}

EMAIL_RECIP_PROPS = [SENDER_PROPS, SENT_REPRESENTING_PROPS, RECEIVED_PROPS, RECEIVED_REPR_PROPS]

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
        props2 = {}
        attach_method = subnode_nid = None
        reminder_in_past = False
        reminder_set_id = None
        for k, v in parent.pc.props.items():
            propid, proptype, value = k, v.wPropType, v.value

            if propid == (PR_MESSAGE_CLASS_W>>16):
                if value == 'IPM.DistList':
                    self.distlist_entryids.append(parent.EntryId)

            if proptype in (PT_CURRENCY, PT_MV_CURRENCY, PT_ACTIONS, PT_SRESTRICT, PT_SVREID):
                continue # unsupported by parser

            nameid = self.propid_nameid.get(propid)
            if nameid:
                propid = PROP_ID(mapiobj.GetIDsFromNames([MAPINAMEID(*nameid)], MAPI_CREATE)[0])
                if nameid[0] == PS_ARCHIVE:
                    continue

                elif nameid[0] == PS_COMMON:
                    if nameid[2] == REMINDER_SIGNALTIME_ID:
                        if MAPI.Time.FileTime(value).datetime() < NOW:
                            reminder_in_past = True
                    elif nameid[2] == REMINDER_SET_ID:
                        reminder_set_id = propid

            if propid == (PR_SUBJECT_W>>16) and value and ord(value[0]) == 0x01:
                value = value[2:] # \x01 plus another char indicates normalized-subject-prefix-length
            elif proptype == PT_SYSTIME:
                value = MAPI.Time.FileTime(value)
            elif propid == PR_ATTACH_METHOD >> 16:
                attach_method = value
            elif propid == PR_ATTACH_DATA_OBJ >> 16 and len(value) == 4: # XXX why not 4?
                subnode_nid = struct.unpack('I', value)[0]

            if isinstance(parent, pst.Attachment) or embedded or PROP_TAG(proptype, propid) != PR_ATTACH_NUM: # work around webapp bug? KC-390
                props2[propid] = SPropValue(PROP_TAG(proptype, propid), value)

        if attach_method == ATTACH_EMBEDDED_MSG and subnode_nid is not None:
            submessage = pst.Message(subnode_nid, self.ltp, self.nbd, parent)
            submapiobj = mapiobj.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY)
            self.import_attachments(submessage, submapiobj)
            self.import_recipients(submessage, submapiobj)
            self.import_props(submessage, submapiobj, embedded=True)

        # Optionally dismiss reminders for events in the past
        if (self.options.dismiss_reminders and \
            reminder_in_past and \
            reminder_set_id is not None):
            props2[reminder_set_id].Value = False

        proplist = list(props2.values()) # TODO move down and overwrite instead of append

        # Check if any recipient property on an item has an EX address and try to convert it.
        for prop_dict in EMAIL_RECIP_PROPS:
            addrtype = parent.pc.getval(PROP_ID(prop_dict['addrtype']))
            if addrtype and addrtype == 'EX':
                proplist.extend(self.convert_exchange_recipient(parent, prop_dict))

        mapiobj.SetProps(proplist)
        mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def convert_exchange_recipient(self, parent, convertprops):
        props = []
        email = parent.pc.getval(convertprops['smtp'])

        # When mapping is provided and no email address was found
        if not email and self.options.mapping:
            legacyExchangeDN = parent.pc.getval(PROP_ID(convertprops['email']))
            if legacyExchangeDN:
                legacyExchangeDN = legacyExchangeDN.lower()
                try:
                    email = self.mapping[legacyExchangeDN]
                    self.log.debug('found legacyExchangeDN: \'%s\' in mapping', legacyExchangeDN)
                except KeyError:
                    if legacyExchangeDN not in self.nomapping:
                        self.log.warning('legacyExchangeDN: \'%s\' was not found in mapping', legacyExchangeDN)
                        self.nomapping.add(legacyExchangeDN)
            else:
                self.log.debug('item has no legacyExchangeDN, skipping importing recipient data')

        # Still no email, logs erros and add it to stats
        if not email:
            self.stats['noemail'] += 1
            recip = 'sender' if convertprops['email'] in [PR_SENDER_EMAIL_ADDRESS_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W] else 'receiver'
            self.log.warning('no email address found for %s property, skipping', recip)
            return []

        try:
            user = self.server.user(email=email)
        except kopano.NotFoundError:
            user = None

        if user:
            props.append(SPropValue(convertprops['addrtype'], u'ZARAFA'))
            props.append(SPropValue(convertprops['email'], user.email))
            props.append(SPropValue(convertprops['name'], user.fullname))
            props.append(SPropValue(convertprops['entryid'], codecs.decode(user.userid, 'hex')))
            props.append(SPropValue(convertprops['searchkey'], user[PR_SEARCH_KEY]))  # TODO: add to Pyko.
        else:
            name = parent.pc.getval(PROP_ID(convertprops['name']))
            if not name:
                name = email

            props.append(SPropValue(convertprops['addrtype'], u'SMTP'))
            props.append(SPropValue(convertprops['email'], email))
            props.append(SPropValue(convertprops['name'], name))
            props.append(SPropValue(convertprops['entryid'], self.server.ab.CreateOneOff(name, u'SMTP', email, MAPI_UNICODE)))
            props.append(SPropValue(convertprops['searchkey'], b'SMTP:' + email.encode('UTF-8') + b'\x00'))

        return props

    def import_attachments(self, message, mapiobj):
        for attachment in message.subattachments:
            attachment = message.get_attachment(attachment)
            (id_, attachment2) = mapiobj.CreateAttach(None, 0)
            self.import_props(attachment, attachment2)

    def import_recipients(self, message, mapiobj):
        recipients = [] # XXX groups etc?
        for r in message.subrecipients:
            # try to resolve user
            user = None
            key = None
            if r.ObjectType == 6 and r.DisplayType == 0:
                if r.AddressType == 'EX':
                    key = r.SmtpAddress
                elif r.AddressType == 'ZARAFA':
                    key = r.EmailAddress
            if key:
                try:
                    user = self.server.user(email=key) # XXX using email arg for name/fullname/email
                except kopano.NotFoundError:
                    if key not in self.unresolved:
                        self.log.warning("could not resolve user '%s'", key)
                        self.unresolved.add(key)

            # set recipient properties
            props = []
            if r.RecipientType is not None:
                props.append(SPropValue(PR_RECIPIENT_TYPE, r.RecipientType))
            if r.DisplayType is not None:
                props.append(SPropValue(PR_DISPLAY_TYPE, r.DisplayType))
            if r.DisplayName is not None:
                props.append(recip_prop(PROP_ID(PR_DISPLAY_NAME), r.DisplayName))

            if user:
                # matching user: set properties cleanly
                props.append(recip_prop(PROP_ID(PR_ADDRTYPE), u'ZARAFA'))
                props.append(SPropValue(PR_ENTRYID, codecs.decode(user.userid, 'hex')))
                props.append(recip_prop(PROP_ID(PR_EMAIL_ADDRESS), user.name))
            else:
                # create one-off
                email = None
                if r.AddressType == 'EX':
                    if r.SmtpAddress:
                        email = r.SmtpAddress
                    else:
                        email = self.mapping.get(r.EmailAddress.lower())
                elif r.AddressType == 'SMTP':
                    email = r.EmailAddress
                elif r.AddressType == 'ZARAFA':
                    email = r.SmtpAddress
                if email:
                    props.append(SPropValue(PR_ENTRYID, self.server.ab.CreateOneOff(r.DisplayName, u'SMTP', email, MAPI_UNICODE)))
                    props.append(recip_prop(PROP_ID(PR_ADDRTYPE), u'SMTP'))
                    props.append(recip_prop(PROP_ID(PR_EMAIL_ADDRESS), email))
                else:
                    self.log.warning("no email address for recipient '%s'", r.DisplayName or '')

            recipients.append(props)
        mapiobj.ModifyRecipients(0, recipients)

    def import_pst(self, p, store):
        folders = list(p.folder_generator())
        root_path = rev_cp1252(folders[0].path)
        self.distlist_entryids = []
        self.entryid_map = {}

        for folder in folders:
            with log_exc(self.log, self.stats):
                import_nids = []
                if self.options.nids:
                    for nid in self.options.nids:
                        nid = int(nid)
                        parentNid = p.nbd.nbt_entries[nid].nidParent
                        if folder.nid.nid == parentNid.nid:
                            import_nids.append(nid)
                    if not import_nids:
                        continue

                path = rev_cp1252(folder.path[len(root_path)+1:]) or '(root)'
                if path == '(root)' and folder.ContentCount == 0:
                    continue

                if self.options.folders and \
                   path.lower() not in [f.lower() for f in self.options.folders]:
                    continue

                self.log.info("importing folder '%s'", path)
                if self.options.import_root:
                    path = self.options.import_root + '/' + path
                while True:
                    try:
                        folder2 = store.folder(path, create=True)
                        if self.options.clean_folders:
                            folder2.empty()
                        if folder.ContainerClass:
                            folder2.container_class = folder.ContainerClass
                        break
                    except MAPIErrorNetworkError as e:
                        self.log.warning("%s: Connection to server lost, retrying in 5 sec", e)
                        time.sleep(5)

                if import_nids:
                    for nid in import_nids:
                        message = pst.Message(pst.NID(nid), p.ltp, messaging=p.messaging)
                        self.import_message(message, folder2)
                else:
                    for message in p.message_generator(folder):
                        self.import_message(message, folder2)
        self.rewrite_entryids(store)

    def import_message(self, message, folder2):
        type_map = [
            ('IPM.Note', 'mail'),
            ('IPM.Schedule', 'mail'),
            ('IPM.Contact', 'contact'),
            ('IPM.DistList', 'distlist'),
            ('IPM.Appointment', 'appointment'),
        ]
        for (class_, type_) in type_map:
            if message.MessageClass and message.MessageClass.startswith(class_):
                break
        else:
            type_ = 'item'

        with log_exc(self.log, self.stats):
            while True:
                try:
                    self.log.debug("importing %s '%s' (NID=%d)", type_, rev_cp1252(message.Subject or ''), message.nid.nid)
                    message2 = folder2.create_item(save=False)
                    self.entryid_map[message.EntryId] = message2.entryid
                    self.import_attachments(message, message2.mapiobj)
                    self.import_recipients(message, message2.mapiobj)
                    self.import_props(message, message2.mapiobj)
                    self.stats['messages'] += 1
                    break
                except MAPIErrorNetworkError as e:
                    self.log.warning("{}: Connection to server lost, retrying in 5 sec".format(e))
                    time.sleep(5)


    def rewrite_entryids(self, store):
        # distribution lists
        for eid in self.distlist_entryids:
            with log_exc(self.log, self.stats):
                item = store.item(self.entryid_map[eid])
                members = item.get(PidLidDistributionListMembers)
                oneoffs = item.get(PidLidDistributionListOneOffMembers)
                if members:
                    for i, (member, oneoff) in enumerate(zip(members, oneoffs)):
                        pos = len(WRAPPED_ENTRYID_PREFIX)
                        prefix, flags, rest = member[:pos], member[pos], member[pos+1:]
                        if (prefix == WRAPPED_ENTRYID_PREFIX and \
                            (flags & WRAPPED_EID_TYPE_MASK) in (WRAPPED_EID_TYPE_CONTACT, WRAPPED_EID_TYPE_PERSONAL_DISTLIST) and \
                            rest in self.entryid_map):
                                members[i] = member[:pos+1] + codecs.decode(self.entryid_map[rest], 'hex')
                        else: # fall-back to oneoff if we can't resolve
                            members[i] = oneoffs[i]
                    item[PidLidDistributionListMembers] = members

    def get_named_property_map(self, p):
        propid_nameid = {}
        for nameid in p.messaging.nameid_entries:
            propid_nameid[nameid.NPID] = (
                nameid.guid,
                MNID_STRING if nameid.N==1 else MNID_ID,
                nameid.name if nameid.N==1 else nameid.dwPropertyID
            )
        return propid_nameid

    @property
    def mapping(self):
        if not self._mapping and self.options.mapping:
            with open(self.options.mapping, 'r') as fp:
                self._mapping = json.load(fp)
        return self._mapping

    def main(self):
        self._mapping = {}
        self.nomapping = set()
        self.stats = {'messages': 0, 'errors': 0, 'noemail': 0}

        pst.set_log(self.log, self.stats)
        self.unresolved = set()
        t0 = time.time()
        for arg in self.args:
            self.log.info("importing file '%s'", arg)
            try:
                p = pst.PST(arg)
            except pst.PSTException:
                self.log.error("'%s' is not a valid PST file", arg)
                continue
            self.nbd, self.ltp = p.nbd, p.ltp
            self.propid_nameid = self.get_named_property_map(p)
            for name in self.options.users:
                self.log.info("importing to user '%s'", name)
                store = self.server.user(name).store
                if store:
                    self.import_pst(p, store)
                else:
                    self.log.error("user '%s' has no store", name)
            for guid in self.options.stores:
                store = self.server.store(guid)
                self.log.info("importing to store '%s'", guid)
                if store:
                    self.import_pst(p, store)
                else:
                    self.log.error("guid '%s' has no store", guid)

        self.log.info('imported %d items in %.2f seconds (%.2f/sec, %d errors) (no resolved email address: %d)',
                      self.stats['messages'], time.time()-t0, self.stats['messages']/(time.time()-t0), self.stats['errors'], self.stats['noemail'])


def show_contents(args, options):
    writer = csv.writer(sys.stdout)
    for arg in args:
        p = pst.PST(arg)
        folders = list(p.folder_generator())
        root_path = rev_cp1252(folders[0]).path

        if options.summary:
            print("Folders:       %s" % sum(1 for f in folders))
            print("Items:         %s" % p.get_total_message_count())
            print("Attachments:   %s" %  p.get_total_attachment_count())
        else:
            for folder in folders:
                path = rev_cp1252(folder.path[len(root_path)+1:]) or '(root)'
                if path == '(root)' and folder.ContentCount == 0:
                    continue
                if options.folders and path.lower() not in [f.lower() for f in options.folders]:
                    continue
                if options.stats:
                    writer.writerow([path, folder.ContentCount])
                elif options.index:
                    for message in p.message_generator(folder):
                        writer.writerow([path, rev_cp1252(message.Subject or '')])


def create_mapping(args, options):
    mapping = defaultdict(dict)
    for arg in args:
        print("Creating mapping of PST '%s'" % arg)
        try:
            p = pst.PST(arg)
        except pst.PSTException:
            print("'%s' is not a valid PST file" % arg)
            continue

        for folder in p.folder_generator():
            for message in p.message_generator(folder):
                for rec in message.subrecipients:
                    if rec.AddressType != 'EX':
                        continue
                    if not rec.EmailAddress or not rec.SmtpAddress:
                        continue
                    mapping[rec.EmailAddress.lower()] = rec.SmtpAddress

    with open(options.create_mapping, 'w') as fp:
        json.dump(mapping, fp)
        print('Written mapping file to \'%s\'.' % options.create_mapping)



def create_mapping(args, options):
    mapping = defaultdict(dict)
    for arg in args:
        print("Creating mapping of PST '%s'" % arg)
        try:
            p = pst.PST(arg)
        except pst.PSTException:
            print("'%s' is not a valid PST file" % arg)
            continue

        for folder in p.folder_generator():
            for message in p.message_generator(folder):
                for rec in message.subrecipients:
                    if rec.AddressType != 'EX':
                        continue
                    if not rec.EmailAddress or not rec.SmtpAddress:
                        continue
                    mapping[rec.EmailAddress.lower()] = rec.SmtpAddress

    with open(options.create_mapping, 'w') as fp:
        json.dump(mapping, fp)
        print('Written mapping file to \'%s\'.' % options.create_mapping)


def main():
    parser = kopano.parser('CflSKQUPus', usage='kopano-migration-pst PATH [-u NAME]')
    parser.add_option('', '--stats', dest='stats', action='store_true', help='list folders for PATH')
    parser.add_option('', '--index', dest='index', action='store_true', help='list items for PATH')
    parser.add_option('', '--import-root', dest='import_root', action='store', help='import under specific folder', metavar='PATH')
    parser.add_option('', '--nid', dest='nids', action='append', help='import specific nid', metavar='NID')

    parser.add_option('', '--clean-folders', dest='clean_folders', action='store_true', default=False, help='empty folders before import (dangerous!)', metavar='PATH')
    parser.add_option('', '--create-ex-mapping', dest='create_mapping', help='create legacyExchangeDN mapping for Exchange 2007 PST', metavar='FILE')
    parser.add_option('', '--ex-mapping', dest='mapping', help='mapping file created --create-ex--mapping ', metavar='FILE')
    parser.add_option('', '--summary', dest='summary', action='store_true', help='show total amount of items for a given PST')
    parser.add_option('', '--dismiss-reminders', dest='dismiss_reminders', action='store_true', default=False, help='dismiss reminders for events in the past')

    options, args = parser.parse_args()
    options.service = False

    if not args or (bool(options.stats or options.index or options.create_mapping or options.summary) == bool(options.users or options.stores)):
        parser.print_help()
        sys.exit(1)

    if options.stats or options.index or options.summary:
        show_contents(args, options)
    elif options.create_mapping:
        create_mapping(args, options)
    else:
        Service('migration-pst', options=options, args=args).start()

if __name__ == '__main__':
    main() # pragma: no cover
