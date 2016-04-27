#!/bin/sh
curl -u presence:presence -X GET -d '{"AuthenticationToken":"1421324321:markd@kopano.com:WFAI3NU3YBAA91F4+9BOILXYGTPAV+PVDRNKIJ+JCVY=", "Type": "UserStatus", "UserStatus": [{"user_id": "guido@psf.org"}, {"user_id": "rms@gnu.org"}, {"user_id": "markd"}]}' http://localhost:1234/ -H "Content-Type: application/json"
