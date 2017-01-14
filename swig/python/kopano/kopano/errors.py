"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

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
