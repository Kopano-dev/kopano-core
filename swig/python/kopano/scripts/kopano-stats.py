#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# replacement for regular kopano-stats (practically complete)

# usage: ./kopano-stats --top

import curses
import sys
import time
import kopano
from MAPI.Util import *

TABLES = {
    'company': (PR_EC_STATSTABLE_COMPANY, PR_EC_COMPANY_NAME),
    'session': (PR_EC_STATSTABLE_SESSIONS, (PR_EC_STATS_SESSION_IPADDRESS, -PR_EC_STATS_SESSION_IDLETIME)),
    'servers': (PR_EC_STATSTABLE_SERVERS, PR_EC_STATS_SERVER_NAME),
    'system': (PR_EC_STATSTABLE_SYSTEM, PR_NULL),
    'users': (PR_EC_STATSTABLE_USERS, (PR_EC_COMPANY_NAME, PR_EC_USERNAME_A)),
}

def top(scr, server):
    def delta_dict(d1, d2):
        delta = {}
        for key, value in d2.iteritems():
            try:
                delta[key] = float(value) - float(d1.get(key, 0))
            except:
                pass
        return delta

    def get_system_stats():
        return server.table(PR_EC_STATSTABLE_SYSTEM).dict_(PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_VALUE)

    def get_session_stats():
        return server.table(PR_EC_STATSTABLE_SESSIONS).index(PR_EC_STATS_SESSION_ID)

    def add_system_stats():
        hosts = len(set([row[PR_EC_STATS_SESSION_IPADDRESS] for row in session_stats.values()]))
        users = len(set([row[PR_EC_USERNAME_A] for row in session_stats.values()]))
        dblUser = dblSystem = 0
        for row in session_stats.values():
            delta = delta_dict(sessions_stats_old.get(row[PR_EC_STATS_SESSION_ID], {}), row)
            dblUser += delta[PR_EC_STATS_SESSION_CPU_USER]
            dblSystem += delta[PR_EC_STATS_SESSION_CPU_SYSTEM]
        delta = delta_dict(system_stats_old, system_stats)
        line = 'Last update: %s (%1.1fs since last time)' % (
            time.strftime('%a %b %d %H:%M:%S %Y'),
            dt,
        )
        scr.addstr(0, 0, line)
        line = 'Sess: %s  Sess grp: %s  Users: %s  Hosts: %s  CPU: %s%% Len: %s QAge: %s RTT: %1.0f ms ' % (
            system_stats['sessions'],
            system_stats['sessiongroups'],
            users,
            hosts,
            int((dblUser + dblSystem) * 100),
            system_stats['queuelen'],
            system_stats['queueage'],
            delta['response_time'] / delta['soap_request'],
        )
        scr.addstr(1, 0, line)
        line = 'SQL/s SEL: %1.0f  UPD: %1.0f  INS: %1.0f  DEL: %1.0f  Threads(idle): %s(%s) MWOPS: %1.0f MROPS: %1.0f SOAP calls: %1.0f' % (
            delta['sql_select']/dt,
            delta['sql_update']/dt,
            delta['sql_insert']/dt,
            delta['sql_delete']/dt,
            system_stats['threads'],
            system_stats['threads_idle'],
            delta['mwops']/dt,
            delta['mrops']/dt,
            delta['soap_request']/dt,
        )
        scr.addstr(2, 0, line)

    def add_session_stats(showcols, sortcol, reverse, groupmap):
        fmt = ['%%-%ds' % w for w in [3, 20, 15, 10, 10, 15, 10, 10, 10, 10, 10, 10]]
        data = []
        for row in session_stats.values():
            delta = delta_dict(sessions_stats_old.get(row[PR_EC_STATS_SESSION_ID], {}), row)
            data.append((
                groupmap.setdefault(row[PR_EC_STATS_SESSION_GROUP_ID], len(groupmap)+1),
                2**64 - row[PR_EC_STATS_SESSION_ID] if row[PR_EC_STATS_SESSION_ID] < 0 else row[PR_EC_STATS_SESSION_ID], #XXX
                row[PR_EC_STATS_SESSION_CLIENT_VERSION],
                row[PR_EC_USERNAME_A],
                row[PR_EC_STATS_SESSION_PEER_PID] or row[PR_EC_STATS_SESSION_IPADDRESS],
                row[PR_EC_STATS_SESSION_CLIENT_APPLICATION],
                ('%d:%02d' % divmod(int(row[PR_EC_STATS_SESSION_CPU_REAL]), 60)),
                ('%d:%02d' % divmod(int(row[PR_EC_STATS_SESSION_CPU_USER] + row[PR_EC_STATS_SESSION_CPU_SYSTEM]), 60)),
                int((delta[PR_EC_STATS_SESSION_CPU_USER] + delta[PR_EC_STATS_SESSION_CPU_SYSTEM]) / dt * 100),
                int(delta[PR_EC_STATS_SESSION_REQUESTS]),
                next(iter(row[PR_EC_STATS_SESSION_PROCSTATES]), ''),
                next(iter(row[PR_EC_STATS_SESSION_BUSYSTATES]), ''),
            ))
        data.sort(key=lambda d: d[sortcol], reverse=reverse)
        data.insert(0, ['GRP', 'SESSIONID', 'VERSION', 'USERID', 'IP/PID', 'APP', 'TIME', 'CPUTIME', 'CPU', 'NREQ', 'STAT', 'TASK'])
        for cnt, d in zip(range(4, maxY), data):
            line = ' '.join([fmt[i] % x for i, x in enumerate(d) if showcols[i]])
            scr.addstr(cnt, 0, line, curses.A_BOLD if cnt > 4 and d[-4] else curses.A_NORMAL) # -4: CPU

    showcols = 2*[False]+10*[True]
    sortcol, lastsort, reverse = 1, None, False
    lastsort = None
    groupmap = {}
    scr.nodelay(1)
    curses.noecho()
    curses.nonl()
    curses.curs_set(0)
    t = time.time()
    system_stats, session_stats = get_system_stats(), get_session_stats()
    while True:
        c = scr.getch()
        if c == ord('q'):
            break
        elif ord('1') <= c <= ord('9'): # XXX 0?
            showcols[c-ord('1')] = not showcols[c-ord('1')]
        elif ord('a') <= c <= ord('f'):
            sortcol = c-ord('a')
            reverse = not reverse if c == lastsort else False
            lastsort = c
        maxY, maxX = scr.getmaxyx()
        scr.clear()
        t, dt = time.time(), time.time()-t
        session_stats, sessions_stats_old = get_session_stats(), session_stats
        system_stats, system_stats_old = get_system_stats(), system_stats
        add_system_stats()
        add_session_stats(showcols, sortcol, reverse, groupmap)
        scr.refresh()
        time.sleep(1)

def opt_args():
    parser = kopano.parser('skpc')
    parser.add_option('--system', dest='system', action='store_true',  help='Gives information about threads, SQL and caches')
    parser.add_option('--users', dest='users', action='store_true', help='Gives information about users, store sizes and quotas')
    parser.add_option('--company', dest='company', action='store_true', help='Gives information about companies, company sizes and quotas')
    parser.add_option('--servers', dest='servers', action='store_true', help='Gives information about cluster nodes')
    parser.add_option('--session', dest='session', action='store_true', help='Gives information about sessions and server time spent in SOAP calls')
    parser.add_option('--top', dest='top', action='store_true', help='Shows top-like information about sessions')
    parser.add_option('-d','--dump', dest='dump', action='store_true', help='print output as csv')
    return parser.parse_args()

def main():
    options, args = opt_args()
    if options.top:
        try:
            curses.wrapper(top, kopano.Server(options))
        except KeyboardInterrupt:
            sys.exit(-1)
    else:
        for option, (table, sort) in TABLES.items():
            if getattr(options, option):
                table = kopano.Server(options).table(table)
                table.sort(sort)
                print(table.csv(delimiter=';') if options.dump else table.text())

if __name__ == '__main__':
    main()
