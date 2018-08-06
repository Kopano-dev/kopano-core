"""
Part of the high-level python bindings for Kopano

Copyright 2018 - Kopano and its licensors (see LICENSE file for details)
"""

import datetime
import time
import re
import sys

import dateutil.parser

from MAPI import (
    FL_SUBSTRING, FL_IGNORECASE, RELOP_GT, RELOP_EQ, PT_BOOLEAN, PT_UNICODE,
    BMR_EQZ, BMR_NEZ, PT_SYSTIME, RELOP_GE, RELOP_LT, RELOP_LE, RELOP_NE,
    PT_SHORT, PT_LONG, PT_LONGLONG, PT_FLOAT, PT_DOUBLE, MNID_ID, MNID_STRING,
    PT_MV_UNICODE, MAPI_TO, MAPI_CC, MAPI_BCC
)

from MAPI.Tags import (
    PR_SUBJECT_W, PR_BODY_W, PR_MESSAGE_DELIVERY_TIME, PR_HASATTACH,
    PR_MESSAGE_SIZE, PR_MESSAGE_FLAGS, MSGFLAG_READ, PR_SENDER_NAME_W,
    PR_DISPLAY_NAME_W, PR_SENT_REPRESENTING_NAME_W, PR_SMTP_ADDRESS_W,
    PR_MESSAGE_ATTACHMENTS, PR_ATTACH_LONG_FILENAME_W, PR_MESSAGE_RECIPIENTS,
    PR_RECIPIENT_TYPE,
)

from MAPI.Struct import (
    SOrRestriction, SAndRestriction, SNotRestriction, SContentRestriction,
    SPropValue, SPropertyRestriction, SBitMaskRestriction, SSubRestriction,
)

from MAPI.Time import FileTime
from MAPI.Defs import PROP_TYPE

from .errors import ArgumentError
from .restriction import Restriction
from .defs import PSETID_Address, PS_PUBLIC_STRINGS
from .compat import fake_unicode as _unicode

from .parse import (
    ParserInput, Parser, Char, CharSet, ZeroOrMore, OneOrMore, Sequence,
    Choice, Optional, Wrapper, NoMatch
)

# TODO such grouping: 'subject:(fresh exciting)'
# TODO Regex: avoid substr
# TODO OneOrMore(regex) not needed?
# TODO operator associativity/precedence (eg 'NOT a AND b'), check MSG
# TODO escaping double quotes
# TODO asterisk not implicit for phrases
# TODO relative dates rel. to timezone (eg "received:today")
# TODO graph does not support 'size>"10 KB" and such? we now roll our own
# TODO email matching on to/cc/bcc (PR_SEARCH_KEY/PR_EMAIL_ADDRESS?)

EMAIL1_NAME = (PSETID_Address, MNID_ID, 0x8083, PT_UNICODE) # TODO merge
CATEGORY_NAME = (PS_PUBLIC_STRINGS, MNID_STRING, u'Keywords', PT_MV_UNICODE)

MESSAGE_KEYWORD_PROP = {
    'subject': PR_SUBJECT_W,
    'body': PR_BODY_W,
    'content': PR_BODY_W, # TODO what does content mean
    'received': PR_MESSAGE_DELIVERY_TIME,
    'hasattachment': PR_HASATTACH,
    'hasattachments': PR_HASATTACH,
    'size': PR_MESSAGE_SIZE,
    'read': (PR_MESSAGE_FLAGS, MSGFLAG_READ),
    'from': PR_SENT_REPRESENTING_NAME_W, # TODO email address
    'sender': PR_SENDER_NAME_W, # TODO why does 'from:user1@domain.com' work!?
    'attachment': (PR_MESSAGE_ATTACHMENTS, PR_ATTACH_LONG_FILENAME_W),
    'category': CATEGORY_NAME,
    'to': (PR_MESSAGE_RECIPIENTS, MAPI_TO),
    'cc': (PR_MESSAGE_RECIPIENTS, MAPI_CC),
    'bcc': (PR_MESSAGE_RECIPIENTS, MAPI_BCC),
    'participants': (PR_MESSAGE_RECIPIENTS, None),
}

CONTACT_KEYWORD_PROP = {
    'name': PR_DISPLAY_NAME_W,
    'email': EMAIL1_NAME,
}

USER_KEYWORD_PROP = {
    'name': PR_DISPLAY_NAME_W,
    'email': PR_SMTP_ADDRESS_W,
}

TYPE_KEYWORD_PROPMAP = {
    'message': MESSAGE_KEYWORD_PROP,
    'contact': CONTACT_KEYWORD_PROP,
    'user': USER_KEYWORD_PROP,
}

DEFAULT_PROPTAGS = {
    'message': [PR_SUBJECT_W, PR_BODY_W, PR_SENT_REPRESENTING_NAME_W],
    'contact': [PR_DISPLAY_NAME_W, EMAIL1_NAME],
    'user': [PR_DISPLAY_NAME_W, PR_SMTP_ADDRESS_W],
}

OP_RELOP = {
    '<': RELOP_LT,
    '>': RELOP_GT,
    '>=': RELOP_GE,
    '<=': RELOP_LE,
    '<>': RELOP_NE,
}

# TODO merge with freebusy version
NANOSECS_BETWEEN_EPOCH = 116444736000000000
def datetime_to_filetime(d):
    return FileTime(int(time.mktime(d.timetuple())) * 10000000 + NANOSECS_BETWEEN_EPOCH)

def _interval_restriction(proptag, start, end):
    start = datetime_to_filetime(start)
    end = datetime_to_filetime(end)

    return SAndRestriction([
        SPropertyRestriction(RELOP_GE, proptag, SPropValue(proptag, start)),
        SPropertyRestriction(RELOP_LT, proptag, SPropValue(proptag, end))
    ])

# AST nodes

class Term(object):
    def __init__(self, sign=None, field=None, op=None, value=None):
        self.sign = sign
        self.field = field
        self.op = op
        self.value = value

    def restriction(self, type_, store):
        if self.field:
            # determine proptag for term, eg 'subject'
            proptag = TYPE_KEYWORD_PROPMAP[type_][self.field]
            flag = None
            subobj = None
            recipient_type = None

            # property in sub-object (attachments/recipient): use sub-restriction
            if isinstance(proptag, tuple) and len(proptag) == 2:
                if(proptag[0]) == PR_MESSAGE_ATTACHMENTS:
                    subobj, proptag = proptag
                elif(proptag[0]) == PR_MESSAGE_RECIPIENTS:
                    subobj, recipient_type = proptag
                    proptag = PR_DISPLAY_NAME_W # TODO email
                else:
                    proptag, flag = proptag

            # named property: resolve local proptag
            elif isinstance(proptag, tuple) and len(proptag) == 4:
                proptag = store._name_id(proptag[:3]) | proptag[3]

            # comparison operator
            if self.op in ('<', '>', '>=', '<=', '<>'):
                if PROP_TYPE(proptag) == PT_SYSTIME:
                    d = dateutil.parser.parse(self.value)
                    d = datetime_to_filetime(d)
                    restr = SPropertyRestriction(
                                OP_RELOP[self.op],
                                proptag,
                                SPropValue(proptag, d)
                            )
                else:
                    value = self.value
                    unit = ''
                    if [x for x in ('KB', 'MB', 'GB') if value.endswith(x)]:
                        value, unit = value[:-2], value[-2:]

                    if PROP_TYPE(proptag) in (PT_FLOAT, PT_DOUBLE):
                        value = float(value)
                    else:
                        value = int(value)

                    if unit == 'KB':
                        value *= 1024
                    elif unit == 'MB':
                        value *= 1024**2
                    elif unit == 'GB':
                        value *= 1024**3

                    restr = SPropertyRestriction(
                                OP_RELOP[self.op],
                                proptag,
                                SPropValue(proptag, value)
                            )

            # contains/equals operator
            elif self.op in (':', '='):
                if PROP_TYPE(proptag) == PT_UNICODE:
                    restr = SContentRestriction(
                                FL_SUBSTRING | FL_IGNORECASE,
                                proptag,
                                SPropValue(proptag, self.value)
                            )

                elif flag or PROP_TYPE(proptag) == PT_BOOLEAN:
                    if flag:
                        restr = SBitMaskRestriction(
                                    BMR_NEZ if self.value in ('yes', 'true') else BMR_EQZ,
                                    proptag,
                                    flag
                                )
                    else:
                        restr = SPropertyRestriction(
                                    RELOP_EQ,
                                    proptag,
                                    SPropValue(proptag, self.value in ('yes', 'true'))
                                )

                elif PROP_TYPE(proptag) == PT_MV_UNICODE:
                    proptag2 = (proptag ^ PT_MV_UNICODE) | PT_UNICODE # funky!
                    restr = SContentRestriction(
                                FL_SUBSTRING | FL_IGNORECASE,
                                proptag,
                                SPropValue(proptag2, self.value)
                            )

                elif PROP_TYPE(proptag) in (PT_SHORT, PT_LONG, PT_LONGLONG, PT_FLOAT, PT_DOUBLE):
                    conv = float if PROP_TYPE(proptag) in (PT_FLOAT, PT_DOUBLE) else int
                    if '..' in self.value:
                        val1, val2 = self.value.split('..')
                        restr = SAndRestriction([
                            SPropertyRestriction(
                                RELOP_GE,
                                proptag,
                                SPropValue(proptag, conv(val1))
                            ),
                            SPropertyRestriction(
                                RELOP_LT,
                                proptag,
                                SPropValue(proptag, conv(val2))
                            )
                        ])
                    else:
                        restr = SPropertyRestriction(
                                    RELOP_EQ,
                                    proptag,
                                    SPropValue(proptag, conv(self.value))
                                )

                elif PROP_TYPE(proptag) == PT_SYSTIME:
                    if self.value == 'today':
                        d = datetime.datetime.now().date()
                        d2 = d + datetime.timedelta(days=1)
                        restr = _interval_restriction(proptag, d, d2)

                    elif self.value == 'yesterday':
                        d2 = datetime.datetime.now().date()
                        d = d2 - datetime.timedelta(days=1)
                        restr = _interval_restriction(proptag, d, d2)

                    elif self.value == 'this week':
                        d2 = datetime.datetime.now()
                        d = d2.date() - datetime.timedelta(days=d2.weekday())
                        restr = _interval_restriction(proptag, d, d2)

                    elif self.value == 'this month':
                        d2 = datetime.datetime.now()
                        d = d2.date() - datetime.timedelta(days=d2.day-1)
                        restr = _interval_restriction(proptag, d, d2)

                    elif self.value == 'last month':
                        now = datetime.datetime.now()
                        d2 = now.date() - datetime.timedelta(days=now.day-1)
                        d = (d2 - datetime.timedelta(days=1)).replace(day=1)
                        restr = _interval_restriction(proptag, d, d2)

                    elif self.value == 'this year':
                        d2 = datetime.datetime.now()
                        d = datetime.datetime(d2.year, 1, 1)
                        restr = _interval_restriction(proptag, d, d2)

                    elif self.value == 'last year':
                        now = datetime.datetime.now()
                        d2 = datetime.datetime(now.year, 1, 1)
                        d = datetime.datetime(d2.year-1, 1, 1)
                        restr = _interval_restriction(proptag, d, d2)

                    elif '..' in self.value:
                        date1, date2 = self.value.split('..') # TODO hours etc
                        d = dateutil.parser.parse(date1)
                        d2 = dateutil.parser.parse(date2)
                        restr = _interval_restriction(proptag, d, d2)

                    else:
                        d = dateutil.parser.parse(self.value) # TODO hours etc
                        d2 = d + datetime.timedelta(days=1)
                        restr = _interval_restriction(proptag, d, d2)

            # turn restriction into sub-restriction
            if subobj:
                if recipient_type is not None:
                    restr = SAndRestriction([
                        restr,
                        SPropertyRestriction(
                                RELOP_EQ,
                                PR_RECIPIENT_TYPE,
                                SPropValue(PR_RECIPIENT_TYPE, recipient_type)
                        )
                    ])
                restr = SSubRestriction(subobj, restr)

        else:
            defaults = [(store._name_id(proptag[:3]) | proptag[3])
                           if isinstance(proptag, tuple) else proptag
                               for proptag in DEFAULT_PROPTAGS[type_]]
            restr = SOrRestriction([
                       SContentRestriction(
                           FL_SUBSTRING | FL_IGNORECASE,
                           p,
                           SPropValue(p, self.value)
                       ) for p in defaults
                   ])

        if self.sign == '-':
            restr = SNotRestriction(restr)

        return restr

    def __repr__(self):
        return 'Term(%s%s%s%s)' % (
            self.sign or '',
            self.field or '',
            '('+(self.op or '')+')',
            self.value
        )

class Operation(object):
    def __init__(self, op=None, args=None):
        self.op = op
        self.args = args

    def restriction(self, type_, store):
        if self.op == 'AND':
            return SAndRestriction(
                [arg.restriction(type_, store) for arg in self.args]
            )
        elif self.op == 'OR':
            return SOrRestriction(
                [arg.restriction(type_, store) for arg in self.args]
            )
        elif self.op == 'NOT':
            return SNotRestriction(
                self.args[0].restriction(type_, store)
            )

    def __repr__(self):
        return '%s(%s)' % (
            self.op,
            ','.join(repr(arg) for arg in self.args)
        )

# build parser

class Regex(Parser):
    def __init__(self, regex):
        self._re = re.compile(regex)

    def parse(self, parser_input) :
        if parser_input.remaining() == 0:
            return NoMatch()
        rest = parser_input._data[parser_input._position:] # TODO slow
        match = self._re.match(rest)
        if not match:
            return NoMatch()
        else:
            n = match.end()
            parser_input.read(n)
            parser_input.inc_position(n)
            return self.match(rest[:n])

def _build_parser():
    whitespace = CharSet(' ')

    alphaspace = Regex(r'[\w \-+*@.]+')
    alphaplus = Regex(r'[\w\-+:<>=@.]+')

    word = OneOrMore(Regex(r'[\w+\-\*@\./]'))
    word.modifier = lambda t: ''.join(t)

    text = OneOrMore(alphaspace)
    text.modifier = lambda t: ''.join(t)

    quoted = Sequence(Char('"'), text, Char('"'))
    quoted.modifier = lambda t: ''.join(t)[1:-1]

    value = Choice(word, quoted)

    def op(s):
        operator = Sequence(*(Char(c) for c in s))
        operator.modifier = lambda t: ''.join(t)
        return operator

    operator = Choice(op(':'), op('='), op('<='), op('>='), op('<>'),
                      op('<'), op('>'))

    sign = CharSet('+-')

    term = Sequence(Optional(Sequence(Optional(sign), word, operator)), value)
    term.modifier = lambda t: Term(
        sign=t[0][0] if t[0] else None,
        field=t[0][1] if t[0] else None,
        op=t[0][2] if t[0] else None,
        value=t[1]
    )

    termplus = OneOrMore(alphaplus)
    termplus.modifier = lambda t: ''.join(t)

    term_fallback = Sequence(ZeroOrMore(whitespace), Choice(term, termplus))
    term_fallback.modifier = lambda t: \
        (Term(value=t[1]) if not isinstance(t[1], Term) else t[1])

    lpar = Sequence(ZeroOrMore(whitespace), op('('))
    rpar = Sequence(ZeroOrMore(whitespace), op(')'))

    expr = Wrapper()

    bracketed = Sequence(lpar, expr, rpar)
    bracketed.modifier = lambda t: t[1]

    unit = Choice(bracketed, term_fallback)

    and_ = Sequence(ZeroOrMore(whitespace), op('AND'))
    and_.modifier = lambda t: 'AND'
    or_ = Sequence(ZeroOrMore(whitespace), op('OR'))
    or_.modifier = lambda t: 'OR'
    not_ = Sequence(ZeroOrMore(whitespace), op('NOT'))
    not_.modifier = lambda t: 'NOT'

    wsexpr = Sequence(ZeroOrMore(whitespace), expr)
    wsexpr.modifier = lambda t: t[1]

    wsexpr2 = Sequence(OneOrMore(whitespace), expr)
    wsexpr2.modifier = lambda t: t[1]

    andor = Sequence(unit,
        Optional(Sequence(Optional(Choice(and_, or_)), Choice(bracketed, wsexpr))))
    def modifier(t):
        if t[1] is None:
            return t[0]
        else:
            return Operation(op=t[1][0] or 'AND', args=[t[0], t[1][1]])
    andor.modifier = modifier

    nott = Sequence(not_, Choice(bracketed, wsexpr2))
    nott.modifier = lambda t: Operation(op=t[0], args=[t[1]])

    expr.parser = Choice(nott, andor)

    return expr

_PARSER = _build_parser()

def _query_to_restriction(query, type_, store):
    query = _unicode(query)
    try:
        ast = _PARSER.parse(ParserInput(query)).value
        return Restriction(ast.restriction(type_, store))
    except Exception:
        raise ArgumentError("could not process query")
