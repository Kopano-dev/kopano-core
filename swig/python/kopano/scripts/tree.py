#!/usr/bin/python

# print store structure, down to attachments

# usage: ./tree.py [-u username]

import kopano

server = kopano.Server()
for company in server.companies(): # checks -c/--company command-line option
    print(company)
    for user in company.users(): # checks -u/--user command-line option
                                 # server.users() gives all users
         print('  ' + user.name)
         for folder in user.store.folders():
             indent = (folder.depth + 2) * '  '
             print(indent + str(folder))
             for item in folder:
                 print(indent + '  ' + str(item))
                 for attachment in item.attachments():
                     print(indent + '    ' + attachment.filename)
