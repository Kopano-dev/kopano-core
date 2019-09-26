# SPDX-License-Identifier: AGPL-3.0-only
from kopano import log_exc
import sleekxmpp

STATUS_MAP = {  # map xmpp statuses to statuses supported by kopano-presence
    'available': 'available',
    'chat': 'available',
    'dnd': 'busy',
    'away': 'away',
    'xa': 'away',
    'unavailable': 'unavailable',
}

class XmppPresence(sleekxmpp.ClientXMPP):
    """ monitor status events """

    def __init__(self, jid, password, service):
        """ setup event handlers """

        sleekxmpp.ClientXMPP.__init__(self, jid, password)
        self.service = service
        self.add_event_handler("session_start", self.start)
        self.add_event_handler("changed_status", self.status_event)

    def start(self, event):
        """ this is needed to get presence updates """

        self.send_presence()

    def status_event(self, msg):
        """ parse incoming status, and update presence service """

        with log_exc(self.service.log):
            if self.service.config['xmpp_user_id_strip_domain']:
                username = str(msg['from']).split('/')[0].split('@')[0] # strip entire domain
            else:
                username = str(msg['from']).split('/')[0].replace('@chat.', '@') # XXX chat?
            self.service.data_set(username, 'xmpp', STATUS_MAP[msg['type']], msg['status'])

class Plugin:
    def __init__(self, service):
        """ setup xmpp background thread(s) """

        self.service = service
        self.log = service.log
        self.log.info('xmpp: connecting to server')
        self.xmpp = XmppPresence(service.config['xmpp_jid'], service.config['xmpp_password'], service)
        if self.xmpp.connect(reattempt=False):
            self.xmpp.process()
            self.log.info('xmpp: plugin enabled')
        else:
            self.log.error('xmpp: could not connect to server')

    def disconnect(self):
        self.xmpp.disconnect()
