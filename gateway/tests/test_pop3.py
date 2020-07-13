import poplib

import pytest


def test_cmd_user(pop3):
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd('USER')
    assert 'command must have 1 argument' in str(excinfo)


def test_cmd_login_failed(pop3):
    pop3.user('notexistinguser')

    with pytest.raises(poplib.error_proto) as excinfo:
        pop3.pass_('notexistinguser')
    assert 'Wrong username or password' in str(excinfo)


@pytest.mark.parametrize('command', ['STAT', 'LIST', 'RETR arg1', 'DELE arg1', 'NOOP', 'RSET', 'TOP arg1 arg2', 'UIDL'])
def test_cmd_nologin(pop3, command):
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd(command)
    assert 'Invalid command' in str(excinfo)


@pytest.mark.parametrize('arg', ['-1', '123456789', '-1 -100', '-1 9999'])
def test_cmd_list_errors(pop3, pop3login, arg):
    command = 'LIST {}'.format(arg)
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd(command)
    assert '-ERR' in str(excinfo)


@pytest.mark.parametrize('arg', ['-1', '123456789', '-1 -100', '-1 9999'])
def test_cmd_retr_error(pop3, pop3login, arg):
    command = 'RETR {}'.format(arg)
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd(command)
    assert '-ERR' in str(excinfo)


@pytest.mark.parametrize('arg', ['-1', '123456789', '*', '9999', '9999 1234'])
def test_cmd_dele_error(pop3, pop3login, arg):
    command = 'DELE {}'.format(arg)
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd(command)
    assert '-ERR' in str(excinfo)


@pytest.mark.parametrize('arg', ['arg1 arg2', '1,2,3'])
def test_cmd_noop_error(pop3, pop3login, arg):
    command = 'NOOP {}'.format(arg)
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd(command)
    assert 'NOOP must have 0 arguments' in str(excinfo)


@pytest.mark.parametrize('arg', ['1234', 'SSSS', '1234 1234'])
def test_cmd_rset_error(pop3, pop3login, arg):
    command = 'RSET {}'.format(arg)
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd(command)
    assert 'RSET must have 0 arguments' in str(excinfo)


@pytest.mark.parametrize('arg', ['1', '1 2 5', '-1 -2'])
def test_cmd_top_error(pop3, pop3login, arg):
    command = 'TOP {}'.format(arg)
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd(command)
    assert '-ERR' in str(excinfo)


@pytest.mark.parametrize('arg', ['-1', '-1 -2', '9999999'])
def test_cmd_uidl_error(pop3, pop3login, arg):
    command = 'UIDL {}'.format(arg)
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd(command)
    assert '-ERR' in str(excinfo)


@pytest.mark.parametrize('arg', ['123', '123 1234', '9999999'])
def test_cmd_stat_error(pop3, pop3login, arg):
    command = 'STAT {}'.format(arg)
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd(command)
    assert '-ERR' in str(excinfo)


def test_cmd_unknown(pop3, pop3login):
    with pytest.raises(poplib.error_proto) as excinfo:
        pop3._shortcmd('FAIL')
    assert '-ERR Function not (yet) implemented' in str(excinfo)


def test_login_logout(pop3, pop3login):
    assert pop3.quit()[:3] == b'+OK'


def test_stat(pop3, pop3login):
    assert pop3.stat()


def test_list(pop3, pop3login, reloginpop3, ipmnote):
    pop3 = reloginpop3()
    plist = pop3.list()
    assert plist[0][:3] == b'+OK'
    one = pop3.list(1)
    assert one[4:] == plist[1][0]


def test_uidl(pop3, pop3login, reloginpop3, ipmnote):
    pop3.quit()
    pop3 = reloginpop3()
    uidl1 = pop3.uidl()
    assert uidl1[0] == b'+OK'


def test_top(pop3, pop3login, reloginpop3, ipmnote):
    pop3.quit()
    pop3 = reloginpop3()
    msgs = int(pop3.stat()[0])
    lines = pop3.top(msgs, 1)[1]
    # skip headers
    n = 0
    for item in lines:
        n += 1
        if item == b'':
            break

    # '' and 1 email line left
    assert len(lines) == n + 1


def test_retr(pop3, pop3login, reloginpop3, ipmnote):
    pop3.quit()
    pop3 = reloginpop3()
    msgs = int(pop3.stat()[0])
    result = pop3.retr(msgs)
    assert result[0][:3] == b'+OK'
    assert int(result[0].split(b' ')[1]) == result[2] - 2


def test_xDELE(pop3, pop3login, ipmnote, reloginpop3):
    pop3 = reloginpop3()

    msgs1 = int(pop3.stat()[0])
    assert pop3.dele(1)[:3] == b'+OK'
    assert pop3.rset()[:3] == b'+OK'

    pop3.quit()
    pop3 = reloginpop3()

    msgs2 = int(pop3.stat()[0])
    assert msgs1 == msgs2

    for i in range(msgs1 - 1, msgs1):
        assert pop3.dele(i+1)[:3] == b'+OK'

    pop3.quit()
    pop3 = reloginpop3()

    msgs3 = int(pop3.stat()[0])
    assert msgs1 - 1 == msgs3


def test_capa(pop3, pop3login):
    caps = {'CAPA': [], 'TOP': [], 'UIDL': [], 'RESP-CODES': [], 'AUTH-RESP-CODE': []}
    assert pop3.capa() == caps
