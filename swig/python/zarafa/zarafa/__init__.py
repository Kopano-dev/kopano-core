# SPDX-License-Identifier: AGPL-3.0-or-later
import sys

import kopano

kopano.ZarafaException = kopano.Error
kopano.ZarafaConfigException = kopano.ConfigError
kopano.ZarafaNotFoundException = kopano.NotFoundError
kopano.ZarafaLogonException = kopano.LogonError
kopano.ZarafaNotSupported = kopano.NotSupportedError
kopano.ZarafaNotSupportedException = kopano.NotSupportedError

sys.modules[__name__] = kopano
