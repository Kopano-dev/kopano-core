# LDAP backend does not allow modification of quota


def test_warning_limit(quota):
    assert quota.warning_limit == 0


def test_hard_limit(quota):
    assert isinstance(quota.hard_limit, int)


def test_soft_limit(quota):
    assert quota.soft_limit == 0
