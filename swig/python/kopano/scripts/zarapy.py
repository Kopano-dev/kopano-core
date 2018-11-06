#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# simple html-based viewer (users, folders, items)

import cherrypy
import kopano

cherrypy.server.socket_host = '127.0.0.1'

class ZaraPy(object):
    @cherrypy.expose
    def index(self):
        html ='<html>'

        for company in kopano.Server().companies():
            html = html + '<table>'
            html = html + "<tr><th>company.name</th><th>user.name </th><th> user.store.guid</th><th>user.server.name</th><tr>\n"
            for user in kopano.Server().company(company.name).users():
                html = html + "<tr><td>" + company.name + "</td><td><a href=/viewuser/"+ company.name + "/" + user.name + ">" + user.name + "</a><td><a href=/listfolders/"+ company.name + "/" + user.name + ">Folders</a></td></td><td>" + user.store.guid + "</td><td>" + user.server.name + "</td><tr>\n"
            html = html + '</table>\n'

            html =  html + '</html>\n'

        return html

    @cherrypy.expose
    def viewuser(self, company=None,user=None):
        user = dict(user=user)
        company = dict(company=company)

        html = '<html><h1>Company: %s <br>User: %s</h1>' % (company['company'],user['user'])
        html = html + "<table><tr><th>prop.idname</th><th>prop.proptype</th><th>prop.value</th><tr>\n"

        for props in kopano.Server().company(company['company']).user(user['user']).props():
            if props.typename == 'PT_BINARY':
                html = html + "<tr><td>" + str(props.idname) + "</td><td>" +  "</td><td>" + str(props.value.encode('hex').upper()) + "</td><tr>\n"
            else:
                html = html + "<tr><td>" + str(props.idname) + "</td><td>" + "</td><td>" + str(props.value) + "</td><tr>\n"
    
        html = html + '</table></html>\n'
        return html


    @cherrypy.expose
    def listfolders(self, company=None,user=None,folder=None):
        user = dict(user=user)
        company = dict(company=company)
        foldername= dict(folder=folder)
        html = '<html><h1>Company: %s <br>User: %s</h1>' % (company['company'],user['user'])
        if foldername['folder']:
            html = html + "<table><tr><th>item.subject</th><th>item.entryid</th><tr>\n"
            for folder in kopano.Server().company(company['company']).user(user['user']).store.folders():
                if folder.entryid == foldername['folder']:
                    for item in folder.items():
                        html = html + "<tr><td><a href=/listitem/%s/%s/%s/%s>%s</a></td><td>%s</td><tr>\n" %( company['company'],user['user'],folder.entryid,item.entryid.encode('hex'),item.subject.encode("utf-8"),item.entryid.encode('hex'))
        else:
            html = html + "<table><tr><th>item.subject</th><th>item.entryid</th><tr>\n"
            for folder in kopano.Server().company(company['company']).user(user['user']).store.folders():
                html = html + "<tr><td><a href=/listfolders/%s/%s/%s>%s</a></td><td>%s</td><tr>\n" %( company['company'],user['user'],folder.entryid,folder.name,folder.entryid)
        
        html = html + '</table></html>\n'

        return html

    @cherrypy.expose
    def listitem(self, company=None,user=None,folder=None,item=None):
        user = dict(user=user)
        company = dict(company=company)
        itemname= dict(item=item)
        foldername= dict(folder=folder)
        html=''
        print(itemname['item'] + "\n")
        html = '<html><h1>Company: %s <br>User: %s</h1>' % (company['company'],user['user'])
        #html = html + "<table><tr><th>prop.name</th><th>prop.value</th><tr>\n"
        if itemname['item']:
            #html = html + "<table><tr><th>item.subject</th><th>item.entryid</th><tr>\n"
            for folder in kopano.Server().company(company['company']).user(user['user']).store.folders():
                if folder.entryid == foldername['folder']:
                    for item in folder.items():
                        print("curr %s" , item.entryid.encode('hex'))
                        print("look %s", itemname['item'])
                        if item.entryid.encode('hex')==itemname['item']:
                            print("match")
                            for prop in item.props():
                                if (prop.idname == 'PR_HTML') or (prop.idname=='PR_BODY'):
                                    try:
                                        html = html + "<table><tr><td>%s</td><td>%s</td><tr>\n" % ( prop.name , str(prop.value))
                                    except:
                                        pass
        
        html = html + '</table></html>\n'

        return html

if __name__ == '__main__':
    conf = {
        '/': {
            'tools.sessions.on': True
        }
    }
    cherrypy.quickstart(ZaraPy())
