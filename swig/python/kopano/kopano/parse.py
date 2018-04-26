# Parser building blocks taken from:
#
# https://gist.github.com/kssreeram/16549d95711a98702ed3
#
# Copyright 2015 KS Sreeram
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#
# Minor changes in class ParserInput: copyright 2018 Kopano
#

#
# ParserInput
#
# This class represents the input data and the current
# position in the data.
#
# Brief note about 'max_position':
#
# When running a complex parser, the current position moves forward
# when a particular sub-parser matches and moves backward when
# it doesn't match. The 'max_position' field tracks the farthest position
# over the entire execution of a complex parser.
# When the complex parser fails, the farthest position is a good indicator
# of the location of the error.
#
# The purpose of the 'max_position' field is to report
# the error location when the main parser doesn't match.
#

import sys

class ParserInput(object) :
    def __init__(self, data, position=0) :
        self._data = data
        self._position = position
        self._max_position = position

    def data(self) :
        return self._data

    def position(self) :
        return self._position

    def remaining(self) :
        return len(self._data) - self._position

    def read(self, n=1) :
        i = self._position
        return self._data[i:i+n]

    def inc_position(self, n=1) :
        self._position += n
        self._max_position = max(self._max_position, self._position)

    def set_position(self, position) :
        self._position = position
        self._max_position = max(self._max_position, self._position)

    def max_position(self) :
        return self._max_position


#
# Match, NoMatch
#
# A parser object returns either a Match or NoMatch object.
# The Match object contains a 'value' property that holds
# the result of parsing.
#

class Match(object) :
    def __init__(self, value) :
        self.value = value

    def __repr__(self) :
        return "Match(%s)" % (self.value,)

class NoMatch(object) :
    def __repr__(self) :
        return "NoMatch()"

def is_match(result) :
    return isinstance(result, Match)


#
# Parser
#
# This is the base-class representing parser objects.
# The 'parse' method is the method that does the parsing.
# It accepts a ParserInput object and returns either a Match
# or NoMatch object.
#
# In the case of a match, the parsed result is returned
# in the 'value' property of the Match object and the
# parser input position is advanced by the length of the input
# that was parsed.
#
# In the case of no-match, the parser input position must
# remain unchanged from what it was when the 'parse' method
# was called. If the input position was advanced during
# intermediate stages, then it should be restored back to
# what it was when the 'parse' method was called.
#
# Note on the 'match' method and 'modifier' attribute:
#
# The 'modifier' attribute can be set to a function that
# modifies the 'value' property of the Match object in the event
# of a successful match. The 'match' method is a helper method
# that applies the 'modifier' if it is present.
#

class Parser(object) :
    def match(self, value) :
        modifier = getattr(self, "modifier", None)
        if modifier is not None :
            value = self.modifier(value)
        return Match(value)

    def parse(self, parser_input) :
        raise NotImplementedError


#
# Char
#
# The Char parser parses a single character matching the character
# provided in the constructor if it is found at the current position
# and fails if it's not found or if there are no more characters left.
#

class Char(Parser) :
    def __init__(self, char) :
        self._char = char

    def parse(self, parser_input) :
        if parser_input.remaining() == 0 :
            return NoMatch()
        c = parser_input.read()
        if c != self._char :
            return NoMatch()
        parser_input.inc_position()
        return self.match(c)


#
# CharSet
#
# The CharSet parser parses a single character matching any one
# of the characters passed in the constructor. The input to the
# constructor can be a string, a list of characters, or a set of
# characters.
#

class CharSet(Parser) :
    def __init__(self, chars) :
        self._chars = chars

    def parse(self, parser_input) :
        if parser_input.remaining() == 0 :
            return NoMatch()
        c = parser_input.read()
        if c not in self._chars :
            return NoMatch()
        parser_input.inc_position()
        return self.match(c)


#
# ZeroOrMore
#
# ZeroOrMore takes another parser as input and applies it repeatedly
# until it doesn't match. All the results of the repeated application
# are collected in a list and returned as the result of parsing.
#
# This parser never fails to match because it simply returns an
# empty list as a match if the input parser doesn't match even once.
#

class ZeroOrMore(Parser) :
    def __init__(self, parser) :
        self._parser = parser

    def parse(self, parser_input) :
        values = []
        while True :
            result = self._parser.parse(parser_input)
            if not is_match(result) :
                break
            values.append(result.value)
        return self.match(values)


#
# OneOrMore
#
# OneOrMore takes another parser as input and applies it repeatedly
# until it doesn't match. All the results of the repeated application
# are collected in a list and returned as the result of parsing.
#
# This parser matches only if the input parser matches atleast once.
#

class OneOrMore(Parser) :
    def __init__(self, parser) :
        self._parser = parser

    def parse(self, parser_input) :
        values = []
        while True :
            result = self._parser.parse(parser_input)
            if not is_match(result) :
                break
            values.append(result.value)
        if len(values) == 0 :
            return NoMatch()
        return self.match(values)


#
# Sequence
#
# The Sequence parser takes N parsers as input and applies them
# one after another. Sequence succeeds in matching if all the input
# parsers succeed and doesn't match even if one of input parsers don't
# match. In the event of a successful match, the results of the input
# parsers are collected in a list and returned as the result of parsing.
#

class Sequence(Parser) :
    def __init__(self, *parsers) :
        self._parsers = parsers

    def parse(self, parser_input) :
        values = []
        saved = parser_input.position()
        for parser in self._parsers :
            result = parser.parse(parser_input)
            if not is_match(result) :
                parser_input.set_position(saved)
                return NoMatch()
            values.append(result.value)
        return self.match(values)


#
# Choice
#
# The Choice parser takes N parsers as input. It applies the first one
# and if it succeeds then the result is returned as a successful match.
# If the first parser doesn't match, then the second parser is attempted.
# This continues until one of the parsers matches. The result of the
# first matching parser is returned as the match result of
# the Choice parser. The Choice parser doesn't match if all the
# input parsers fail to match.
#

class Choice(Parser) :
    def __init__(self, *parsers) :
        self._parsers = parsers

    def parse(self, parser_input) :
        for parser in self._parsers :
            result = parser.parse(parser_input)
            if is_match(result) :
                return self.match(result.value)
        return NoMatch()


#
# Optional
#
# The Optional parser takes a parser as input and always succeeds
# in matching. If the input parser succeeds in matching, then its
# result is returned as the result of a successful match. If the
# input parser doesn't match, then None is returned as a successful
# match.
#

class Optional(Parser) :
    def __init__(self, parser) :
        self._parser = parser

    def parse(self, parser_input) :
        result = self._parser.parse(parser_input)
        if not is_match(result) :
            return self.match(None)
        return self.match(result.value)


#
# Wrapper
#
# Wrapper is a simple direct wrapper around another parser.
# The input parser is available in the 'parser' property.
# It can be leftout during construction and can be set directly
# set later. This allows for forward declaring a parser that needs
# to call itself recursively.
#

class Wrapper(Parser) :
    def __init__(self, parser=None) :
        self.parser = parser

    def parse(self, parser_input) :
        result = self.parser.parse(parser_input)
        if not is_match(result) :
            return result
        return self.match(result.value)


#
# EOF
#
# The EOF parser succeeds in matching if the input posiition
# is at the end. It fails to match otherwise.
#

class EOF(Parser) :
    def parse(self, parser_input) :
        if parser_input.remaining() == 0 :
            return self.match(None)
        else :
            return NoMatch()


#
# Example 1
#
# Parse one or more digits
#

def example1() :
    digit = CharSet("0123456789")
    digits = OneOrMore(digit)
    return digits


#
# Example 2
#
# Parse one or more digits and convert to integer
#

def example2() :

    # convert single digit to integer
    def char_to_digit(c) :
        return ord(c) - ord("0")

    # convert list of digit values to integer
    def digits_to_number(a) :
        result = 0
        for x in a :
            result = result*10 + x
        return result

    digit = CharSet("0123456789")
    digit.modifier = char_to_digit
    digits = OneOrMore(digit)
    digits.modifier = digits_to_number
    return digits


#
# Example 3
#
# An expression parser.
#

def example3() :

    # Helpers

    def char_to_digit(c) :
        return ord(c) - ord("0")

    def digits_to_number(a) :
        result = 0
        for x in a :
            result = result*10 + x
        return result

    def bracket_inner(a) :
        return a[1]

    def apply_sign(a) :
        sign = a[0]
        if sign is not None :
            return [sign, a[1]]
        else :
            return a[1]

    def make_operation_tree(a) :
        value = a[0]
        for operator, value2 in a[1] :
            value = [operator, value, value2]
        return value


    # parsers

    whitespace = ZeroOrMore(Char(" "))

    def eat_space(parser) :
        parser2 = Sequence(whitespace, parser)
        parser2.modifier = lambda a : a[1]
        return Wrapper(parser2)

    digit = CharSet("0123456789")
    digit.modifier = char_to_digit

    number = eat_space(OneOrMore(digit))
    number.modifier = digits_to_number

    expr = Wrapper()

    left_parenthesis = eat_space(Char("("))
    right_parenthesis = eat_space(Char(")"))
    bracketed = Sequence(left_parenthesis, expr, right_parenthesis)
    bracketed.modifier = bracket_inner

    sign = eat_space(CharSet("+-"))
    atomic = Sequence(Optional(sign), Choice(number, bracketed))
    atomic.modifier = apply_sign

    term_operator = eat_space(CharSet("*/"))
    term_operation = Sequence(term_operator, atomic)
    term = Sequence(atomic, ZeroOrMore(term_operation))
    term.modifier = make_operation_tree

    expr_operator = eat_space(CharSet("+-"))
    expr_operation = Sequence(expr_operator, term)

    expr.parser = Sequence(term, ZeroOrMore(expr_operation))
    expr.modifier = make_operation_tree

    return expr


#
# main
#

main_parser = None

#
# Uncomment one of the following lines.
#
# main_parser = example1()
# main_parser = example2()
# main_parser = example3()

def main() :
    if main_parser is None :
        print("Please uncomment one of the example initializer lines")
        return

    if len(sys.argv) != 2 :
        print("%s <input>" % (sys.argv[0],))
        return
    s = sys.argv[1]
    parser_input = ParserInput(s, 0)
    result = main_parser.parse(parser_input)
    if not is_match(result) :
        print("ERROR: %s" % (s,))
        i = len("ERROR: ") + parser_input.max_position()
        print("%s^" % ("-"*i,))
    else :
        print("RESULT: %s" % result.value)
        i = parser_input.position()
        data = parser_input.data()
        if i < len(data) :
            print("UNPARSED LEFTOVER: %s" % data[i:])

if __name__ == "__main__" :
    main()
