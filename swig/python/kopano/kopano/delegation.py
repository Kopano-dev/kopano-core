"""
Part of the high-level python bindings for Kopano

Copyright 2017 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI import (
    ROW_REMOVE, FL_PREFIX, RELOP_NE, ROW_ADD, ROW_MODIFY, MAPI_BEST_ACCESS,
    MAPI_UNICODE,
)
from MAPI.Tags import (
    PR_RULE_CONDITION, PR_RULE_ACTIONS, PR_RULE_PROVIDER, ACTTYPE, PR_ENTRYID,
    PR_RULE_ID, PR_RULES_TABLE, IID_IExchangeModifyTable, PR_RULE_LEVEL,
    PR_RULE_NAME, PR_RULE_SEQUENCE, PR_RULE_STATE, PR_RULE_PROVIDER_DATA,
    PR_MESSAGE_CLASS_W, PR_DELEGATED_BY_RULE, PR_SENSITIVITY, PR_ADDRTYPE_W,
    PR_EMAIL_ADDRESS_W, PR_DISPLAY_NAME_W, PR_SEARCH_KEY, PR_SMTP_ADDRESS_W,
    PR_OBJECT_TYPE, PR_DISPLAY_TYPE, PR_RECIPIENT_TYPE,
)
from MAPI.Defs import (
    PpropFindProp
)
from MAPI.Struct import (
    ROWENTRY, SPropValue, ACTION, actFwdDelegate, ACTIONS, SAndRestriction,
    SOrRestriction, SNotRestriction, SContentRestriction, SPropertyRestriction,
    SExistRestriction,
)
from .compat import (
    hex as _hex, repr as _repr,
)
from .defs import *
from .errors import NotFoundError

USERPROPS = [
    PR_ENTRYID,
    PR_ADDRTYPE_W,
    PR_EMAIL_ADDRESS_W,
    PR_DISPLAY_NAME_W,
    PR_SEARCH_KEY,
    PR_SMTP_ADDRESS_W,
    PR_OBJECT_TYPE,
    PR_DISPLAY_TYPE,
    PR_RECIPIENT_TYPE,
]

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

    def _parse_rule(self):
        userids, deletion = [], False
        for rule in self.store.inbox.rules():
            if PR_RULE_PROVIDER in rule.mapirow and PR_RULE_ACTIONS in rule.mapirow:
                if rule.mapirow[PR_RULE_PROVIDER] == 'Schedule+ EMS Interface':
                    actions = rule.mapirow[PR_RULE_ACTIONS].lpAction
                    if actions and actions[0].acttype == ACTTYPE.OP_DELEGATE:
                        for addrentry in actions[0].actobj.lpadrlist:
                            entryid = PpropFindProp(addrentry, PR_ENTRYID)
                            if entryid:
                                userids.append(entryid.Value)
        return userids, deletion

    def _save_rule(self, userids, deletion):
        # remove existing rule # XXX update
        for rule in self.store.inbox.rules():
            if rule.mapirow[PR_RULE_PROVIDER] == 'Schedule+ EMS Interface' and \
               PR_RULE_ID in rule.mapirow:
                pr_rule_id = rule.mapirow[PR_RULE_ID]

                rulerows = [ROWENTRY(ROW_REMOVE, [SPropValue(PR_RULE_ID, pr_rule_id)])]
                table = self.store.inbox.mapiobj.OpenProperty(PR_RULES_TABLE, IID_IExchangeModifyTable, 0, 0)
                table.ModifyTable(0, rulerows)

        # create new rule
        row = [
            SPropValue(PR_RULE_LEVEL, 0),
            SPropValue(PR_RULE_NAME, "Delegate Meetingrequest service"),
            SPropValue(PR_RULE_PROVIDER, "Schedule+ EMS Interface"),
            SPropValue(PR_RULE_SEQUENCE, 0),
            SPropValue(PR_RULE_STATE, 1),
            SPropValue(PR_RULE_PROVIDER_DATA, ''),
        ]

        actions = []
        userprops = []
        for userid in userids:
            user = self.store.server.gab.OpenEntry(userid, None, MAPI_BEST_ACCESS)
            userprops.append(user.GetProps(USERPROPS, MAPI_UNICODE))

        actions.append(ACTION( ACTTYPE.OP_DELEGATE, 0, None, None, 0, actFwdDelegate(userprops)))
#        # XXX deletion
        row.append(SPropValue(PR_RULE_ACTIONS, ACTIONS(1, actions)))

        cond = SAndRestriction([SContentRestriction(FL_PREFIX, PR_MESSAGE_CLASS_W, SPropValue(PR_MESSAGE_CLASS_W, u"IPM.Schedule.Meeting")),
            SNotRestriction( SExistRestriction(PR_DELEGATED_BY_RULE) ),
            SOrRestriction([SNotRestriction( SExistRestriction(PR_SENSITIVITY)),
            SPropertyRestriction(RELOP_NE, PR_SENSITIVITY, SPropValue(PR_SENSITIVITY, 2))])
        ])
        row.append(SPropValue(PR_RULE_CONDITION, cond))
        rulerows = [ROWENTRY(ROW_ADD, row)]
        table = self.store.inbox.mapiobj.OpenProperty(PR_RULES_TABLE, IID_IExchangeModifyTable, 0, 0)
        table.ModifyTable(0, rulerows)

    @property
    def send_copy(self):
        """Delegate receives copies of meeting requests."""
        userids, deletion = self._parse_rule()
        return self.user.userid.decode('hex') in userids

    @send_copy.setter
    def send_copy(self, value):
        userids, deletion = self._parse_rule()
        if value:
            userids.append(self.user.userid.decode('hex')) # XXX dupe
        else:
            userids = [u for u in userids if u != self.user.userid.decode('hex')]
        self._save_rule(userids, deletion)

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
        self.send_copy = ('send_copy' in value)

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
        try:
            pos = entryids.Value.index(self.user.userid.decode('hex'))
        except ValueError:
            raise NotFoundError("no delegation for user '%s'" % self.user.name)

        del entryids.Value[pos]
        del names.Value[pos]
        del flags.Value[pos]

        fbmsg.SetProps([entryids, names, flags])
        fbmsg.SaveChanges(0)

    def __unicode__(self):
        return u"Delegation('%s')" % self.user.name

    def __repr__(self):
        return _repr(self)

