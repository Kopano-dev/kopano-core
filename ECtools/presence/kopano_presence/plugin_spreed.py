# SPDX-License-Identifier: AGPL-3.0-only
import threading
import time
from kopano import log_exc

class AutoUnavailableThread(threading.Thread):
    """ make users 'unavailable' after a configurable number of minutes """

    def run(self):
        while not self.stop:
            with log_exc(self.service.log):
                for info in self.data.values():
                    if int(time.time()) - info['last_update'] > self.limit*60:
                        self.service.log.info('spreed: auto unavailable')
                        self.data.pop(info['user_id'])
                        self.service.data_set(info['user_id'], 'spreed', 'unavailable', '')
            time.sleep(1)

class Plugin:
    def __init__(self, service):
        """ just need to setup auto-unavailability thread """

        self.service = service
        self.log = service.log
        self.thread = AutoUnavailableThread()
        self.thread.data = {}
        self.thread.limit = service.config['spreed_auto_unavailable']
        self.thread.service = service
        self.thread.stop = False
        self.thread.start()
        service.log.info('spreed: plugin enabled')

    def update(self, user_id, info):
        """ when user becomes other than 'unavailable', add it to thread to monitor time """

        if info['status'] != 'unavailable':
            self.thread.data[user_id] = info

    def disconnect(self):
        self.thread.stop = True
