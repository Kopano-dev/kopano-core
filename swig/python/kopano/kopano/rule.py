# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

from MAPI import (
    ROW_MODIFY,
)

from MAPI.Tags import (
    PR_RULE_NAME_W, PR_RULE_STATE, PR_RULE_PROVIDER_W, PR_RULE_ACTIONS,
    ST_ENABLED, PR_RULE_CONDITION
)

from MAPI.Struct import (
    ACTIONS, ACTION, actMoveCopy, ROWENTRY, SPropValue,
)

from .compat import (
    bdec as _bdec
)

from .restriction import Restriction

class Rule(object):
    """Rule class.

    A rule consist of a :class:`condition <Restriction>` and a set of
    :class:`actions <Action>`. When a mail is delivered (usually in the
    inbox), and it matches the condition, the actions are executed.

    Typical actions are deletion, forwarding, copying and moving.
    """

    def __init__(self, mapirow, table):
        self.mapirow = mapirow
        self.table = table
        #: Rule name.
        self.name = mapirow.get(PR_RULE_NAME_W)
        #: Rule provider.
        self.provider = mapirow.get(PR_RULE_PROVIDER_W)
        if PR_RULE_STATE in mapirow:
            #: Is the rule active.
            self.active = bool(mapirow[PR_RULE_STATE] & ST_ENABLED)
        else:
            self.active = False

    @property
    def restriction(self):
        """Rule :class:`condition <Restriction>`."""
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
        """Rule :class:`actions <Action>`."""
        if PR_RULE_ACTIONS in self.mapirow:
            for action in self.mapirow[PR_RULE_ACTIONS].lpAction:
                yield Action(action)

    def __unicode__(self):
        return "Rule('%s')" % self.name

    def __repr__(self):
        return self.__unicode__()

class Action(object):
    """:class:`Rule <Rule>` action class."""

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
        #: Action operator (*move*, *delete*, ..)
        self.operator = self.acttype_str[mapiobj.acttype]

    def __unicode__(self):
        return "Action('%s')" % self.operator

    def __repr__(self):
        return _repr(self)
