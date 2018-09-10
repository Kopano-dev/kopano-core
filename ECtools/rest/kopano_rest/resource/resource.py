# SPDX-License-Identifier: AGPL-3.0-or-later
import calendar
import datetime
try:
    import ujson as json
except ImportError: # pragma: no cover
    import json
import time

try:
    import urlparse
except ImportError:
    import urllib.parse as urlparse

import pytz
import dateutil
from jsonschema import ValidationError

UTC = dateutil.tz.tzutc()

INDENT = True
try:
    json.dumps({}, indent=True) # ujson 1.33 doesn't support 'indent'
except TypeError: # pragma: no cover
    INDENT = False

import dateutil.parser
import falcon

DEFAULT_TOP = 10

def _header_args(req, name): # TODO use urlparse.parse_qs or similar..?
    d = {}
    header = req.get_header(name)
    if header:
        for arg in header.split(';'):
            k, v = arg.split('=')
            d[k] = v
    return d

def _header_sub_arg(req, name, arg):
    args = _header_args(req, name)
    if arg in args:
        return args[arg].strip('"')

def _date(d, local=False, show_time=True):
    if d is None:
        return '0001-01-01T00:00:00Z'
    fmt = '%Y-%m-%d'
    if show_time:
        fmt += 'T%H:%M:%S'
    if d.microsecond:
        fmt += '.%f'
    if not local:
        fmt += 'Z'
    # TODO make pyko not assume naive localtime..
    seconds = time.mktime(d.timetuple())
    d = datetime.datetime.utcfromtimestamp(seconds)
    return d.strftime(fmt)

def _tzdate(d, req):
    if d is None:
        return None

    fmt = '%Y-%m-%dT%H:%M:%S'

    # local to UTC
    seconds = time.mktime(d.timetuple())
    d = datetime.datetime.utcfromtimestamp(seconds)

    # apply timezone preference header
    pref_timezone = _header_sub_arg(req, 'Prefer', 'outlook.timezone')
    if pref_timezone:
        try:
            tzinfo = pytz.timezone(pref_timezone)
        except Exception as e:
            raise falcon.HTTPBadRequest(None, "A valid TimeZone value must be specified. The following TimeZone value is not supported: '%s'." % pref_timezone)
        d = d.replace(tzinfo=UTC).astimezone(tzinfo).replace(tzinfo=None)
    else:
        pref_timezone = 'UTC'

    return {
        'dateTime': d.strftime(fmt),
        'timeZone': pref_timezone, # TODO error
    }

def _naive_local(d): # TODO make pyko not assume naive localtime..
    if d.tzinfo is not None:
        return d.astimezone(datetime.timezone.utc).replace(tzinfo=None)
    else:
        return d

def set_date(item, field, arg):
    d = dateutil.parser.parse(arg['dateTime'])
    seconds = calendar.timegm(d.timetuple())
    d = datetime.datetime.fromtimestamp(seconds)
    setattr(item, field, d)

def _parse_qs(req):
    args = urlparse.parse_qs(req.query_string)
    for arg, values in args.items():
        if len(values) > 1:
            raise falcon.HTTPBadRequest(None, "Query option '%s' was specified more than once, but it must be specified at most once." % arg)

    for key in ('$top', '$skip'):
        if key in args:
            value = args[key][0]
            if not value.isdigit():
                raise falcon.HTTPBadRequest(None, "Invalid value '%s' for %s query option found. The %s query option requires a non-negative integer value." % (value, key, key))

    return args

def _parse_date(args, key):
    try:
        value = args[key][0]
    except KeyError:
        raise falcon.HTTPBadRequest(None, 'This request requires a time window specified by the query string parameters StartDateTime and EndDateTime.')
    try:
        return _naive_local(dateutil.parser.parse(value))
    except ValueError:
        raise falcon.HTTPBadRequest(None, "The value '%s' of parameter '%s' is invalid." % (value, key))

def _start_end(req):
    args = _parse_qs(req)
    return _parse_date(args, 'startDateTime'), _parse_date(args, 'endDateTime')

class Resource(object):
    def __init__(self, options):
        self.options = options

    def get_fields(self, req, obj, fields, all_fields):
        fields = fields or all_fields or self.fields
        result = {}
        for f in fields:
            if f in all_fields:
                if all_fields[f].__code__.co_argcount == 1:
                    result[f] = all_fields[f](obj)
                else:
                    result[f] = all_fields[f](req, obj)

        # TODO do not handle here
        if '@odata.type' in result and not result['@odata.type']:
            del result['@odata.type']
        return result

    def json(self, req, obj, fields, all_fields, multi=False, expand=None):
        data = self.get_fields(req, obj, fields, all_fields)
        if not multi:
            data['@odata.context'] = req.path
        if expand:
            data.update(expand)
        if INDENT:
            return json.dumps(data, indent=2)
        else:
            return json.dumps(data)

    def json_multi(self, req, obj, fields, all_fields, top, skip, count, deltalink, add_count=False):
        header = b'{\n'
        header += b'  "@odata.context": "%s",\n' % req.path.encode('utf-8')
        if add_count:
            header += b'  "@odata.count": "%d",\n' % count
        if deltalink:
            header += b'  "@odata.deltaLink": "%s",\n' % deltalink
        else:
            path = req.path
            if req.query_string:
                args = _parse_qs(req)
                if '$skip' in args:
                    del args['$skip']
                path += '?'+'&'.join(a+'='+','.join(b) for (a,b) in args.items())
            header += b'  "@odata.nextLink": "%s?$skip=%d",\n' % (path.encode('utf-8'), skip+top)
        header += b'  "value": [\n'
        yield header
        first = True
        for o in obj:
            if isinstance(o, tuple):
                o, resource = o
                all_fields = resource.fields
            if not first:
                yield b',\n'
            first = False
            wa = self.json(req, o, fields, all_fields, multi=True).encode('utf-8')
            yield b'\n'.join([b'    '+line for line in wa.splitlines()])
        yield b'\n  ]\n}'

    def respond(self, req, resp, obj, all_fields=None, deltalink=None):
        # determine fields
        args = _parse_qs(req)
        if '$select' in args:
            fields = set(args['$select'][0].split(',') + ['@odata.type', '@odata.etag', 'id'])
        else:
            fields = None

        resp.content_type = "application/json"

        pref_body_type = _header_sub_arg(req, 'Prefer', 'outlook.body-content-type')
        if pref_body_type in ('text', 'html'):
            resp.set_header('Preference-Applied', 'outlook.body-content-type='+pref_body_type) # TODO graph doesn't do this actually?
        # TODO add outlook.timezone

        # multiple objects: stream
        if isinstance(obj, tuple):
            obj, top, skip, count = obj
            add_count = '$count' in args and args['$count'][0] == 'true'

            resp.stream = self.json_multi(req, obj, fields, all_fields or self.fields, top, skip, count, deltalink, add_count)

        # single object
        else:
            # expand sub-objects # TODO stream?
            expand = None
            if '$expand' in args:
                expand = {}
                for field in args['$expand'][0].split(','):
                    if hasattr(self, 'relations') and field in self.relations:
                        objs, resource = self.relations[field](obj)
                        expand[field] = [self.get_fields(req, obj2, resource.fields, resource.fields) for obj2 in objs()]

                    elif hasattr(self, 'expansions') and field in self.expansions:
                        obj2, resource = self.expansions[field](obj)
                        # TODO item@odata.context, @odata.type..
                        expand[field.split('/')[1]] = self.get_fields(req, obj2, resource.fields, resource.fields)

            resp.body = self.json(req, obj, fields, all_fields or self.fields, expand=expand)

    def respond_204(self, resp): # TODO integrate with respond, status_code=..?
        resp.set_header('Content-Length', '0') # https://github.com/jonashaag/bjoern/issues/139
        resp.status = falcon.HTTP_204

    def generator(self, req, generator, count=0):
        # determine pagination and ordering
        args = _parse_qs(req)
        top = int(args['$top'][0]) if '$top' in args else DEFAULT_TOP
        skip = int(args['$skip'][0]) if '$skip' in args else 0
        order = args['$orderby'][0].split(',') if '$orderby' in args else None
        if order:
            order = tuple(('-' if len(o.split()) > 1 and o.split()[1] == 'desc' else '')+o.split()[0] for o in order)
        return (generator(page_start=skip, page_limit=top, order=order), top, skip, count)

    def create_message(self, folder, fields, all_fields=None):
        # TODO item.update and/or only save in the end
        item = folder.create_item()

        for field in (all_fields or self.set_fields):
            if field in fields:
                (all_fields or self.set_fields)[field](item, fields[field])

        return item

    def folder_gen(self, req, folder):
        args = _parse_qs(req) # TODO generalize
        if '$search' in args:
            query = args['$search'][0]
            def yielder(**kwargs):
                for item in folder.items(query=query):
                    yield item
            return self.generator(req, yielder, 0)
        else:
            return self.generator(req, folder.items, folder.count)

    def parse_qs(self, req):
        return _parse_qs(req)

    def load_json(self, req):
        try:
            return json.loads(req.stream.read().decode('utf-8'))
        except ValueError as e:
            raise falcon.HTTPBadRequest(None, "Invalid JSON")

    def validate_json(self, schema, fields):
        try:
            schema.validate(fields)
        except ValidationError as e:
            raise falcon.HTTPBadRequest(None, "JSON schema violation: %s " % e.message)
