import MAPI
from MAPI.Util import *
from MAPI.Time import *
from MAPI.Struct import *

from plugintemplates import *
import StringIO 
from PIL import Image


class BMP2PNG(IMapiDAgentPlugin):

    prioPostConverting = 10

    def PostConverting(self, session, addrbook, store, folder, message):
        props = message.GetProps([PR_HASATTACH], 0)

        if (props[0].Value == False):
            return MP_CONTINUE,

        self.FindBMPAttachmentsAndConvert(message)

        return MP_CONTINUE,

    def FindBMPAttachmentsAndConvert(self, message, embedded=False):
        # Get the attachments
        table = message.GetAttachmentTable(MAPI_UNICODE)
        table.SetColumns([PR_ATTACH_NUM, PR_ATTACH_LONG_FILENAME, PR_ATTACH_METHOD, PR_ATTACH_MIME_TAG], 0)
        changes = False
        while(True):
            rows = table.QueryRows(10, 0)
            if (len(rows) == 0):
                break

            for row in rows:
                if (row[0].ulPropTag != PR_ATTACH_NUM or row[2].ulPropTag != PR_ATTACH_METHOD):
                    self.logger.logDebug("!--- Attachment found but the row is incomplete, attachment number or method data is invalid. Data: %s" % (row) )
                    continue

                attnum = row[0].Value
                method = row[2].Value
                itype = ''
                if (row[3].ulPropTag == PR_ATTACH_MIME_TAG):
                    itype = row[3].Value.upper()

                if (method == ATTACH_BY_VALUE and itype.find('BMP') >=0):
                    try:
                        self.ConvertBMP2PNG(message, attnum)
                        changes = True
                    except Exception, e:
                        self.logger.logError("!--- [%d] Unable to convert BMP to PNG: %s" % (attnum, e) )
                elif (method == ATTACH_EMBEDDED_MSG):
                    self.logger.logDebug("*--- [%d] Embedded message found" % (attnum))

                    attach = message.OpenAttach(attnum, None, MAPI_MODIFY)
                    embeddedmsg = attach.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_MODIFY | MAPI_DEFERRED_ERRORS)
                    
                    if (self.FindBMPAttachmentsAndConvert(embeddedmsg, True) == True):
                        changes = True
                        attach.SaveChanges(0)
                else:
                     self.logger.logDebug("*--- [%d] Not supported attachment method %d" % (attnum, method))

        if (embedded and changes):
            message.SaveChanges(0)

        return changes
        
    def ConvertBMP2PNG(self, message, attnum):
        attname = 'Unknown'
        attach = message.OpenAttach(attnum, IID_IAttachment, 0)
        attprops = attach.GetProps([PR_ATTACH_LONG_FILENAME, PR_DISPLAY_NAME, PR_ATTACH_FILENAME], 0)
        stream = attach.OpenProperty(PR_ATTACH_DATA_BIN, IID_IStream, 0, MAPI_MODIFY)

        datain = StringIO.StringIO(stream.Read(0xFFFFFF))
        dataout = StringIO.StringIO()

        img = Image.open(datain)
        img.save(dataout, 'PNG') 
        stream.SetSize(0)
        stream.Seek(0,0)

        stream.Write(dataout.getvalue())
        stream.Commit(0)
        props = [SPropValue(PR_ATTACH_MIME_TAG, 'image/png'), SPropValue(PR_ATTACH_EXTENSION, '.png')]

        if (attprops[0].ulPropTag == PR_ATTACH_LONG_FILENAME and attprops[0].Value[-4:].upper() == '.BMP'):
            props.append(SPropValue(PR_ATTACH_LONG_FILENAME, attprops[0].Value[0:-3] + 'png'))
            attname = attprops[0].Value

        if (attprops[1].ulPropTag == PR_DISPLAY_NAME and attprops[1].Value[-4:].upper() == '.BMP' ):
             props.append(SPropValue(PR_DISPLAY_NAME, attprops[1].Value[0:-3] + 'png'))
             if (len(attname) == 0):
                 attname =  attprops[1].Value

        if (attprops[2].ulPropTag == PR_ATTACH_FILENAME and attprops[2].Value[-4:].upper() == '.BMP'):
            props.append(SPropValue(PR_ATTACH_FILENAME, attprops[2].Value[0:-3] + 'png'))
            if (len(attname) == 0):
                attname =  attprops[2].Value

        attach.SetProps(props)
        attach.SaveChanges(0)


        self.logger.logDebug("*--- [%d] Attachment '%s' converted to png" % (attnum, attname))

        return 0

