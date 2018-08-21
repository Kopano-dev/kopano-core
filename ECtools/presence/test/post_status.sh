#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
curl -u presenca:presence -X GET -d '{"AuthenticationToken":"1421324321:markd@kopano.com:WFAI3NU3YBAA91F4+9BOILXYGTPAV+PVDRNKIJ+JCVY=", "Type": "UserStatus", "UserStatus": [{"user_id": "guido@psf.org"}, {"user_id": "rms@gnu.org"}, {"user_id": "markd"}, {"user_id": "ryan"}]}' http://localhost:1234/ -H "Content-Type: application/json"
