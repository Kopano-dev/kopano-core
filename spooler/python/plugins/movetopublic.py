# SPDX-License-Identifier: AGPL-3.0-only
import MAPI
from MAPI.Util import *
from MAPI.Time import *
from MAPI.Struct import *

from plugintemplates import *

import zconfig

class MoveToPublic(IMapiDAgentPlugin):

    prioPreDelivery = 50

    configfile = '/etc/kopano/movetopublic.cfg'

    def __init__(self, logger):
        self.rulelist = {}
        IMapiDAgentPlugin.__init__(self, logger)

        self.Init()

    def Init(self):
        config = zconfig.ZConfigParser(self.configfile,
                                     defaultoptions={}
                                     )

        # scan max for 100 settings
        for i in range(1, 100, 1):
            try:
                data = config.getdict('rule'+str(i), ['recipient', 'destination_folder'])
                self.rulelist[data['recipient'].lower()] = data['destination_folder']
            except:
                break

        self.logger.logDebug("*--- Rule list %s" % self.rulelist)

    def PreDelivery(self, session, addrbook, store, folder, message):

        props = message.GetProps([PR_RECEIVED_BY_EMAIL_ADDRESS_W], 0)
        if props[0].ulPropTag != PR_RECEIVED_BY_EMAIL_ADDRESS_W:
            self.logger.logError("!--- No received by emailaddress")
            return MP_CONTINUE,

        recipient = props[0].Value.lower()

        if recipient not in self.rulelist:
            self.logger.logInfo("*--- No rule for recipient '%s'" % recipient.encode('utf-8'))
            return MP_CONTINUE,

        publicstore = GetPublicStore(session)
        if publicstore == None:
            # check for company public
            companyname = None

            storeprops = store.GetProps([PR_MAILBOX_OWNER_ENTRYID], 0)
            if storeprops[0].ulPropTag == PR_MAILBOX_OWNER_ENTRYID:
                user = addrbook.OpenEntry(storeprops[0].Value, None, 0)
                userprops = user.GetProps([PR_EC_COMPANY_NAME_W], 0)
                if userprops[0].ulPropTag == PR_EC_COMPANY_NAME_W:
                    companyname = userprops[0].Value

                if companyname == None:
                    self.logger.logError("!--- Can not open a public store")
                    return MP_CONTINUE,

                ema = store.QueryInterface(IID_IExchangeManageStore)
                publicstoreid = ema.CreateStoreEntryID(None, companyname, MAPI_UNICODE)
                publicstore = session.OpenMsgStore(0, publicstoreid, None, MDB_WRITE)

        publicfolders = publicstore.OpenEntry(publicstore.GetProps([PR_IPM_PUBLIC_FOLDERS_ENTRYID], 0)[0].Value, None, MAPI_MODIFY)

        folderlist = self.rulelist[recipient].split('/')
        if len(folderlist) == 0:
            self.logger.logWarn("!--- No folders in the rule of recipient '%s'" % recipient.encode('utf-8'))
            return MP_CONTINUE,

        folder = publicfolders
        for foldername in folderlist:
            if len(foldername) > 0:
                folder = folder.CreateFolder(0, foldername, "Create by Move to Public plugin", None, OPEN_IF_EXISTS | MAPI_UNICODE)

        msgnew = folder.CreateMessage(None, 0)
        tags = message.GetPropList(MAPI_UNICODE)
        message.CopyProps(tags, 0, None, IID_IMessage, msgnew, 0)

        msgnew.SaveChanges(0)
        folderid = folder.GetProps([PR_ENTRYID], 0)[0].Value
        msgid =  msgnew.GetProps([PR_ENTRYID], 0)[0].Value

        publicstore.NotifyNewMail( NEWMAIL_NOTIFICATION(msgid, folderid, 0, None, 0) )

        self.logger.logInfo("*--- Message moved to public folder '%s'" % (self.rulelist[recipient].encode('utf-8')) )

        return MP_STOP_SUCCESS,
