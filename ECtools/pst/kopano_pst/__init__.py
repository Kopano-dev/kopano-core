#!/usr/bin/python
import time
import pst
import kopano
from MAPI.Util import *
import MAPI.Time

def export_props(x, m2):
    props2 = []
    for k, v in x.pc.props.items():
        propid, proptype, value = k, v.wPropType, v.value
        if proptype == PT_SYSTIME: # XXX into pyko?
            value = MAPI.Time.unixtime(time.mktime(value.timetuple()))
        props2.append(SPropValue(PROP_TAG(proptype,propid), value))
    m2.mapiobj.SetProps(props2)
    m2.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

def export_attachments(m, m2):
    for a in m.subattachments:
        a = m.get_attachment(a)
        a2 = m2.create_attachment(a.Filename, a.data) # XXX other methods
        export_props(a, a2)

def export_recipients(m, m2):
    recipients = [] # XXX entryid?
    for r in m.subrecipients:
        recipients.append([
            SPropValue(PR_RECIPIENT_TYPE, r.RecipientType),
            SPropValue(PR_DISPLAY_NAME_W, r.DisplayName),
            SPropValue(PR_ADDRTYPE_W, r.AddressType),
            SPropValue(PR_EMAIL_ADDRESS_W, r.EmailAddress),
        ])
    m2.mapiobj.ModifyRecipients(0, recipients)
    m2.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

def export_pst(pst, user): # XXX named props, embedded msg?
    for f in pst.folder_generator():
        path = f.path.replace('\\', '/')[1:] # XXX escaping
        f2 = user.folder(path, create=True)
        for m in pst.message_generator(f):
            m2 = f2.create_item()
            export_props(m, m2)
            export_attachments(m, m2)
            export_recipients(m, m2)

def main():
    parser = kopano.parser('skpUPu', usage='kopano-pst PATH -u NAME')
    options, args = parser.parse_args()
    assert len(args) == 1, 'please specify path to .pst file'
    assert len(options.users) == 1, 'please specify user to export to'

    export_pst(pst.PST(args[0]), kopano.user(options.users[0]))

if __name__ == '__main__':
    main()
