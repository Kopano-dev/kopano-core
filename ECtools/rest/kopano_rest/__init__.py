# SPDX-License-Identifier: AGPL-3.0-or-later
from .version import __version__

import falcon
import kopano

kopano.set_bin_encoding('base64')
kopano.set_missing_none()
# TODO set_timezone_aware?

from .api_v1.rest import RestAPI
from .api_v1.notify import NotifyAPI
