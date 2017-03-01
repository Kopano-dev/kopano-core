"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI.Tags import (
    PR_RULE_NAME, PR_RULE_STATE, PR_RULE_PROVIDER, PR_RULE_ACTIONS, ST_ENABLED,
    PR_RULE_CONDITION
)

from .compat import repr as _repr, fake_unicode as _unicode

from .restriction import Restriction

class Rule(object):
    def __init__(self, mapirow):
        self.mapirow = mapirow
        self.name = _unicode(mapirow[PR_RULE_NAME])
        self.provider = _unicode(mapirow[PR_RULE_PROVIDER])
        self.active = bool(mapirow[PR_RULE_STATE] & ST_ENABLED)

    @property
    def restriction(self):
        return Restriction(mapiobj=self.mapirow[PR_RULE_CONDITION])

    def actions(self):
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
