"""
Part of the high-level python bindings for Kopano

Copyright 2017 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI.Tags import (
    PR_RULE_CONDITION, PR_RULE_ACTIONS, PR_RULE_PROVIDER, ACTTYPE, PR_ENTRYID,
)
from MAPI.Defs import (
    PpropFindProp
)
from .compat import (
    hex as _hex, repr as _repr,
)
from .defs import *

class Delegation(object):
    """Delegation class"""

    def __init__(self, store, user):
        self.store = store
        self.user = user

    @property
    def see_private(self):
        fbmsg, (entryids, names, flags) = self.store._fbmsg_delgs()
        pos = entryids.Value.index(self.user.userid.decode('hex'))

        return bool(flags.Value[pos] & 1)

    @see_private.setter
    def see_private(self, b):
        fbmsg, (entryids, names, flags) = self.store._fbmsg_delgs()
        pos = entryids.Value.index(self.user.userid.decode('hex'))

        if b:
            flags.Value[pos] |= 1
        else:
            flags.Value[pos] &= ~1

        fbmsg.SetProps([flags])
        fbmsg.SaveChanges(0)

    @property # XXX setter
    def send_copy(self):
        """Delegate receives copies of meeting requests."""
        for rule in self.store.inbox.rules():
            if rule.mapirow[PR_RULE_PROVIDER] == 'Schedule+ EMS Interface':
                actions = rule.mapirow[PR_RULE_ACTIONS].lpAction
                if actions[0].acttype == ACTTYPE.OP_DELEGATE:
                    for addrentry in actions[0].actobj.lpadrlist:
                        entryid = PpropFindProp(addrentry, PR_ENTRYID)
                        return _hex(entryid.Value) == self.user.userid
        return False

    @property
    def flags(self):
        flags = []
        if self.see_private:
            flags.append('see_private')
        if self.send_copy:
            flags.append('send_copy')
        return flags

    @flags.setter
    def flags(self, value):
        self.see_private = ('see_private' in value)

    @staticmethod
    def _delete_after_copy(store):
        """Delete meetingrequests after copying them to delegates."""
        for rule in store.inbox.rules():
            if rule.mapirow[PR_RULE_PROVIDER] == 'Schedule+ EMS Interface':
                actions = rule.mapirow[PR_RULE_ACTIONS].lpAction
                if len(actions) >= 2 and actions[1].acttype == ACTTYPE.OP_DELETE:
                    return True
        return False

    def _delete(self):
        # XXX update delegate rule

        fbmsg, (entryids, names, flags) = self.store._fbmsg_delgs()
        pos = entryids.Value.index(self.user.userid.decode('hex'))

        del entryids.Value[pos]
        del names.Value[pos]
        del flags.Value[pos]

        fbmsg.SetProps([entryids, names, flags])
        fbmsg.SaveChanges(0)

    def __unicode__(self):
        return u"Delegation('%s')" % self.user.name

    def __repr__(self):
        return _repr(self)

