from .version import __version__

import falcon
import kopano

kopano.set_bin_encoding('base64')
kopano.set_missing_none()
# TODO set_timezone_aware?

from .api_v0 import (
    rest, notify
)

RestAPI = rest.RestAPIV0
NotifyAPI = notify.NotifyAPIV0
