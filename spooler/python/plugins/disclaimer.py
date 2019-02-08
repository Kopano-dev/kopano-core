# SPDX-License-Identifier: AGPL-3.0-only
import MAPI
from MAPI.Util import *
from MAPI.Time import *
from MAPI.Struct import *

from plugintemplates import *

import os
import codecs


class Disclaimer(IMapiSpoolerPlugin):

    disclaimerdir = '/etc/kopano/disclaimers'

    def bestBody(self, message):
        tag = PR_NULL
        bodytag = PR_BODY_W # todo use flags to support PR_BODY_A
        props = message.GetProps([bodytag, PR_HTML, PR_RTF_COMPRESSED, PR_RTF_IN_SYNC], 0)

        if (props[3].ulPropTag != PR_RTF_IN_SYNC):
            return tag

        if((props[0].ulPropTag == bodytag or ( PROP_TYPE(props[0].ulPropTag) == PT_ERROR and props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY) ) and
           (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and props[1].Value == MAPI_E_NOT_FOUND) and
           (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and props[2].Value == MAPI_E_NOT_FOUND)):
            tag = bodytag
        elif((props[1].ulPropTag == PR_HTML or ( PROP_TYPE(props[1].ulPropTag) == PT_ERROR and props[1].Value == MAPI_E_NOT_ENOUGH_MEMORY) ) and
             (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY) and
             (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and props[2].Value == MAPI_E_NOT_ENOUGH_MEMORY) and
             props[3].Value == False):
            tag = PR_HTML
        elif((props[2].ulPropTag == PR_RTF_COMPRESSED or ( PROP_TYPE(props[2].ulPropTag) == PT_ERROR and props[2].Value == MAPI_E_NOT_ENOUGH_MEMORY) ) and
             (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY) and
             (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and props[1].Value == MAPI_E_NOT_FOUND) and
             props[3].Value == True):
            tag = PR_RTF_COMPRESSED

        return tag

    def getCharSetByCP(self, codepage):
        cp2char = { 20106:        "DIN_66003",
                    20108:        "NS_4551-1",
                    20107:     "SEN_850200_B",
                    950:          "big5",
                    50221:      "csISO2022JP",
                    51932:           "euc-jp",
                    51936:           "euc-cn",
                    51949:           "euc-kr",
                    949:           "euc-kr",
                    949:            "cp949",
                    949:   "ks_c_5601-1987",
                    936:          "gb18030",
                    936:           "gb2312",
                    936:           "GBK",
                    52936:         "csgb2312",
                    852:           "ibm852",
                    866:           "ibm866",
                    50220:      "iso-2022-jp",
                    50222:      "iso-2022-jp",
                    50225:      "iso-2022-kr",
                    1252:     "windows-1252",
                    1252:       "iso-8859-1",
                    28591:       "iso-8859-1",
                    28592:       "iso-8859-2",
                    28593:       "iso-8859-3",
                    28594:       "iso-8859-4",
                    28595:       "iso-8859-5",
                    28596:       "iso-8859-6",
                    28597:       "iso-8859-7",
                    28598:       "iso-8859-8",
                    28598:     "iso-8859-8-i",
                    28599:       "iso-8859-9",
                    28603:      "iso-8859-13",
                    28605:      "iso-8859-15",
                    20866:           "koi8-r",
                    21866:           "koi8-u",
                    932:        "shift-jis",
                    1200:          "unicode",
                    1201:       "unicodebig",
                    65000:            "utf-7",
                    65001:            "utf-8",
                    1250:     "windows-1250",
                    1251:     "windows-1251",
                    1253:     "windows-1253",
                    1254:     "windows-1254",
                    1255:     "windows-1255",
                    1256:     "windows-1256",
                    1257:     "windows-1257",
                    1258:     "windows-1258",
                    874:      "windows-874",
                    20127:         "us-ascii" }
        try:
            return cp2char[codepage]
        except:
            return "us-ascii"

    def getDisclaimer(self, extension, company):
        if company == None:
            company = 'default'

        name = os.path.join(self.disclaimerdir, company + '.' + extension)

        self.logger.logDebug("*--- Open disclaimer file '%s'" % (name) )

        return open(name, 'rb').read()

    def PreSending(self, session, addrbook, store, folder, message):

        if os.path.isdir(self.disclaimerdir) == False:
           self.logger.logWarn("!--- Disclaimer directory '%s' doesn't exist." % self.disclaimerdir)
           return MP_CONTINUE,

        company = None

        props = store.GetProps([PR_USER_ENTRYID], 0)
        if props[0].ulPropTag == PR_USER_ENTRYID:
            currentuser = session.OpenEntry(props[0].Value, None, 0)

            userprops = currentuser.GetProps([PR_EC_COMPANY_NAME_W], 0)
            if userprops[0].ulPropTag == PR_EC_COMPANY_NAME_W and len(userprops[0].Value) > 0:
                company = userprops[0].Value.encode("utf-8")
                self.logger.logDebug("*--- Company name is '%s'" % (company) )


        bodytag = self.bestBody(message)

        self.logger.logDebug("*--- The message bestbody 0x%08X" % bodytag)
        if bodytag == PR_BODY_W:

            disclaimer = u"\r\n" + codecs.decode(self.getDisclaimer('txt', company), 'utf-8')

            bodystream = message.OpenProperty(PR_BODY_W, IID_IStream, 0, MAPI_MODIFY)
            bodystream.Seek(0, STREAM_SEEK_END)
            bodystream.Write(disclaimer.encode('utf-32-le'))
            bodystream.Commit(0)

        elif bodytag == PR_HTML:
            charset = "us-ascii"
            props = message.GetProps([PR_INTERNET_CPID], 0)
            if props[0].ulPropTag == PR_INTERNET_CPID:
                charset = self.getCharSetByCP(props[0].Value)

            disclaimer = u"<br>" + codecs.decode(self.getDisclaimer('html', company), 'utf-8')

            stream = message.OpenProperty(PR_HTML, IID_IStream, 0, MAPI_MODIFY)
            stream.Seek(0, STREAM_SEEK_END)
            stream.Write(disclaimer.encode(charset))
            stream.Commit(0)

        elif bodytag == PR_RTF_COMPRESSED:
            self.logger.logWarn("!--- RTF disclaimer is not supported")
            # RTF not supported because this cause body issues.
            return MP_CONTINUE,

            rtf = self.getDisclaimer('rtf', company)

            stream = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, 0, MAPI_MODIFY)
            uncompressed = WrapCompressedRTFStream(stream, MAPI_MODIFY)

            # Find end tag
            uncompressed.Seek(-5, STREAM_SEEK_END)
            data = uncompressed.Read(5)
            for i in range(4, 0, -1):
                if data[i] == '}':
                    uncompressed.Seek(i-5, STREAM_SEEK_END)
                    break

            uncompressed.Write(rtf)
            uncompressed.Commit(0)
            stream.Commit(0)
        else:
            self.logger.logWarn("!--- No Body exists")

        return MP_CONTINUE,
