# SPDX-License-Identifier: AGPL-3.0-only
from MAPI.Util import *
import plugintemplates

'''
convert uu-encoded elements in body to separate attachments

'''

SNIP_MSG = '(uuencoded file(s) converted to attachment(s))'
LOG_MSG = 'found %d uuencoded element(s) in body, converting to attachment(s)'

STATE_TEXT, STATE_UU = 0, 1

class UUDecode(plugintemplates.IMapiDAgentPlugin):
    def PostConverting(self, session, addrbook, store, folder, message):
        body = message.GetProps([PR_BODY],0)[0].Value
        lines = [line.strip() for line in body.strip().splitlines()]
        state = STATE_TEXT
        body2 = []
        attachments = []
        uulines = []
        
        for i in range(len(lines)): # no 'enumerate' for python 2.4 compatibility
            line = lines[i]
            if state == STATE_TEXT:
                split = line.split(' ')
                if len(split) >= 3 and split[0] == 'begin':
                    state = STATE_UU
                    uulines = [line]
                else:
                    body2.append(line)
            elif state == STATE_UU:
                uulines.append(line)
                if line == 'end' and lines[i-1] == '`' or line == 'end':
                    attachments.append(uulines)
                    body2.extend(['', SNIP_MSG, ''])
                    state = STATE_TEXT
        if state != STATE_TEXT:
            body2.extend(uulines)
        if attachments:
            self.logger.logDebug(LOG_MSG % len(attachments))
            for uulines in attachments:
                (id, attach) = message.CreateAttach(None, 0)
                fname = uulines[0].split(' ', 2)[2]
                self.logger.logDebug('filename: %s' % fname)
                attach.SetProps([SPropValue(PR_DISPLAY_NAME,fname ), SPropValue(PR_ATTACH_METHOD, 1)])
                attach.SetProps([SPropValue(PR_ATTACH_FILENAME,fname ), SPropValue(PR_ATTACH_METHOD, 1)])
                stream = attach.OpenProperty(PR_ATTACH_DATA_BIN, IID_IStream, 0, MAPI_MODIFY | MAPI_CREATE)
                stream.Write(('\n'.join(uulines)+'\n').decode('uu'))
                attach.SaveChanges(0)
            message.SetProps([SPropValue(PR_BODY, '\r\n'.join(body2))])
            message.SaveChanges(0)
        return plugintemplates.MP_CONTINUE,
