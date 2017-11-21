import json
import urlparse
import types

import falcon
import kopano


class Resource(object):
    def get_fields(self, obj, fields):
        return {f: self.fields[f](obj) for f in fields}

    def json(self, obj, fields):
        # dump requested fields
        return json.dumps(self.get_fields(obj, fields),
            indent=4, separators=(',', ': ')
        )

    def respond(self, req, resp, obj):
        # determine fields (default all)
        args = urlparse.parse_qs(req.query_string)
        if 'fields' in args:
            fields = args['fields'][0].split(',') # TODO 0?
        else:
            fields = self.fields.keys()

        # jsonify result (as stream)
        resp.content_type = "application/json"
        if isinstance(obj, types.GeneratorType):
#            #disable chunking for jelle
#            resp.append_header('transfer-encoding', 'chunked')
#            resp.stream = (self.json(o, fields)+'\n' for o in obj)
            resp.body = json.dumps(
                [self.get_fields(o, fields) for o in obj]
            )
        else:
            resp.body = self.json(obj, fields)

    def generator(self, req, generator):
        # TODO error for non-items?

        # determine pagination and ordering
        args = urlparse.parse_qs(req.query_string)
        start = int(args['start'][0]) if 'start' in args else None
        limit = int(args['limit'][0]) if 'limit' in args else None
        order = tuple(args['order'][0].split(',')) if 'order' in args else None

        return generator(page_start=start, page_limit=limit, order=order)

class UserResource(Resource):
    fields = {
        'userid': lambda user: user.userid,
        'name': lambda user: user.name,
        'fullname': lambda user: user.fullname,
        'store': lambda user: user.store.entryid,
    }

    def on_get(self, req, resp, userid=None):
        if userid:
            data = server.user(userid=userid)
        else:
            data = server.users()
        self.respond(req, resp, data)

class StoreResource(Resource):
    fields = {
        'entryid': lambda store: store.entryid,
        'public': lambda store: store.public,
        'user': lambda store: store.user.entryid if store.user else None,
    }

    def on_get(self, req, resp, storeid=None):
        if storeid:
            data = server.store(entryid=storeid)
        else:
            data = server.stores()
        self.respond(req, resp, data)

class FolderResource(Resource):
    fields = {
        'entryid': lambda folder: folder.entryid,
        'parent': lambda folder: folder.parent.entryid,
        'name': lambda folder: folder.name,
        'modified': lambda folder: folder.last_modified.isoformat()
    }

    def on_get(self, req, resp, storeid, folderid=None):
        store = server.store(entryid=storeid) # TODO cache?
        if folderid:
            data = store.folder(entryid=folderid)
        else:
            data = store.folders()
        self.respond(req, resp, data)

    def on_post(self, req, resp, storeid, folderid):
        store = server.store(entryid=storeid) # TODO cache?
        folder = store.folder(entryid=folderid)

        fields = json.loads(req.stream.read())
        item = folder.create_item(**fields) # TODO conversion

        resp.status = falcon.HTTP_201
        resp.location = req.path+'/items/'+item.entryid

    def on_put(self, req, resp, storeid, folderid):
        store = server.store(entryid=storeid) # TODO cache?
        folder = store.folder(entryid=folderid)

        data = json.loads(req.stream.read())
        if 'action' in data:
            action = data['action']
            items = [store.item(entryid=entryid) for entryid in data['items']]

            if action == 'send':
                for item in items:
                    item.send()
            elif action == 'delete':
                folder.delete(items)
            elif action == 'copy':
                target = store.folder(entryid=data['target'])
                folder.copy(items, target)
            elif action == 'move':
                target = store.folder(entryid=data['target'])
                folder.move(items, target)

class ItemResource(Resource):
    fields = {
        'entryid': lambda item: item.entryid,
        'subject': lambda item: item.subject,
        'to': lambda item: ['%s <%s>' % (to.name, to.email) for to in item.to],
        'text': lambda item: item.text,
        'modified': lambda item: item.last_modified.isoformat(),
        'received': lambda item: item.received.isoformat()
    }

    def on_get(self, req, resp, storeid, folderid, itemid=None):
        store = server.store(entryid=storeid) # TODO cache?
        if itemid:
            data = store.item(itemid)
        else:
            folder = store.folder(entryid=folderid)
            data = self.generator(req, folder.items)
        self.respond(req, resp, data)

server = kopano.Server(parse_args=False)

app = falcon.API()
users = UserResource()
stores = StoreResource()
items = ItemResource()
folders = FolderResource()

app.add_route('/users', users)
app.add_route('/users/{userid}', users)
app.add_route('/stores', stores)
app.add_route('/stores/{storeid}', stores)
app.add_route('/stores/{storeid}/folders', folders)
app.add_route('/stores/{storeid}/folders/{folderid}', folders)
app.add_route('/stores/{storeid}/folders/{folderid}/items', items)
app.add_route('/stores/{storeid}/folders/{folderid}/items/{itemid}', items)
