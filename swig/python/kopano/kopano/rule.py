# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI import (
    ROW_MODIFY,
)

from MAPI.Tags import (
    PR_RULE_NAME_W, PR_RULE_STATE, PR_RULE_PROVIDER_W, PR_RULE_ACTIONS, ST_ENABLED,
    PR_RULE_CONDITION
)

from MAPI.Struct import (
    ACTIONS, ACTION, actMoveCopy, ROWENTRY, SPropValue,
)

from .compat import (
    repr as _repr, bdec as _bdec
)

from .restriction import Restriction

class Rule(object):
    """Rule class"""

    def __init__(self, mapirow, table):
        self.mapirow = mapirow
        self.table = table
        self.name = mapirow.get(PR_RULE_NAME_W)
        self.provider = mapirow.get(PR_RULE_PROVIDER_W)
        if PR_RULE_STATE in mapirow:
            self.active = bool(mapirow[PR_RULE_STATE] & ST_ENABLED)
        else:
            self.active = False

    @property
    def restriction(self):
        if PR_RULE_CONDITION in self.mapirow:
            return Restriction(mapiobj=self.mapirow[PR_RULE_CONDITION])

    def create_action(self, type_, folder=None):
        if type_ == 'move': # TODO other types
            action = ACTION(
                1, 0, None, None, 0x0,
                actMoveCopy(_bdec(folder.store.entryid), _bdec(folder.entryid))
            )

            row = []
            found = False
            for proptag, value in self.mapirow.items():
                if proptag == PR_RULE_ACTIONS:
                    found = True
                    value.lpAction.append(action)
                row.append(SPropValue(proptag, value))
            if not found:
                row.append(SPropValue(PR_RULE_ACTIONS, ACTIONS(1, [action])))

            self.table.ModifyTable(0, [ROWENTRY(ROW_MODIFY, row)])

        return Action(action)

    def actions(self):
        if PR_RULE_ACTIONS in self.mapirow:
            for action in self.mapirow[PR_RULE_ACTIONS].lpAction:
                yield Action(action)

    def __unicode__(self):
        return u"Rule('%s')" % self.name

    def __repr__(self):
        return _repr(self)

class Action(object):
    acttype_str = {
        1: 'move',
        2: 'copy',
        3: 'reply',
        4: 'oof_reply',
        5: 'defer',
        6: 'bounce',
        7: 'forward',
        8: 'delegate',
        9: 'tag',
        10: 'delete',
        11: 'mark_read',
    }

    def __init__(self, mapiobj):
        self.mapiobj = mapiobj
        self.operator = self.acttype_str[mapiobj.acttype]

    def __unicode__(self):
        return u"Action('%s')" % self.operator

    def __repr__(self):
        return _repr(self)
