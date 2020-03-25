from kopano import Restriction


def test_restriction(rule):
    assert isinstance(rule.restriction, Restriction)


def test_createaction(rule):
    assert list(rule.actions())


def test_actions(rule):
    action = next(rule.actions())
    assert str(action) == "Action('move')"


def test_active(rule):
    assert rule.active


def test_str(rule):
    assert str(rule) == "Rule('test')"
