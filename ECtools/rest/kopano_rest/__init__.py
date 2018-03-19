from .version import __version__

import falcon
import kopano

kopano.set_bin_encoding('base64')
kopano.set_missing_none()
# TODO set_timezone_aware?

from . import rest
from . import notify

RestAPI = rest.RestAPI
NotifyAPI = notify.NotifyAPI
