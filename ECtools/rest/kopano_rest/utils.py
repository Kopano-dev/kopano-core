
def _folder(store, folderid):
    name = folderid.lower()
    if name == 'inbox':
        return store.inbox
    elif name == 'drafts':
        return store.drafts
    elif name == 'calendar':
        return store.calendar
    elif name == 'contacts':
        return store.contacts
    elif name == 'deleteditems':
        return store.wastebasket
    elif name == 'junkemail':
        return store.junk
    elif name == 'sentitems':
        return store.sentmail
    else:
        return store.folder(entryid=folderid)
