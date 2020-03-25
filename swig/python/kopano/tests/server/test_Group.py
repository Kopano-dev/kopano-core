def test_name(group):
    assert group.name == 'Everyone'


def test_setters(group):
    pass  # requires db plugin


def test_groupid(group):
    assert group.groupid


def test_hidden(group):
    assert not group.hidden


def test_cmp(user, group):
    assert group == group
    assert user in group


def test_str(group):
    assert str(group) == "Group('Everyone')"
