"""
Part of the high-level python bindings for Kopano

Copyright 2017 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI import (
    ROW_REMOVE, FL_PREFIX, RELOP_NE, ROW_ADD, ROW_MODIFY, MAPI_BEST_ACCESS,
    MAPI_UNICODE,
)
from MAPI.Tags import (
    PR_RULE_CONDITION, PR_RULE_ACTIONS, PR_RULE_PROVIDER_W, ACTTYPE, PR_ENTRYID,
    PR_RULE_ID, PR_RULES_TABLE, IID_IExchangeModifyTable, PR_RULE_LEVEL,
    PR_RULE_NAME_W, PR_RULE_SEQUENCE, PR_RULE_STATE, PR_RULE_PROVIDER_DATA,
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
    bdec as _bdec, repr as _repr,
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
        pos = entryids.Value.index(_bdec(self.user.userid))

        return bool(flags.Value[pos] & 1)

    @see_private.setter
    def see_private(self, b):
        fbmsg, (entryids, names, flags) = self.store._fbmsg_delgs()
        pos = entryids.Value.index(_bdec(self.user.userid))

        if b:
            flags.Value[pos] |= 1
        else:
            flags.Value[pos] &= ~1

        fbmsg.SetProps([flags])
        fbmsg.SaveChanges(0)

    @staticmethod
    def _parse_rule(store):
        userids, deletion = [], False
        for rule in store.inbox.rules():
            if PR_RULE_PROVIDER_W in rule.mapirow and PR_RULE_ACTIONS in rule.mapirow:
                if rule.mapirow[PR_RULE_PROVIDER_W] == u'Schedule+ EMS Interface':
                    actions = rule.mapirow[PR_RULE_ACTIONS].lpAction
                    if actions and actions[0].acttype == ACTTYPE.OP_DELEGATE:
                        for addrentry in actions[0].actobj.lpadrlist:
                            entryid = PpropFindProp(addrentry, PR_ENTRYID)
                            if entryid:
                                userids.append(entryid.Value)
                    if len(actions) >= 2 and actions[1].acttype == ACTTYPE.OP_DELETE:
                        deletion = True
        return userids, deletion

    @staticmethod
    def _save_rule(store, userids, deletion):
        # remove existing rule # XXX update
        for rule in store.inbox.rules():
            if rule.mapirow[PR_RULE_PROVIDER_W] == u'Schedule+ EMS Interface' and \
               PR_RULE_ID in rule.mapirow:
                pr_rule_id = rule.mapirow[PR_RULE_ID]

                rulerows = [ROWENTRY(ROW_REMOVE, [SPropValue(PR_RULE_ID, pr_rule_id)])]
                table = store.inbox.mapiobj.OpenProperty(PR_RULES_TABLE, IID_IExchangeModifyTable, 0, 0)
                table.ModifyTable(0, rulerows)

        # create new rule
        row = [
            SPropValue(PR_RULE_LEVEL, 0),
            SPropValue(PR_RULE_NAME_W, u"Delegate Meetingrequest service"),
            SPropValue(PR_RULE_PROVIDER_W, u"Schedule+ EMS Interface"),
            SPropValue(PR_RULE_SEQUENCE, 0),
            SPropValue(PR_RULE_STATE, 1),
            SPropValue(PR_RULE_PROVIDER_DATA, b''),
        ]

        actions = []
        userprops = []
        for userid in userids:
            user = store.server.gab.OpenEntry(userid, None, MAPI_BEST_ACCESS)
            userprops.append(user.GetProps(USERPROPS, MAPI_UNICODE))

        actions.append(ACTION( ACTTYPE.OP_DELEGATE, 0, None, None, 0, actFwdDelegate(userprops)))
        if deletion:
            actions.append(ACTION( ACTTYPE.OP_DELETE,  0, None, None, 0, None))
        row.append(SPropValue(PR_RULE_ACTIONS, ACTIONS(1, actions)))

        cond = SAndRestriction([SContentRestriction(FL_PREFIX, PR_MESSAGE_CLASS_W, SPropValue(PR_MESSAGE_CLASS_W, u"IPM.Schedule.Meeting")),
            SNotRestriction( SExistRestriction(PR_DELEGATED_BY_RULE) ),
            SOrRestriction([SNotRestriction( SExistRestriction(PR_SENSITIVITY)),
            SPropertyRestriction(RELOP_NE, PR_SENSITIVITY, SPropValue(PR_SENSITIVITY, 2))])
        ])
        row.append(SPropValue(PR_RULE_CONDITION, cond))
        rulerows = [ROWENTRY(ROW_ADD, row)]
        table = store.inbox.mapiobj.OpenProperty(PR_RULES_TABLE, IID_IExchangeModifyTable, 0, 0)
        table.ModifyTable(0, rulerows)

    @property
    def send_copy(self):
        """Delegate receives copies of meeting requests."""
        userids, deletion = self._parse_rule(self.store)
        return _bdec(self.user.userid) in userids

    @send_copy.setter
    def send_copy(self, value):
        userids, deletion = self._parse_rule(self.store)
        if value:
            userids.append(_bdec(self.user.userid)) # XXX dupe
        else:
            userids = [u for u in userids if u != _bdec(self.user.userid)]
        self._save_rule(self.store, userids, deletion)

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
    def _send_only_to_delegates(store):
        """Delete meetingrequests after copying them to delegates."""
        _, deletion = Delegation._parse_rule(store)
        return deletion

    @staticmethod
    def _set_send_only_to_delegates(store, value):
        userids, deletion = Delegation._parse_rule(store)
        Delegation._save_rule(store, userids, value)

    def _delete(self):
        # XXX update delegate rule

        fbmsg, (entryids, names, flags) = self.store._fbmsg_delgs()
        try:
            pos = entryids.Value.index(_bdec(self.user.userid))
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

