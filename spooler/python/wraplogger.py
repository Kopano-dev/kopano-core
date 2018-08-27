# SPDX-License-Identifier: AGPL-3.0-only
from MAPI import ec_log

class WrapLogger(object):
    def log(self, lvl, msg):
        ec_log(lvl, msg)

    def logDebug(self, msg):
        ec_log(6, msg)

    def logInfo(self, msg):
        ec_log(5, msg)
 
    def logNotice(self, msg):
        ec_log(4, msg)
    
    def logWarn(self, msg):
        ec_log(3, msg)

    def logError(self, msg):
        ec_log(2, msg)

    def logFatal(self, msg):
        ec_log(1, msg)
