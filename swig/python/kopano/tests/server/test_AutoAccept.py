def test_enabled(user):
    user.autoaccept.enabled = True
    assert user.autoaccept.enabled

    user.autoaccept.enabled = False
    assert not user.autoaccept.enabled


def test_conflicts(user):
    user.autoaccept.conflicts = False
    assert not user.autoaccept.conflicts

    user.autoaccept.conflicts = True
    assert user.autoaccept.conflicts


def test_recurring(user):
    user.autoaccept.recurring = False
    assert not user.autoaccept.recurring

    user.autoaccept.recurring = True
    assert user.autoaccept.recurring
