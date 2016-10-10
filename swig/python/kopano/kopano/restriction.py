from kopano import _encode, REV_TAG

# Restriction
from MAPI.Tags import RELOP_EQ, RELOP_LE, RELOP_LT, RELOP_GE, RELOP_NE, RELOP_RE, RELOP_GT
from MAPI.Util import SPropertyRestriction, SAndRestriction, SOrRestriction, SPropValue, RES_PROPERTY, PROP_TYPE, PT_BINARY, PT_LONG

# Proptags
from MAPI.Tags import PR_MESSAGE_SIZE, PR_SUBJECT, PR_SOURCE_KEY

class ParseError(Exception):
    pass

import ply.lex as lex
import ply.yacc as yacc

mapping = {
    '>':  RELOP_GT,
    '<':  RELOP_LE,
    '==': RELOP_EQ,
    '<=': RELOP_LE,
    '=>': RELOP_GT,
    '!=': RELOP_NE,
}

rev_mapping = {v: k for k,v in mapping.items()}

# XXX: extend to supported?
propmap = {
    'size': PR_MESSAGE_SIZE,
    'subject': PR_SUBJECT,
    'sourcekey': PR_SOURCE_KEY,
}

# List of token names.   This is always required
tokens = [
   'GREATER',
   'LESS',
   'GREATER_EQUAL',
   'LESS_EQUAL',
   'EQUAL',
   'NOT_EQUAL',
   'VALUE',
   'ID',
]

reserved = {
    'and': 'AND',
    'or': 'OR',
}

tokens = tokens + list(reserved.values())

# A string containing ignored characters (spaces and tabs)
t_ignore  = ' \t'
# Regular expression rules for simple tokens
t_GREATER       = r'>'
t_GREATER_EQUAL = r'>='
t_LESS          = r'<'
t_LESS_EQUAL    = r'<='
t_EQUAL         = r'=='
t_NOT_EQUAL     = r'!='
t_VALUE         = r'[0-9]+'

def t_ID(t):
    r'[a-zA-Z_][a-zA-Z_0-9]*'
    t.type = reserved.get(t.value,'VALUE')    # Check for reserved words
    return t

# Error handling rule
def t_error(t):
    print("Illegal character '%s'" % t.value[0])
    t.lexer.skip(1)

# Build the lexer
lexer = lex.lex()

def parse_expr(p):
    left_side = p[1]
    operator = mapping.get(p[2])
    right_side = p[3]

    if left_side in propmap:
        value = right_side
        proptag = propmap.get(left_side)
    elif right_side in propmap:
        value = left_side
        proptag = propmap.get(right_side)
    else:
        raise ParseError('non supported property %s or %s used' % (right_side, left_side))

    # XXX: use property class? or some conversion magic
    if PROP_TYPE(proptag) == PT_BINARY:
        value = value.decode('hex')
    elif PROP_TYPE(proptag) == PT_LONG:
        value = int(value)

    return SPropertyRestriction(operator, proptag, SPropValue(proptag, value))


def p_expression_greater(p):
    'expression : VALUE GREATER VALUE'

    p[0] = parse_expr(p)

def p_expression_less(p):
    'expression : VALUE LESS VALUE'

    p[0] = parse_expr(p)

def p_expression_not_equal(p):
    'expression : VALUE NOT_EQUAL VALUE'

    p[0] = parse_expr(p)

def p_expression_equal(p):
    'expression : VALUE EQUAL VALUE'

    p[0] = parse_expr(p)

def p_expression_less_equal(p):
    'expression : VALUE LESS_EQUAL VALUE'

    p[0] = parse_expr(p)

def p_expression_greater_equal(p):
    'expression : VALUE GREATER_EQUAL VALUE'

    p[0] = parse_expr(p)

def p_expression_and(p):
    'expression : expression AND expression'
    p[0] = SAndRestriction([p[1], p[3]])

def p_expression_or(p):
    'expression : expression OR expression'
    p[0] = SOrRestriction([p[1], p[3]])

# Error rule for syntax errors
def p_error(p):
    raise ParseError("Syntax error in input!")

yacc.yacc()

class Restriction(object):
    def __init__(self, condition='', mapires=None):
        self.condition = condition

        if condition != '':
            mapires = yacc.parse(condition)

        if mapires:
           self.mapires = mapires
           # We can parse these
           if self.mapires.rt == RES_PROPERTY:
               self.proptag = REV_TAG.get(mapires.lpProp.ulPropTag)
               self.value = mapires.lpProp.Value
               self.operator = rev_mapping[mapires.relop]

    def __unicode__(self):
        return u"Restriction('%s')" % (self.condition)

    def __repr__(self):
        return _encode(unicode(self))
