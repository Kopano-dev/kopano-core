# SPDX-License-Identifier: AGPL-3.0-only
import hashlib
import hmac
import time

t = int(time.time())
userid = 'markd@kopano.com'
secret = 'GEHEIM'

print '%d:%s:%s' % (t, userid, hmac.new(secret, '%d:%s' % (t, userid), hashlib.sha256).digest().encode('base64').strip().upper())

