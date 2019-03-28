# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import warnings

class Error(Exception):
    pass

class ConfigError(Error):
    pass

class DuplicateError(Error):
    pass

class NotFoundError(Error):
    pass

class LogonError(Error):
    pass

class NotSupportedError(Error):
    pass

class ArgumentError(Error):
    pass

class _DeprecationWarning(Warning):
    pass

warnings.simplefilter('default', _DeprecationWarning)
