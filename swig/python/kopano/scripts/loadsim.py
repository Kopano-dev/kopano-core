#! /usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# very simple load simulator

import os
import random
import sys
import time
import kopano

parser = kopano.parser('SKQC', usage='loadsim [options]')
parser.add_option('-u', '--user', dest='user', action='store', help='user to send mails to')
parser.add_option('-N', '--n-write-workers', dest='n_write_workers', action='store', help='number of write workers to start', default=1)
parser.add_option('-n', '--n-read-workers', dest='n_read_workers', action='store', help='number of read workers to start', default=1)
parser.add_option('-r', '--new-session', dest='restart_session', action='store_false', help='start a new session for each iteration')
parser.add_option('-R', '--random-new-session', dest='random_restart_session', action='store_false', help='randomly start a new session for an iteration')
parser.add_option('-e', '--eml', dest='eml', action='store', help='eml-file to use')

o, a = parser.parse_args()

if o.eml == None:
	print('EML file missing')
	sys.exit(1)

eml_file_data = open(o.eml, 'rb').read()

random.seed()

def read_worker():
	server = None

	while True:
		try:
			if o.restart_session == True or server == None or (o.random_restart_session and random.randint(0, 1) == 1):
				server = kopano.server(o)

			u = server.user(o.user)
			for folder in u.store.folders():
				for item in folder:
					if random.randint(0, 1) == 1:
						dummy = [(att.filename, att.mimetype, len(att.data)) for att in item.attachments()]

		except KeyboardInterrupt:
			return

		except Exception as e:
			print(e)

def write_worker():
	server = None

	while True:
		try:
			if o.restart_session == True or server == None or (o.random_restart_session and random.randint(0, 1) == 1):
				server = kopano.server(o)

			item = server.user(o.user).store.inbox.create_item(eml = eml_file_data)

		except KeyboardInterrupt:
			return

		except Exception as e:
			print(e)

pids = []

print('Starting %s writers' % o.n_write_workers)
for i in range(int(o.n_write_workers)):
	pid = os.fork()

	if pid == 0:
		write_worker()
		sys.exit(0)

	elif pid == -1:
		print('fork failed')
		break;

	pids.append(pid)

print('Starting %s readers' % o.n_read_workers)
for i in range(int(o.n_read_workers)):
	pid = os.fork()

	if pid == 0:
		read_worker()
		sys.exit(0)

	elif pid == -1:
		print('fork failed')
		break;

	pids.append(pid)

print('Child processes started')

try:
	while True:
		time.sleep(86400)

except:
	pass

print('Terminating...')

for pid in pids:
	try:
		os.kill(pid, -9)

	except:
		pass

print('Finished.')
