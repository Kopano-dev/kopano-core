from .api_v0.rest import RestAPIv0
from wsgiref.simple_server import make_server

app = RestAPIv0()

s = make_server('localhost', 8000, app)
s.serve_forever()
