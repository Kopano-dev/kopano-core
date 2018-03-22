from .version import __version__

import falcon
import kopano

kopano.set_bin_encoding('base64')
kopano.set_missing_none()
# TODO set_timezone_aware?

from .api_v0.rest import RestAPIv0
from .api_v0.notify import NotifyAPIv0

RestAPIv1 = RestAPIv0
NotifyAPIv1 = NotifyAPIv0

# default
RestAPI = RestAPIv0
NotifyAPI = NotifyAPIv0
