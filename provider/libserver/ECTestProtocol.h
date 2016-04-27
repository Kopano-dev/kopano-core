/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <kopano/kcodes.h>

struct soap;
class ECSession;

ECRESULT TestPerform(struct soap *soap, ECSession *lpSession, char *szCommand, unsigned int ulArgs, char **args);
ECRESULT TestSet(struct soap *soap, ECSession *lpSession, char *szVarName, char *szValue);
ECRESULT TestGet(struct soap *soap, ECSession *lpSession, char *szVarName, char **szValue);
            

#endif
