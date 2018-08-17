# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

from datetime import datetime
import optparse
import sys

from .compat import decode as _decode

def parse_date(option, opt_str, value, parser):
    setattr(parser.values, option.dest, datetime.strptime(value, '%Y-%m-%d'))

def parse_loglevel(option, opt_str, value, parser):
    setattr(parser.values, option.dest, value.upper())

def parse_str(option, opt_str, value, parser):
    setattr(parser.values, option.dest, _decode(value))

def parse_list_str(option, opt_str, value, parser):
    getattr(parser.values, option.dest).append(_decode(value))

def parse_bool(option, opt_str, value, parser):
    if value not in ('yes', 'no'):
        raise Exception("error: %s option requires 'yes' or 'no'" % opt_str)
    setattr(parser.values, option.dest, value == 'yes')

def show_version(*args):
    import __main__
    print(__main__.__version__)
    sys.exit()

def _callback(**kwargs):
    d = {
        'type': 'str',
        'action': 'callback',
        'callback': parse_str
    }
    d.update(kwargs)
    return d

def _true():
    return {'action': 'store_true'}

def _int():
    return {'type': int, 'metavar': 'N'}

def _guid():
    return _callback(metavar='GUID')

def _name():
    return _callback(metavar='NAME')

def _path():
    return _callback(metavar='PATH')

def _bool():
    return _callback(callback=parse_bool, metavar='YESNO')

def _date():
    return _callback(callback=parse_date, metavar='DATE')

def _list_name():
    return _callback(callback=parse_list_str, default=[], metavar='NAME')

def parser(options='cskpUPufmvCGSlbe', usage=None):
    """
Return OptionParser instance from the standard ``optparse`` module,
containing common kopano command-line options

:param options: string containing a char for each desired option

Available options:

-c, --config: Path to configuration file

-s, --server-socket: Storage server socket address

-k, --sslkey-file: SSL key file

-p, --sslkey-password: SSL key password

-U, --auth-user: Login as user

-P, --auth-pass: Login with password

-C, --company: Run program for specific company

-u, --user: Run program for specific user

-S, --store: Run program for specific store

-f, --folder: Run program for specific folder

-b, --period-begin: Run program for specific period

-e, --period-end: Run program for specific period

-F, --foreground: Run service in foreground

-m, --modify: Enable modification (python-kopano does not check this!)

-l, --log-level: Set log level (debug, info, warning, error, critical)

-I, --input-dir: Specify input directory

-O, --output-dir: Specify output directory

-v, --verbose: Enable verbose output (python-kopano does not check this!)

-V, --version: Show program version
"""

    parser = optparse.OptionParser(
        formatter=optparse.IndentedHelpFormatter(max_help_position=42),
        usage=usage
    )

    kw_str = _callback()
    kw_date = _callback(callback=parse_date)
    kw_list_str = _callback(callback=parse_list_str)

    if 'c' in options:
        parser.add_option(
            '-c', '--config', dest='config_file',
            help='load settings from FILE', metavar='FILE', **kw_str
        )

    if 's' in options:
        parser.add_option(
            '-s', '--server-socket', dest='server_socket',
            help='connect to server SOCKET', metavar='SOCKET', **kw_str
        )

    if 'k' in options:
        parser.add_option(
            '-k', '--ssl-key', dest='sslkey_file',
            help='SSL key file', metavar='FILE', **kw_str
        )

    if 'p' in options:
        parser.add_option(
            '-p', '--ssl-pass', dest='sslkey_pass',
            help='SSL key password', metavar='PASS', **kw_str
        )

    if 'U' in options:
        parser.add_option(
            '-U', '--auth-user', dest='auth_user',
            help='login as user', metavar='NAME', **kw_str
        )

    if 'P' in options:
        parser.add_option(
            '-P', '--auth-pass', dest='auth_pass',
            help='login with password', metavar='PASS', **kw_str
        )

    if 'G' in options:
        parser.add_option(
            '-G', '--group', dest='groups', default=[],
            help='Specify group', metavar='NAME', **kw_list_str
        )

    if 'C' in options:
        parser.add_option(
            '-C', '--company', dest='companies', default=[],
            help='Specify company', metavar='NAME', **kw_list_str
        )

    if 'u' in options:
        parser.add_option(
            '-u', '--user', dest='users', default=[],
            help='Specify user', metavar='NAME', **kw_list_str
        )

    if 'S' in options:
        parser.add_option(
            '-S', '--store', dest='stores', default=[],
            help='Specify store', metavar='GUID', **kw_list_str
        )

    if 'f' in options:
        parser.add_option(
            '-f', '--folder', dest='folders', default=[],
            help='Specify folder', metavar='NAME', **kw_list_str
        )

    if 'b' in options:
        parser.add_option(
            '-b', '--period-begin',
            dest='period_begin', help='Specify period', metavar='DATE',
            **kw_date
        )

    if 'e' in options:
        parser.add_option(
            '-e', '--period-end', dest='period_end',
            help='Specify period', metavar='DATE', **kw_date
        )

    if 'F' in options:
        parser.add_option(
            '-F', '--foreground', dest='foreground',
            action='store_true', help='run program in foreground'
        )

    if 'm' in options:
        parser.add_option(
            '-m', '--modify', dest='modify',
            action='store_true', help='enable database modification'
        )

    if 'l' in options:
        parser.add_option(
            '-l', '--log-level', dest='loglevel',
            action='callback', type='str', callback=parse_loglevel,
            help='set log level (CRITICAL, ERROR, WARNING, INFO, DEBUG)',
            metavar='LEVEL'
        )

    if 'v' in options:
        parser.add_option(
            '-v', '--verbose', dest='verbose',
            action='store_true', help='enable verbose output'
        )

    if 'V' in options:
        parser.add_option(
            '-V', '--version', action='callback',
            help='show program version', callback=show_version
        )

    if 'w' in options:
        parser.add_option(
            '-w', '--worker-processes', dest='worker_processes',
            help='number of parallel worker processes', type='int',
            metavar='N'
        )

    if 'I' in options:
        parser.add_option(
            '-I', '--input-dir', dest='input_dir',
            help='specify input directory', metavar='PATH', **kw_str
        )

    if 'O' in options:
        parser.add_option(
            '-O', '--output-dir', dest='output_dir',
            help='specify output directory', metavar='PATH', **kw_str
        )

    return parser
