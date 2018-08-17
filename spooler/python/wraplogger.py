# SPDX-License-Identifier: AGPL-3.0-only

class WrapLogger(object):

    def __init__(self, logger):
        self.logger = logger

    def log(self, lvl, msg):
        if self.logger is None:
            return
        self.logger.log(lvl, msg)

    def logDebug(self, msg):
        self.log(6, msg)

    def logInfo(self, msg):
        self.log(5, msg)
 
    def logNotice(self, msg):
        self.log(4, msg)
    
    def logWarn(self, msg):
        self.log(3, msg)

    def logError(self, msg):
        self.log(2, msg)

    def logFatal(self, msg):
        self.log(1, msg)
