from conftest import KOPANO_PICTURE_NAME


def test_name(picture):
    assert picture.name == KOPANO_PICTURE_NAME

def test_mimetype(picture):
    assert picture.mimetype == 'image/png'

def test_width(picture):
    assert picture.width == 24

def test_height(picture):
    assert picture.height == 24

def test_size(picture):
    assert picture.size == (24, 24)

def test_str(picture):
    assert str(picture) == 'Picture({})'.format(KOPANO_PICTURE_NAME)

def test_scale(picture):
    scaled = picture.scale((12, 12))
    assert scaled.size == (12, 12)
