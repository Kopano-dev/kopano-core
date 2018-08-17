#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-only

# sending a basic mail

# usage: change hard-coded stuff and run

import kopano
 
kopano.User('user1').outbox.create_item(subject='subject', to='user1@domain.com; user2@domain.com', body='this is a body').send()
