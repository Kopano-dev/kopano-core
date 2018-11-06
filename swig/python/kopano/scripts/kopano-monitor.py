#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later

# example of using Service class, in the form of an (incomplete) python version of kopano-monitor

# usage: ./kopano-monitor.py

import time
from datetime import date, timedelta, datetime

import kopano
from kopano import log_exc, Config
from kopano.utils import bytes_to_human

import MAPI
from MAPI.Util import *

CONFIG = {
    'quota_check_interval': Config.integer(default=15),
    'mailquota_resend_interval': Config.integer(default=1),
    'servers': Config.string(default=''), # XXX
    'userquota_warning_template': Config.path(default='/etc/kopano/quotamail/userwarning.mail'),
    'userquota_soft_template': Config.path(default='/etc/kopano/quotamail/usersoft.mail'),
    'userquota_hard_template': Config.path(default='/etc/kopano/quotamail/userhard.mail'),
    'companyquota_warning_template': Config.path(default='/etc/kopano/quotamail/companywarning.mail'),
}

""""
TODO:
- set sender
- Company quota
- Send email to system administrator? => who is this?

"""

class Monitor(kopano.Service):
    def replace_template(self, mail, user):
        header = "X-Priority: 1\nTO: {} <{}>\nFROM:\n".format(user.fullname,user.email)
        mail = header + mail
        mail = mail.replace('${KOPANO_QUOTA_NAME}', user.name)
        mail = mail.replace('${KOPANO_QUOTA_STORE_SIZE}', bytes_to_human(user.store.size))
        mail = mail.replace('${KOPANO_QUOTA_WARN_SIZE}', bytes_to_human(user.quota.warn_limit))
        mail = mail.replace('${KOPANO_QUOTA_SOFT_SIZE}', bytes_to_human(user.quota.soft_limit))
        mail = mail.replace('${KOPANO_QUOTA_HARD_SIZE}', bytes_to_human(user.quota.hard_limit))
        return mail

    def check_quota_interval(self, user):
        interval = self.config['mailquota_resend_interval']
        config_item = user.store.config_item('Kopano.Quota')
        try:
            mail_time = config_item.prop(PR_EC_QUOTA_MAIL_TIME)
        except kopano.NotFoundError: # XXX use some kind of defaulting, so try/catch is not necessary
            mail_time = None # set default?
        if not mail_time or datetime.now() - mail_time.pyval > timedelta(days=1):
            mail_time.value = datetime.now()
            return True
        else:
            return False

    def main(self):
        server, config, log = self.server, self.config, self.log
        while True:
            with kopano.log_exc(log):
                log.info('start check')
                for user in server.users():
                    for limit in ('hard', 'soft', 'warning'): # XXX add quota_status to API?
                        if 0 < getattr(user.quota, limit+'_limit') < user.store.size:
                            log.warning('Mailbox of user %s has exceeded its %s limit' % (user.name, limit))
                            if self.check_quota_interval(user):
                                mail = open(self.config['userquota_warning_template']).readlines()
                                mail = ''.join(mail)
                                mail = self.replace_template(mail, user)
                                for r in user.quota.recipients:
                                    r.store.inbox.create_item(eml=mail)
                log.info('check done')
            time.sleep(config['quota_check_interval']*60)

if __name__ == '__main__':
    Monitor('monitor', config=CONFIG).start()
