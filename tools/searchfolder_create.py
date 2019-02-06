# SPDX-License-Identifier: AGPL-3.0-or-later
# Create a lot of dummy SFs
import kopano
s = kopano.server(auth_user='bar', auth_pass='xbar')
u = s.user('bar')
for x in range(0, 10000):
	findroot = u.root.folder('FINDER_ROOT') # search folders are stored here as regular MAPI folders, but with the data coming from special DB tables
	print 'searchfolder count:', findroot.subfolder_count # count search folders
	sf = u.create_searchfolder() # create search folder in 'findroot'
	sf.search_start(u.inbox, 'blah')
