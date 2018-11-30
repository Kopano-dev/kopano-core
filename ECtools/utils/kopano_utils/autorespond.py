#!@PYTHON@
# SPDX-License-Identifier: AGPL-3.0-or-later

from __future__ import print_function

import kopano
from kopano.log import logger
import sys
import os
import time
from contextlib import closing

if sys.hexversion >= 0x03000000:
    import bsddb3 as bsddb
else:
    import bsddb

CONFIG = {
    "autorespond_cc": kopano.Config.boolean(default=False),
    "autorespond_bcc": kopano.Config.boolean(default=False),
    "autorespond_norecip": kopano.Config.boolean(default=False),
    "timelimit": kopano.Config.integer(default=24 * 3600),
    "senddb": kopano.Config.string(default="/var/lib/kopano/autorespond.db"),
    "copy_to_sentmail": kopano.Config.boolean(default=True)
}

user_blacklist = ["mailer-daemon", "postmaster", "root"]

def in_blacklist(log, from_, to):
    short_from = from_.split("@")[0]
    short_to = to.split("@")[0]

    if short_from in user_blacklist or short_to in user_blacklist:
        log.info("From or To is blacklisted (from: %s, to: %s)", from_, to)
        return True

    return False

def send_ooo(server, username, msg, copy_to_sentmail):
    outbox = server.user(username).outbox
    item = outbox.create_item(eml=msg)
    item.send(copy_to_sentmail=copy_to_sentmail)

def check_time(senddb, timelimit, username, to):
    with closing(bsddb.btopen(senddb, 'c')) as db:
        key = (username + ":" + to).encode('utf8')
        if key in db:
            timestamp = int(db[key])
            if timestamp + timelimit > int(time.time()):
                return True
    return False

def add_time(senddb, username, to):
    with closing(bsddb.btopen(senddb, 'c')) as db:
        key = (username + ":" + to).encode('utf8')
        db[key] = str(int(time.time()))

def main():
    parser = kopano.parser("cskp", usage="Usage: %prog [options] from to subject username msgfile")
    options, args = parser.parse_args()

    if len(args) != 5:
        print("Invalid arguments, you have to supply the arguments: from, to, subject, username, and msgfile", file=sys.stderr)
        sys.exit(1)

    config_dict = kopano.CONFIG
    config_dict.update(CONFIG)
    config = kopano.Config(config_dict, options=options, service="autorespond")
    log = logger("autorespond", options=options, config=config)
    server = kopano.Server(options=options, config=config, parse_args=False)

    (from_, to, subject, username, msg_file) = args
    (to_me, bcc_me, cc_me) = (
        os.getenv("MESSAGE_TO_ME"), os.getenv("MESSAGE_BCC_ME"),
        os.getenv("MESSAGE_CC_ME"),
    )

    try:
        fh = open(msg_file, "rb")
        msg = fh.read()
        fh.close()
    except:
        log.info("Could not open msg file: %s" % msg_file)
        sys.exit(1)

    if not(config["autorespond_norecip"] or
           (config["autorespond_bcc"] and bcc_me) or
           (config["autorespond_cc"] and cc_me) or to_me):
        log.debug("Response not send due to configuration")
    elif not(from_ and to and username and msg and msg_file):
        log.info(
            "One of the input arguments was empty (from: %s, to: %s, username: %s, msg: %s)",
            from_, to, username, msg
        )
    elif from_ == to:
        log.info("Loop detected, from == to (%s)", from_)
    elif in_blacklist(log, from_, to):
        log.info("From or to is in blacklist (from: %s, to: %s)", from_, to)
    elif check_time(config["senddb"], config["timelimit"], username, to):
        log.info(
            "Ignoring since we already sent OOO message within timelimit (username: %s, to: %s, timelimit: %d)",
            username, to, config["timelimit"]
        )
    else:
        add_time(config["senddb"], username, to)
        send_ooo(server, username, msg, config["copy_to_sentmail"])
        log.info("Sent OOO to %s (username %s)", to, username)

if __name__ == '__main__':
    main()
