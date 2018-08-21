# SPDX-License-Identifier: AGPL-3.0-only

MP_CONTINUE         = 0
MP_FAILED           = 1
MP_STOP_SUCCESS     = 2
MP_STOP_FAILED      = 3
MP_EXIT             = 4
MP_RETRY_LATER      = 5


class IMapiDAgentPlugin(object):
    prioPostConverting = 9999
    prioPreDelivery = 9999
    prioPostDelivery = 9999
    prioPreRuleProcess = 9999
    prioSendNewMailNotify = 9999

    def __init__(self, logger):
        self.logger = logger

    def PostConverting(self, session, addrbook, store, folder, message):
        raise NotImplementedError('PostConverting not implemented, function ignored')

    def PreDelivery(self, session, addrbook, store, folder, message):
        raise NotImplementedError('PreDelivery not implemented, function ignored')

    def PostDelivery(self, session, addrbook, store, folder, message):
        raise NotImplementedError('PostDelivery not implemented, function ignored')

    def PreRuleProcess(self, session, addrbook, store, rulestable):
         raise NotImplementedError('PreRuleProcess not implemented, function ignored')

    def SendNewMailNotify(self, session, addrbook, store, folder, message):
         raise NotImplementedError('SendNewMailNotify not implemented, function ignored')


class IMapiSpoolerPlugin(object):
    prioPreSending = 9999

    def __init__(self, logger):
        self.logger = logger

    def PreSending(self, session, addrbook, store, folder, message):
        raise NotImplementedError('PreSending not implemented, function ignored')
