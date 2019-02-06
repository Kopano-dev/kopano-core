#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# -*- coding: utf-8 -*-
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4
#
from .version import __version__

import binascii
from datetime import datetime
try:
    import simplejson as json
except ImportError:
    import json
import re
import sys
import time

from tabulate import tabulate
try:
    from ldap3 import Server, Connection, MODIFY_ADD, MODIFY_REPLACE, MODIFY_DELETE, \
        ALL_ATTRIBUTES, ALL, HASHED_SALTED_SHA512, ObjectDef, LEVEL, BASE, HASHED_SALTED_SHA
    from ldap3.utils.dn import safe_rdn
    from ldap3.utils.hashed import hashed
    from ldap3.core import exceptions
    from ldap3.utils.conv import escape_filter_chars
except ImportError as e:
    pass

from MAPI.Util import *
import kopano

def opt_args():
    parser = kopano.parser('skpcUPv')
    parser.add_option("--user", dest="user", action="store", help="Run script for user ")
    parser.add_option("--list", dest="listrules", action="store_true", help="Print rules")
    parser.add_option("--rule", dest="rule", action="store", type="int", help="rule id")
    parser.add_option("--state", dest="state", action="store", help="enable, disable, delete, create")
    parser.add_option("--empty-rules", dest="emptyRules", action="store_true", help="Empty the rules table for a specific user")
    parser.add_option("--create", dest="createrule", action="store",
                      help="create rule, use  --conditions and --actions")
    parser.add_option("--conditions", dest="conditions", action="append", default=[], help="conditions")
    parser.add_option("--actions", dest="actions", action="append", default=[],help="actions")
    parser.add_option("--exceptions", dest="exceptions", action="append", default=[], help="exceptions")
    parser.add_option("--stop-processing", dest="StopProcessingRules", action="store_true", help="Stop processing more rules on this message")
    parser.add_option("--create-if-missing", dest="CreateFolder", action="store_true", help="Create folder if not exist")
    parser.add_option("--import-exchange-rules", dest="importFile", action="store", help="Json file from exchange")
    parser.add_option("--ldap-config", dest="configFile", action="store", help="Config file for LDAP options")
    return parser.parse_args()


class KopanoRules():
    def __init__(self, server, user, conditions, configFile=None,importFile=None, CreateFolder=False):
        self.server = server
        self.conditions = conditions
        self.user = user
        self.configFile = configFile
        self.importFile = importFile
        self.CreateFolder = CreateFolder

    def _legacyExchangeDN(self, exchangeDN):
        try:
            con = Connection(LDAP.URL, LDAP.BINDDN, LDAP.PASSWORD, auto_bind=True, raise_exceptions=True)
        except exceptions.LDAPBindError:
            print('LDAP: Unauthorized')
            sys.exit(1)
        except (exceptions.LDAPInvalidCredentialsResult, exceptions.LDAPInvalidDNSyntaxResult):
            print('LDAP: Username or password in correct')
            sys.exit(1)

        if 'EX' in exchangeDN:
            exchangeDN = exchangeDN.split('[EX:')[1][:-1]

        exchangeDN = escape_filter_chars(exchangeDN)

        search = con.search(LDAP.BASEDN,'(legacyExchangeDN={})'.format(exchangeDN),attributes=['mail'])
        if search and len(con.entries) > 0:
            return json.loads(con.entries[0].entry_to_json())['attributes']['mail'][0]

        return None


    def _get_email(self, user):
        if self.importFile:
            if self.configFile:
                user = self._legacyExchangeDN(user)
                if not user:
                    return None
            else:
                users = re.findall('"([^"]*)"', user)
                if len(users) > 0:
                    user = users[0]
        try:
            username = self.server.user(email=user)
            check = True
        except kopano.NotFoundError:
            check = False

        if check:
            self.mail = 'ZARAFA:%s' % user.upper()
            self.fromname = u'%s <%s>' % (username.name, user)
            self.name = username.name
            self.entryid = username.userid
            self.protocol = u'ZARAFA'
            self.fullname = username.fullname
        else:
            self.mail = 'SMTP:%s' % user.upper()
            self.fromname = u'%s <%s>' % (user, user)
            self.fullname = user
            self.name = user
            self.entryid = binascii.hexlify(self.mail.encode('utf-8'))  # XXXX can be broken.
            self.protocol = u'SMTP'

    def received_from(self):
        user_list = []
        for user in self.conditions:
            self._get_email(user)
            user_list.append(SCommentRestriction(
                        SPropertyRestriction(4, 0xc1d0102, SPropValue(0x00010102, self.mail.encode('utf-8'))),
                        [SPropValue(0x60000003, 1), SPropValue(0x00010102, self.mail.encode('utf-8')),
                         SPropValue(0x0001001F, self.fromname), SPropValue(0x39000003, 0)]))

        return user_list

    def sent_to(self):
        user_list  = []
        for user in self.conditions:
            self._get_email(user)
            user_list.append(SSubRestriction(0xe12000d, SCommentRestriction(
                        SPropertyRestriction(4, 0x300b0102, SPropValue(0x00010102, self.mail.encode('utf-8'))),
                        [SPropValue(0x60000003, 1), SPropValue(0x00010102, self.mail.encode('utf-8')),
                         SPropValue(0x0001001F, self.fromname), SPropValue(0x39000003, 0)])))
        return user_list

    def importance(self):
        importancevalue = {"low": 0,
                           "normal": 1,
                           "high": 2}
        getlevel = importancevalue[self.conditions[0].lower()]
        return SPropertyRestriction(4, 0x170003, SPropValue(0x00170003, getlevel))


    def sent_only_to_me(self):
        return SAndRestriction([SPropertyRestriction(4, 0x57000b, SPropValue(0x0057000B, True)),
                                                SNotRestriction(SContentRestriction(1, 0xe04001f,
                                                                                    SPropValue(0x0E04001F, u';'))),
                                                SPropertyRestriction(4, 0xe03001f, SPropValue(0x0E03001F, u''))])

    def contain_word_sender_address(self):
        return_list = []
        for word in self.conditions:
                return_list.append(SContentRestriction(1, 0xc1d0102, SPropValue(0x0C1D0102, str.encode(word))))

        return return_list

    def contain_word_recipient_address(self):
        return_list = []
        for word in self.conditions:
            return_list.append(SContentRestriction(1, 0x300b0102,SPropValue(0x300B0102, str.encode(word.upper()))))
        if len(return_list) > 1:
            return SSubRestriction(0x0E12000D, SOrRestriction(return_list))
        else:
            return SSubRestriction(0x0E12000D, return_list[0])

    def contain_word_in_subject(self):
        return_list = []
        for word in self.conditions:
            return_list.append(SContentRestriction(65537, 0x37001f, SPropValue(0x0037001F, u'{}'.format(word))))
        return return_list

    def contain_word_in_body(self):
        return_list = []
        for word in self.conditions:
            return_list.append(SContentRestriction(65537, 0x1000001f, SPropValue(0x1000001F, u'{}'.format(word))))
        return return_list

    def contain_word_in_header(self):
        return_list = []
        for word in self.conditions:
            return_list.append(SContentRestriction(65537, 0x7d001f, SPropValue(0x007D001F, u'{}'.format(word))))
        return return_list

    def message_size(self):
        if self.conditions[0] <= self.conditions[1]:
            lowvalue = self.conditions[0]
            highvalue = self.conditions[1]
        else:
            lowvalue = self.conditions[1]
            highvalue = self.conditions[0]

        return SAndRestriction([SPropertyRestriction(2, 0xe080003, SPropValue(0x0E080003, int(lowvalue) * 10)),
                                SPropertyRestriction(0, 0xe080003, SPropValue(0x0E080003, int(highvalue) * 10))])

    def received_date(self, exception=False):
        date1 = MAPI.Time.unixtime(time.mktime(datetime.strptime(self.conditions[0], '%d-%m-%Y').timetuple()))
        date2 = MAPI.Time.unixtime(time.mktime(datetime.strptime(self.conditions[1], '%d-%m-%Y').timetuple()))
        if date1 <= date2:
            datelow = date1
            datehigh = date2
        else:
            datelow = date2
            datehigh = date1
        if exception:
            return SAndRestriction([SNotRestriction(SOrRestriction([
                SContentRestriction(65538,0x1a001f,SPropValue(0x001A001F, u'IPM.Schedule.Meeting.Request')),
                SContentRestriction(65538,0x1a001f,SPropValue(0x001A001F, u'IPM.Schedule.Meeting.Canceled'))]
            ))])

        return SAndRestriction([SPropertyRestriction(2, 0xe060040, SPropValue(0x0E060040, datelow)),
                                SPropertyRestriction(0, 0xe060040, SPropValue(0x0E060040, datehigh))])

    def name_in_to_cc(self):
        return SPropertyRestriction(4, 0x59000b, SPropValue(0x0059000B, True))

    def name_in_to(self):
        return SPropertyRestriction(4, 0x57000b, SPropValue(0x0057000B, False))

    def name_in_cc(self):
        return SAndRestriction([SPropertyRestriction(4,0x57000b,SPropValue(0x0057000B, True)),
                                SNotRestriction(SContentRestriction(1,0xe04001f,SPropValue(0x0E04001F, u';'))),
                                SPropertyRestriction(4,0xe03001f,SPropValue(0x0E03001F, u''))])

    def only_sent_to_me(self):
        return SAndRestriction([SPropertyRestriction(4,0x57000b,SPropValue(0x0057000B, True)),
                                SNotRestriction(SContentRestriction(1,0xe04001f,SPropValue(0x0E04001F, u';'))),
                                SPropertyRestriction(4,0xe03001f,SPropValue(0x0E03001F, u''))])

    def has_attachment(self):
        return SBitMaskRestriction(1, 0xe070003, 16)

    def sensitivity(self):
        sens = {'normal': 0,
                'personal': 1,
                'private': 2,
                'confidential': 3,
                'companyconfidential': 3
        }

        return SPropertyRestriction(4, 0x360003, SPropValue(0x00360003, sens[self.conditions[0].lower()]))

    def meeting_request(self):
        return SOrRestriction(
            [SContentRestriction(65538,0x1a001f,SPropValue(0x001A001F, u'IPM.Schedule.Meeting.Request')),
             SContentRestriction(65538,0x1a001f,SPropValue(0x001A001F, u'IPM.Schedule.Meeting.Canceled'))]
        )

    def _forward_redirect_users(self):
        user_list = []
        for user in self.conditions:
            self._get_email(user)
            user_list.append([SPropValue(0x0FFF0102,
                                           binascii.unhexlify(self.entryid)),
                                SPropValue(0x0FFE0003, 6), SPropValue(0x3001001F, u'%s' % self.fullname),
                                SPropValue(0x39000003, 0), SPropValue(0x3003001F, u'%s' % self.name),
                                SPropValue(0x39FE001F, u'%s' % user), SPropValue(0x3002001F, self.protocol),
                                SPropValue(0x0C150003, 1), SPropValue(0x300B0102, self.mail.encode('utf-8'))])

        return user_list


    def forward_to(self):
        user_list = self._forward_redirect_users()
        if len(user_list) > 0:
            return ACTION(7, 0, None, None, 0x0, actFwdDelegate(user_list))

    def redirect_to(self):
        user_list = self._forward_redirect_users()
        if len(user_list) > 0:
            return ACTION(7, 3, None, None, 0x0, actFwdDelegate(user_list))

    def forward_as_attachment(self):

        user_list = self._forward_redirect_users()
        if len(user_list) > 0:
            return ACTION(7, 4, None, None, 0x0, actFwdDelegate(user_list))

    def _get_folder(self):

        try:
            self.user = self.server.user(self.conditions[1])
            complete_tree = self.conditions[0]
        except kopano.NotFoundError:
            self.user = self.server.user(self.user)
            complete_tree = '/'.join(self.conditions)
        except IndexError:
            self.user = self.server.user(self.user)
            complete_tree = self.conditions[0]

        self.folder = self.user.store.folder(complete_tree, create=self.CreateFolder)


    def move_to(self):
        self._get_folder()
        return ACTION(1, 0, None, None, 0x0, actMoveCopy(binascii.unhexlify(self.user.store.entryid),
                                                         binascii.unhexlify(self.folder.entryid)))

    def copy_to(self):
        self._get_folder()
        return ACTION(2, 0, None, None, 0x0, actMoveCopy(binascii.unhexlify(self.user.store.entryid),
                                                         binascii.unhexlify(self.folder.entryid)))
    def delete(self):
        user_store = self.server.user(self.user).store
        wastebasket = user_store.wastebasket.entryid
        return ACTION(1, 0, None, None, 0x0, actMoveCopy(binascii.unhexlify(user_store.entryid),
                                                         binascii.unhexlify(wastebasket)))
    def mark_as_read(self):
        return ACTION(11, 0, None, None, 0x0, None)

    def mark_as_junk(self):
        user_store = self.server.user(self.user).store
        junk = user_store.junk.entryid
        return ACTION(1, 0, None, None, 0x0, actMoveCopy(binascii.unhexlify(user_store.entryid),
                                                         binascii.unhexlify(junk)))
    def mark_as_importance(self):
        importance = {'low': 0,
                      'normal': 1,
                      'high': 2}
        return ACTION(9, 0, None, None, 0x0, SPropValue(PR_IMPORTANCE, importance[self.conditions[0].lower()]))


def convertcondition(conditions): ## TODO make this nicer
    condition_message = ''
    conlist = []
    if isinstance(conditions, SAndRestriction):
        conditions = conditions.lpRes
    elif isinstance(conditions, SOrRestriction):
        conditions = conditions.lpRes
    elif isinstance(conditions, SCommentRestriction):
        conditions = [conditions.lpRes]
    elif isinstance(conditions,SPropertyRestriction):
        conditions = [conditions]
    elif isinstance(conditions,SSubRestriction):
        conditions = [conditions.lpRes]
    elif isinstance(conditions,SContentRestriction):
        conditions = [conditions]
    elif isinstance(conditions,SBitMaskRestriction):
        conditions = [conditions]
    elif isinstance(conditions,SExistRestriction):
        conditions = [conditions]

    for condition in conditions:
        connum = 0

        # SExistRestriction
        if isinstance(condition, SExistRestriction):
            proptag = hex(condition.ulPropTag)
            if proptag == '0x1a001e':
                condition_message += "Is received (all messages)\n"

        # SOrRestriction
        if isinstance(condition, SOrRestriction):
            totaladdresses = len(condition.lpRes)
            numaddress = 0
            for addresses in condition.lpRes:
                if isinstance(addresses, SCommentRestriction):
                    proptag = hex(addresses.lpRes.ulPropTag)
                    if proptag not in conlist:
                        conlist.append(proptag)
                        if proptag == "0xc1d0102":
                            condition_message += "Is received from "
                        if proptag == '0x300b0102':
                            condition_message += "Is sent to: "

                    user = addresses.lpRes.lpProp.Value.replace(b'ZARAFA:', b'').lower()
                    condition_message += "{} \n".format(user.decode('utf-8'))


                if isinstance(addresses, SContentRestriction):
                    proptag = hex(addresses.ulPropTag)
                    if proptag not in conlist:
                        conlist.append(proptag)
                        if proptag == '0xc1d0102':
                            condition_message += "Includes these word(s) in the sender's address: "
                        if proptag == '0x37001f':
                            condition_message += "Includes these word(s) in the subject: "
                        if proptag == '0x1000001f':
                            condition_message += "Includes these word(s) in the body: "
                        if proptag == '0x7d001f':
                            condition_message += "Includes these word(s) in the header: "
                        if proptag == '0x1a001f':
                            condition_message += "Which is a meeting invitation or update"
                        if proptag == '0x300B0102':
                            condition_message += "Includes these word(s) in the recipient address:"



                    if proptag != '0x1a001f':
                        try:
                            if isinstance(condition.lpProp.Value, bytes):
                                words = condition.lpProp.Value.decode('utf-8')
                            else:
                                words = condition.lpProp.Value
                            condition_message += u"{} \n".format(words)
                        except AttributeError as e:
                            print(e, condition)

                numaddress += 1
                if numaddress != totaladdresses and totaladdresses != 1:
                    condition_message += ", "
                if numaddress == totaladdresses and totaladdresses != 1:
                    condition_message += "\n"

        # single.
        if isinstance(condition, SContentRestriction):

            if not SContentRestriction in conlist:
                conlist.append(SContentRestriction)
                proptag = hex(condition.ulPropTag).lower()
                if proptag == '0xc1d0102':
                    condition_message += "Includes these word(s) in the sender's address: "
                if proptag == '0x37001f':
                    condition_message += "Includes these word(s) in the subject: "
                if proptag == '0x1000001f':
                    condition_message += "Includes these word(s) in the body: "
                if proptag == '0x7d001f':
                    condition_message += "Includes these word(s) in the header: "
                if proptag == '0x1a001f':
                    condition_message += "Which is a meeting invitation or update"
                if proptag == '0x300B0102':
                    condition_message += "Includes these word(s) in the recipient address:"


            if proptag != '0x1a001f':
                if isinstance(condition.lpProp.Value, bytes):
                    words = condition.lpProp.Value.decode('utf-8')
                else:
                    words = condition.lpProp.Value
                condition_message += u"{} \n".format(words)



        if isinstance(condition, SPropertyRestriction):
            proptag = hex(condition.ulPropTag)
            if proptag not in conlist:
                conlist.append(proptag)
                if proptag == "0xc1d0102":
                    user = condition.lpProp.Value.replace(b'ZARAFA:', b'').replace(b'SMTP:', b'').lower()
                    condition_message += "Is received from: {} \n".format(user.decode('utf-8'))

                if proptag == '0x57000b' and condition.lpProp.Value:
                    condition_message += "Is sent only to me \n"
                if proptag == '0x57000b' and not condition.lpProp.Value and '0x58000b' not in conlist:
                    condition_message += "When my name is in the To box\n"
                if proptag == '0x58000b':
                    condition_message += "When my name is in the Cc box\n"
                if proptag == '0x59000b' and '0x58000b' not in conlist:
                    condition_message += "When my name is in the To or Cc box\n"

            if proptag == '0x360003':
                sens = {0: 'normal',
                        1: 'personal',
                        2: 'private',
                        3: 'confidential',
                        }
                condition_message += "If it is marked as %s sensitivity\n" % sens[condition.lpProp.Value]

            if proptag == '0x170003':
                importancevalue = {0: "low",
                                   1: "normal",
                                   2: "high"}
                condition_message += "Has importance %s \n" % importancevalue[int(condition.lpProp.Value)]

            if proptag == '0xe080003':
                if proptag not in conlist:
                    conlist.append(proptag)
                    condition_message += "if received "
                if condition.relop == 2:
                    condition_message += "at least %s kb " % (int(condition.lpProp.Value) / 10)
                if condition.relop == 0:
                    condition_message += "and at most %s kb " % (int(condition.lpProp.Value) / 10)

            if proptag == '0xe060040':
                if proptag not in conlist:
                    conlist.append(proptag)
                    condition_message += "If received "
                if condition.relop == 2:
                    condition_message += "After %s " % condition.lpProp.Value
                if condition.relop == 0:
                    condition_message += "and before %s " % condition.lpProp.Value

        if isinstance(condition, SBitMaskRestriction):
            condition_message += "Which has an attachment \n"
        if isinstance(condition, SCommentRestriction):
            proptag = hex(condition.lpRes.ulPropTag)
            if proptag not in conlist:
                conlist.append(proptag)
                if proptag == '0xc1d0102':
                    condition_message += "Is received from: "
                if proptag == '0x300b0102':
                    condition_message += "Is sent to: "

            user = condition.lpRes.lpProp.Value.replace(b'ZARAFA:', b'').replace(b'SMTP:', b'').lower()
            condition_message += "{} \n".format(user.decode('utf-8'))

        # multiple values
        if isinstance(condition, SAndRestriction):
            for andcon in condition.lpRes:
                if isinstance(andcon, SPropertyRestriction):
                    proptag = hex(andcon.ulPropTag)
                    if proptag == '0x57000b' and andcon.lpProp.Value:
                        condition_message += "Is sent only to me \n"
                    if proptag == '0x57000b' and not andcon.lpProp.Value:
                       condition_message += "When my name is in the Cc box\n"
                    if proptag == '0x58000b':
                        condition_message += "When my name is in the To box\n"
                    if proptag == '0x59000b':
                        condition_message += "When my name is in the To or Cc box\n"
                    if proptag == '0x360003':
                        sens = {0: 'normal',
                                1: 'personal',
                                2: 'private',
                                3: 'confidential',
                                }
                        condition_message += "If it is marked as %s sensitivity\n" % sens[andcon.lpProp.Value]
                    if proptag == '0x170003':
                        importancevalue = {0: "low",
                                           1: "normal",
                                           2: "high"}
                        condition_message += "Has importance %s \n" % importancevalue[int(andcon.lpProp.Value)]
                    if proptag == '0xe080003':
                        if proptag not in conlist:
                            conlist.append(proptag)
                            condition_message += "If received "
                        if andcon.relop == 2:
                            condition_message += "At least %s kb " % (int(andcon.lpProp.Value) / 10)
                        if andcon.relop == 0:
                            condition_message += "and at most %s kb \n" % (int(andcon.lpProp.Value) / 10)
                    if proptag == '0xe060040':
                        if proptag not in conlist:
                            conlist.append(proptag)
                            condition_message += "If received "
                        if andcon.relop == 2:
                            condition_message += "After %s " % andcon.lpProp.Value
                        if andcon.relop == 0:
                            condition_message += "and before %s \n" % andcon.lpProp.Value

                if isinstance(condition, SBitMaskRestriction):
                    condition_message += "Which has an attachment\n"

        #exceptions at the end
        if isinstance(condition, SNotRestriction):
            # create list, so we don't need to run this part twice
            if isinstance(condition.lpRes, SOrRestriction):
                listconditions = condition.lpRes.lpRes
                countconditions = len(condition.lpRes.lpRes)
            else:
                listconditions =  [condition.lpRes]
                countconditions = len(listconditions)
            for cond in listconditions:
                if isinstance(cond, SCommentRestriction):
                    content = cond.rt
                    email = cond.lpRes.lpProp.Value.replace(b'ZARAFA:', '').replace(b'SMTP:', '').lower()
                    if content == 10:
                        if connum == 0:
                            condition_message += 'NOT received from: '
                        condition_message += email

                if isinstance(cond, SContentRestriction):


                    proptag = hex(cond.ulPropTag)
                    if proptag not in conlist:
                        conlist.append(proptag)
                        if 'Except' in condition_message:
                            condition_message += "\n"
                        if proptag == '0xc1d0102':
                            condition_message += "Except if the sender's address contains: "
                        if proptag == '0x37001f':
                            condition_message += "Except if the subject contains: "
                        if proptag == '0x1000001f':
                            condition_message += "Except if the body contains: "
                        if proptag == '0x7d001f':
                            condition_message += "Except if the header contains: "
                        if proptag == '0x1a001f':
                            condition_message += "Except if meeting invitation or update"
                        if proptag == '0x300B0102':
                            condition_message += "Except if the recipient address contains:"
                    if proptag != '0x1a001f':
                        condition_message += '{}'.format(cond.lpProp.Value)

                if isinstance(cond, SAndRestriction):
                    andnum = 0
                    for andcon in cond.lpRes:
                        if isinstance(andcon, SPropertyRestriction):
                            proptag = hex(andcon.ulPropTag)
                            if proptag == '0x57000b':
                                condition_message += "Except if sent only to me \n"
                            if proptag == '0x57000b' and not andcon.lpProp.Value:
                                condition_message += "Except when my name is in the Cc box\n"
                            if proptag == '0xe080003':
                                if andnum ==0:
                                    condition_message += "Except with a size "
                                if andcon.relop == 2:
                                    condition_message += "at least %s kb " % (int(andcon.lpProp.Value) /10)
                                if andcon.relop == 0:
                                    condition_message += "and at most %s kb \n" % (int(andcon.lpProp.Value) /10)
                            if proptag == '0xe060040':
                                if andnum == 0:
                                    condition_message += "Except if received "
                                if andcon.relop == 2:
                                    condition_message += "after %s " % andcon.lpProp.Value
                                if andcon.relop == 0:
                                    condition_message += "and before %s \n" % andcon.lpProp.Value
                        andnum += 1



                if isinstance(cond, SPropertyRestriction):
                    proptag = hex(cond.ulPropTag)
                    if proptag == '0x57000b':
                        condition_message += "Except when my name is in the To box\n"
                    if proptag == '0x59000b':
                        condition_message += "Except when my name is in the To or Cc box\n"
                    if proptag == '0x360003':
                        sens = {0: 'normal',
                                1: 'personal',
                                2: 'private',
                                3: 'confidential',
                        }
                        condition_message += "Except if it is marked as %s sensitivity\n" % sens[cond.lpProp.Value]
                    if proptag == '0x170003':
                        sens = {0: 'low',
                                1: 'normal',
                                2: 'high',
                                }
                        condition_message += "Except if it is marked as %s importance\n" % sens[cond.lpProp.Value]

                connum += 1
                if connum != countconditions and countconditions != 1:
                    condition_message += ", "
                if connum == countconditions and countconditions != 1:
                    condition_message += "\n"

    return condition_message


def convertaction(action, user,server):

    action_message = ''
    movetype = {1: 'Move',
                2: 'Copy',}
    try:
        countact = len(action.Value.lpAction)
    except AttributeError:
        return 'Unknown  action'


    num = 0
    for act in action.Value.lpAction:

        if not act.actobj:
            if act.acttype == 11:
                action_message += 'Mark item as read'
        if isinstance(act.actobj, actDeferAction):

            action_message += 'Deferred  action (client side rule)'

        if isinstance(act.actobj, actMoveCopy):
            folderid = binascii.hexlify(act.actobj.FldEntryId)
            storeid = act.actobj.StoreEntryId
            try:
                try:
                    foldername = user.store.folder(entryid=folderid).name
                except TypeError:
                    foldername = user.store.folder(folderid).name
            except kopano.NotFoundError:
                try:
                    mapistore = server.mapisession.OpenMsgStore(0, storeid, IID_IMsgStore, MDB_WRITE)
                    newstore = kopano.Store(mapiobj=mapistore, server=server)

                    try:
                        foldername = '%s (%s)' % (newstore.folder(entryid=folderid).name, newstore.user.name)
                    except TypeError:
                        foldername = '%s (%s)' % (newstore.folder(folderid).name, newstore.user.name)
                except AttributeError:
                    foldername = 'Folder not available'
                except MAPI.Struct.MAPIErrorNotFound:
                    foldername = 'unknown'


            if act.acttype == 1:
                if folderid == binascii.hexlify(user.store.prop(PR_IPM_WASTEBASKET_ENTRYID).value):
                    action_message += 'Delete message'
                else:
                    action_message += "%s message to folder '%s'" % (movetype[act.acttype], foldername)
            else:
                action_message += "%s message to folder '%s'" % (movetype[act.acttype], foldername)

        if isinstance(act.actobj, actFwdDelegate):
            sendstype = {0: 'Forward message to ',
                         3: 'Redirect message to ',
                         4: 'Forward the message as attachment to '}

            countaddress = len(act.actobj.lpadrlist)
            addnum = 0
            action_message += sendstype[act.ulActionFlavor]
            for addresses in act.actobj.lpadrlist:
                action_message += addresses[5].Value

                addnum += 1
                if addnum != countaddress and countaddress != 1:
                    action_message += ","

        num += 1
        if num != countact and countact != 1:
            action_message += "\n"

    return action_message


def printrules(filters, user, server):
    rulestate = {ST_DISABLED:                      "Disabled",
                 ST_ONLY_WHEN_OOF:                 "Disabled",
                 ST_EXIT_LEVEL:                    "Disabled",
                 ST_ONLY_WHEN_OOF | ST_EXIT_LEVEL: "Disabled",
                 ST_ENABLED:                       "Enabled",
                 ST_ERROR:                         "Error",
                 ST_ENABLED | ST_ERROR:            "Enabled but error reported",
                 ST_ENABLED | ST_EXIT_LEVEL:       "Enabled (stop further rules)",
                 ST_ENABLED | ST_ONLY_WHEN_OOF:    "Only active while OOF",
                 ST_ENABLED | ST_ONLY_WHEN_OOF | ST_EXIT_LEVEL: "Only active while OOF (stop further rules)"}

    table_header = ["Number", "Name", "Condition", "Action", "State"]
    table_data = []
    for rule in filters:
        condition_message = convertcondition(rule[4].Value)
        actions = convertaction(rule[5], user,server)
        name = rule[7].Value
        condition = condition_message
        try:
            table_data.append(
                [rule[0].Value, name, condition, actions,
                rulestate[rule[2].Value]])
        except KeyError:
            continue

    print(tabulate(table_data, headers=table_header,tablefmt="grid"))


def changerule(filters, number, state):
    rowlist = ''
    convertstate = {'':           ST_ENABLED,
                    '-if-oof':    ST_ONLY_WHEN_OOF,
                    '-stop-here': ST_EXIT_LEVEL
                   }
    try:
        rule = filters[number - 1]
    except IndexError:
        print('Rule does not exist')
        sys.exit(1)
    for prop in rule:
        if prop.ulPropTag == PR_RULE_ACTIONS:
            actions = prop.Value
        if prop.ulPropTag == PR_RULE_CONDITION:
            conditions = prop.Value
        if prop.ulPropTag == PR_RULE_NAME:
            name = prop.Value
        if prop.ulPropTag == PR_RULE_SEQUENCE:
            sequence = prop.Value
        if prop.ulPropTag == PR_RULE_PROVIDER_DATA:
            provider_data = binascii.hexlify(prop.Value)
        if prop.ulPropTag == PR_RULE_STATE:
            state_value = prop.Value

    update_state  = False

    try:
        if state[:6] == 'enable':
            update_state = True
            state_value = state_value | convertstate[state[6:]]

        elif state[:7] == 'disable':
            update_state = True
            state_value = state_value & ~convertstate[state[7:]]

    except:
        print('State-flag does not exist')
        sys.exit(1)

    if update_state:
        rowlist = [ROWENTRY(
           ROW_MODIFY,
            [SPropValue(PR_RULE_ID, number),
             SPropValue(PR_RULE_PROVIDER_DATA, binascii.unhexlify(provider_data)),
             SPropValue(PR_RULE_STATE, state_value),
             SPropValue(PR_RULE_ACTIONS, actions),
             SPropValue(PR_RULE_CONDITION, conditions),
             SPropValue(PR_RULE_SEQUENCE, sequence),
             SPropValue(1719730206, b'RuleOrganizer'),
             SPropValue(PR_RULE_NAME, name)
            ])]

    elif state == 'delete':
        rowlist = [ROWENTRY(
            ROW_REMOVE,
            [SPropValue(PR_RULE_ID, number)]
        )]
    else:
        return

    return rowlist, name


def createrule(server, name, lastid, user, conditions=None, actions=None, exceptions=None, StopProcessingRules=False,
               CreateFolder=False, configFile=None, importFile=None):

    '''
    Create kopano rule based on given options
    :param options: dict with the following keys:
        user: User where this rule applies to
        createrule: name of the rule
        actions: list of actions of this rule
        conditions: list of conditions of this rule
        exceptions: list of exceptions of this rule
    :param lastid: last rule id that is knonw
    :return:
    '''

    if not conditions:
        conditions = []
    if not actions:
        actions = []
    if not exceptions:
        exceptions = []

    storeconditions = []
    storeactions = []
    storeexceptions = []

    SOrRestriction_list = ['received-from', 'sent-to', 'contain-word-sender-address', 'contain-word-in-subject',
                           'contain-word-in-body', 'contain-word-in-header']
    '''
    Search for conditions that are known Kopano rules   conditions
    '''
    for condition in conditions:
        conditionslist = []
        splitcondition = condition.split(':', 1)
        condition_rule = splitcondition[0]
        try:
            condition_var = splitcondition[1].split(',')
        except IndexError:
            condition_var = ''
        rules = KopanoRules(server, user, condition_var, configFile,importFile, CreateFolder)


        '''
        Try to get the attribute based on parameter.
        if they don't exist just print the unknown attribute and continue
        '''
        try:
            condition_method = getattr(rules, condition_rule.replace('-', '_'))
            conditionslist.append(condition_method())
        except AttributeError as e:
            print('the Following Attribute is not known "{}"'.format(e))

        '''
            If the condition_rule is higher then 1 add the SOrRestriction attribute before the list
        '''

        if condition_rule in SOrRestriction_list:
            if len(conditionslist) > 1 or (conditionslist[0] and len(conditionslist[0]) > 1):
                storeconditions.append(SOrRestriction(conditionslist[0]))
            elif len(conditionslist) == 1:
                storeconditions.append(conditionslist[0][0])
        elif len(conditionslist) == 1:
            storeconditions.append(conditionslist[0])

    '''
    Search for actions that are known Kopano rules actions
    '''
    for action in actions:
        splitactions = action.split(':')
        action_rule = splitactions[0]
        if len(splitactions) > 1:
            action_var = splitactions[1].split(',')
        else:
            action_var = splitactions[0]
        '''
        Try to get the attribute based on parameter.
        if they don't exist just print the unknown attribute and continue
        '''
        rules = KopanoRules(server, user, action_var, configFile,importFile, CreateFolder)
        try:
            action_method = getattr(rules, action_rule.replace('-', '_'))
            tmp = action_method()
            if tmp:
                storeactions.append(tmp)
        except AttributeError as e:
            print('the Following Attribute is not known "{}"'.format(e))
        except kopano.NotFoundError as e:
            print('rule {} "{}"'.format(name, e))
            return
    '''
    Search for exceptions that are known Kopano rules exceptions
    same as conditions with the differents that the SNotRestriction attibute is added to the rule
    '''
    for exception in exceptions:
        splitexception = exception.split(':')
        exception_rule = splitexception[0]
        try:
            exception_var = splitexception[1].split(',')
        except IndexError:
            exception_var = ''
        exceptionslist = []
        rules = KopanoRules(server, user, exception_var, configFile,importFile, CreateFolder)
        '''
        Try to get the attribute based on parameter.
        if they don't exist just print the unknown attribute and continue
        '''
        try:
            exception_method = getattr(rules, exception_rule.replace('-', '_'))
            if exception_rule == 'meeting_request':
                exceptionslist.append(exception_method(True))
            else:
                exceptionslist.append(SNotRestriction(exception_method()))
        except AttributeError as e:
            print('the Following Attribute is not known "{}"'.format(e))

        '''
        If the exceptionslist is higher then 1 add the SOrRestriction attribute before the list
        '''
        if exception_rule in SOrRestriction_list:
            if len(exceptionslist) > 1 or (exceptionslist[0] and len(exceptionslist[0]) > 1):
                storeexceptions.append(SNotRestriction(SOrRestriction(exceptionslist[0])))
            elif len(exceptionslist) == 1:
                storeexceptions.append(SNotRestriction(exceptionslist[0][0]))
        elif len(exceptionslist) == 1:
            storeconditions.append(exceptionslist[0])

    #combine conditions and exceptions
    if len(storeexceptions) > 0:
        storeconditions = storeconditions + storeexceptions

    # combine all conditions
    if len(storeconditions) > 1:
        returncon = SAndRestriction(storeconditions)
    else:
        returncon = storeconditions[0]

    rule_state = ST_ENABLED
    if StopProcessingRules:
        rule_state = 17
    rowlist = [ROWENTRY(
        ROW_ADD,
        [SPropValue(PR_RULE_STATE, rule_state),
         SPropValue(PR_RULE_ACTIONS, ACTIONS(1, storeactions)),
         SPropValue(PR_RULE_PROVIDER_DATA, binascii.unhexlify('010000000000000074da402772c2e440')),
         SPropValue(PR_RULE_CONDITION, returncon),
         SPropValue(PR_RULE_SEQUENCE, lastid + 1),
         SPropValue(PR_RULE_PROVIDER, b'RuleOrganizer'),
         SPropValue(PR_RULE_NAME,  name.encode('utf-8'))
         ])]

    return rowlist

def kopano_rule(server, user, listrules=False, rule=None, state=None, emptyRules=False, rulename=None, conditions=None,
                actions=None, exceptions=None, StopProcessingRules=False, CreateFolder=False):
    user = server.user(user)
    rule_table = user.store.inbox.mapiobj.OpenProperty(PR_RULES_TABLE, IID_IExchangeModifyTable, 0, 0)
    table = rule_table.GetTable(0)
    cols = table.QueryColumns(TBL_ALL_COLUMNS)
    table.SetColumns(cols, 0)
    filters = table.QueryRows(-1, 0)

    if listrules:
        print(user.name)
        printrules(filters, user, server)
        sys.exit(0)

    if state:
        rowlist, name = changerule(filters, rule, state)

        if rowlist:
            rule_table.ModifyTable(0, rowlist)
            print("State of rule '{}' set to '{}' for user '{}'".format(name.decode('utf-8'), state, user.name))
            sys.exit(0)

    if rulename:
        try:
            lastid = filters[-1][1].Value
        except:
            lastid = 0

        rowlist = createrule(server, rulename, lastid, user.name, conditions,actions, exceptions, StopProcessingRules,
                             CreateFolder)
        if rowlist:

            rule_table.ModifyTable(0, rowlist)
            print("Rule '{}' created ".format(rulename))
            sys.exit(0)

    if emptyRules:
        for rule in filters:
            rowlist, name = changerule(filters, rule[0].Value, 'delete')
            rule_table.ModifyTable(0, rowlist)
            print("Rule '{}' is deleted for user '{}'".format(name.decode('utf-8'), user.name))


def convertRules(kopano_rule, rule_key, rule, exception=False):
    exception_text = ''
    if exception:
        exception_text = "ExceptIf"
    # kopano_rule = exchange_to_kopano['actions'][key]

    if isinstance(rule[rule_key], bool):
        return kopano_rule['kopano_name']

    elif isinstance(rule[rule_key], str):
        exchange_key = exception_text + rule_key
        return '{}:{}'.format(kopano_rule['kopano_name'], rule[exchange_key])

    elif isinstance(rule[rule_key], list):
        exchange_key = exception_text + rule_key
        if isinstance(rule[exchange_key][0], dict):
            join_list=[]
            for tmp in rule[exchange_key]:
                join_list.append(tmp[kopano_rule['dict_key']])
            return '{}:{}'.format(kopano_rule['kopano_name'], ','.join(join_list))
        else:
            return '{}:{}'.format(kopano_rule['kopano_name'], ','.join(rule[exchange_key]))

    elif isinstance(rule[rule_key], dict):
        if isinstance(rule[kopano_rule['value_key']][kopano_rule['dict_key']], list):
            return '{}:{}'.format(kopano_rule['kopano_name'],
                                  ','.join(rule[kopano_rule['value_key']][kopano_rule['dict_key']]))
        else:
            combined_list = [d[kopano_rule['dict_key']] for d in rule[kopano_rule['value_key']]]
            return '{}:{}'.format(kopano_rule['kopano_name'], ','.join(combined_list))

    return None

def exchange_rules():
    exchange_to_kopano = {
       "conditions": {
            "SubjectOrBodyContainsWords": {
                "kopano_name": "contain-word-in-body",
            },
            "SubjectContainsWords": {
               "kopano_name": "contain-word-in-subject",
            },
            "FromAddressContainsWords":{
                "kopano_name": "contain-word-sender-address",
            },
            "RecipientAddressContainsWords": {
                "kopano_name": "contain-word-recipient-address",
            },
            "From": {
                "kopano_name": "received-from",
                "dict_key": "Address",
            },
            "SentTo": {
                "kopano_name": "sent-to",
                "dict_key": "Address",
            },
            "MyNameInToOrCcBox": {
                "kopano_name": "name-in-to-cc",
            },
            "MyNameInCcBox": {
                "kopano_name": "name-in-cc",
            },
            "MyNameInToBox": {
                "kopano_name": "name-in-to",
            },
            "WithImportance": {
                "kopano_name": "importance",
            },
            "WithSensitivity": {
                "kopano_name": "sensitivity",
            },
            "SentOnlyToMe": {
                "kopano_name": "sent-only-to-me",
            },
            "HeaderContainsWords": {
                "kopano_name": "contain-word-in-header",
            },
            "MessageTypeMatches": {
               "kopano_name": "meeting_request",
            },
            "HasAttachment": {
                "kopano_name": "has-attachment",
            },
        },
        "actions": {
            "DeleteMessage": {
                "kopano_name": "delete",
            },
            "ForwardAsAttachmentTo": {
                "kopano_name": "forward-as-attachment"
            },
            "ForwardTo": {
                 "kopano_name": "forward-to",
            },
            "RedirectTo": {
                "kopano_name": "redirect-to",
                "value_key": "RedirectTo",
            },
            "MoveToFolder": {
                "kopano_name": "move-to",
                "value_key": "MoveToFolder",
                "dict_key": "FolderPath",
            },
            "CopyToFolder": {
                "kopano_name": "copy-to",
                "value_key": "CopyToFolder",
            },
        }
    }


    try:
        rules = json.loads(open(options.importFile).read())
    except json.errors.JSONDecodeError as e:
        print('Not a valid json file error: "{}"'.format(e))
        sys.exit(1)
    except IOError:
        print('File not found')
        sys.exit(1)
    if isinstance(rules, dict):
        rules = [rules]

    for rule in rules:
        # Remove all the empty keys from the json
        rule = {k: v for k, v in rule.items() if v is not None}
        if options.verbose:
            print(json.dumps(rule, indent=4))

        user = rule['MailboxOwnerId']
        if isinstance(user, dict):
            options.user = user['Name']
        else:
            options.user = user.split('/')[-1]

        options.createrule = rule['Name']
        options.StopProcessingRules = False
        if rule['StopProcessingRules']:
            options.StopProcessingRules = True

        actions = []
        conditions = []
        exceptions = []
        for key in rule:
            if rule[key]:
                if exchange_to_kopano['actions'].get(key):
                    tmp = convertRules(exchange_to_kopano['actions'][key], key, rule)
                    if tmp:
                        actions.append(tmp)

                if exchange_to_kopano['conditions'].get(key):
                    tmp = convertRules(exchange_to_kopano['conditions'][key], key, rule)
                    if tmp:
                        conditions.append(tmp)

                if exchange_to_kopano['conditions'].get(key.replace('ExceptIf', '')) and 'ExceptIf' in key:
                    tmp = key.replace('ExceptIf', '')
                    tmp = convertRules(exchange_to_kopano['conditions'][tmp], tmp, rule, True)
                    if tmp:
                        exceptions.append(tmp)

        options.actions = actions
        options.conditions = conditions
        options.exceptions = exceptions
        if not options.actions or (not options.conditions and not options.exceptions):

            print('Rule "{}" does not have valid actions or conditions/exceptions'.format(rule['Name']))
            if options.verbose:
                print(rule['Description'])
                print(json.dumps(rule, indent=4))

            continue
        kopano_rule()

def main():
    global server
    global options
    global LDAP
    options, args = opt_args()
    if options.configFile:
        if not os.path.isfile(options.configFile):
            print('Config file not found')
            sys.exit(1)
        import importlib

        LDAP = importlib.import_module(options.configFile.split('.')[0], package=None)
        if 'ldap3' not in sys.modules:
            print('please install ldap3 (pip3 install ldap3)')
            sys.exit(1)

    if not options.user and not options.importFile:
        print('please use: {} --user <username> or \n{} --import-exchange-rules <json file>'.format(sys.argv[0],sys.argv[0]))
        sys.exit(1)

    server = kopano.server(options=options)
    if options.user:
        kopano_rule(server=server, user=options.user, listrules=options.listrules, rule=options.rule,
                    state=options.state, emptyRules=options.emptyRules, rulename=options.createrule,
                    conditions=options.conditions, actions=options.actions, exceptions=options.exceptions,
                    StopProcessingRules=options.StopProcessingRules, CreateFolder=options.CreateFolder)

    if options.importFile:
        exchange_rules()

if __name__ == "__main__":
    main()
