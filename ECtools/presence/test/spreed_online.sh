#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
curl -u presence:presence -X PUT -d '{"AuthenticationToken":"1421324321:markd@kopano.com:WFAI3NU3YBAA91F4+9BOILXYGTPAV+PVDRNKIJ+JCVY=", "Type": "UserStatus", "UserStatus": [{"user_id": "markd", "spreed":{"status":"available","message":"feeling chatty.."}}]}' http://localhost:1234/ -H "Content-Type: application/json"
#./get_status.sh
