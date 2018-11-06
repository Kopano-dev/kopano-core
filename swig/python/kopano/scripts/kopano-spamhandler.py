#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# call (learning) "spam command" for items in junk folders, optionally deleting them

# usage: ./kopano-spamhandler.py (change kopano-spamhandler.cfg)

# XXX use python-kopano Config class

try:
    from ConfigParser import ConfigParser
except ImportError:
    from configparser import ConfigParser
import sys
import subprocess
import datetime
import kopano
delcounter = 0

def main():
    global delcounter
    learncounter = 0
    (users, allusers, remoteusers, autolearn, autodelete, deleteafter, spamcommand) = getconfig()
    z = kopano.Server()
    if allusers and not users:
        users = []
        for user in z.users(remote=remoteusers):
            users.append(user.name)
    for username in users:
        try:
            user = z.user(username)
            for item in user.store.junk.items():
                if autolearn:
                    if (not item.header('x-spam-flag')) or ( item.header('x-spam-flag') == 'NO'):
                        print("%s : untagged spam [Subject: %s]" % (user.name, item.subject))
                        try:
                            p = subprocess.Popen(spamcommand, shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
                            learn, output_err = p.communicate(item.eml())
                        except:
                            print('failed to run [%s] [%s]' % (spamcommand, output_err))
                        if learn:
                            print("%s : learned [%s]" % (user.name, learn.rstrip('\n')))
                            delmsg = 'delete after learn'
                            deletejunk(user, item, delmsg)
                            learncounter += 1
                        continue
                if autodelete:
                    if item.received.date() < (datetime.date.today()-datetime.timedelta(days=deleteafter)):
                        delmsg = 'autodelete'
                        deletejunk(user, item, delmsg)
        except Exception as error:
            print("%s : Unable to open store/item : [%s] [%s]" % (username, username, error))
            continue
    print("Summary learned %d items, deleted %d items" % (learncounter, delcounter))


def deletejunk(user, item, delmsg):
    global delcounter
    try:
        user.store.junk.delete([item])
        print("%s : %s [Subject: %s]" % (user.name, delmsg, item.subject))
        delcounter += 1
    except Exception as error:
        print("%s : Unable to %s item [Subject: %s] [%s]" % (user.name, delmsg, item.subject, error))
        pass
    return


def getconfig():
    Config = ConfigParser()
    try:
        Config.read('kopano-spamhandler.cfg')
        users = Config.get('users', 'users')
        remoteusers = Config.getboolean('users', 'remoteusers')
        autolearn = Config.getboolean('learning', 'autolearn')
        autodelete = Config.getboolean('deleting', 'autodelete')
        deleteafter = Config.getint('deleting', 'deleteafter')
        spamcommand = Config.get('spamcommand', 'command')
        if not users:
            allusers = True
        else:
            allusers = False
            users = users.replace(" ", "").split(",")
        return (users, allusers, remoteusers, autolearn, autodelete, deleteafter, spamcommand)
    except:
        exit('Configuration error, please check kopano-spamhandler.cfg')


if __name__ == '__main__':
    main()
