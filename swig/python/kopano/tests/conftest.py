import os

import kopano
import pytest


KOPANO_PICTURE_NAME = 'kopano-meet-icon.png'


@pytest.fixture(scope="module")
def picture():
    dir_path = os.path.dirname(os.path.realpath(__file__))
    data = open('{}/attachments/kopano-meet-icon.png'.format(dir_path), 'rb').read()
    yield kopano.Picture(data, KOPANO_PICTURE_NAME)
