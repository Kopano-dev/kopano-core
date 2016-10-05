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

#include <kopano/platform.h>

#include <iostream>
#include <list>
#include <getopt.h>

using namespace std;

#include "LDAPConfigCheck.h"
#include "UnixConfigCheck.h"
#include "ServerConfigCheck.h"
#include "GatewayConfigCheck.h"
#include "IcalConfigCheck.h"
#include "MonitorConfigCheck.h"
#include "SpoolerConfigCheck.h"
#include "DAgentConfigCheck.h"

static const struct option long_options[] = {
	{ "help",   no_argument,		NULL, 'h' },
	{ "ldap",	required_argument,	NULL, 'l' },
	{ "unix",	required_argument,	NULL, 'u' },
	{ "server",	required_argument,	NULL, 's' },
	{ "gateway",required_argument,	NULL, 'g' },
	{ "ical",	required_argument,	NULL, 'i' },
	{ "monitor",required_argument,	NULL, 'm' },
	{ "spooler",required_argument,	NULL, 'p' },
	{ "dagent", required_argument,	NULL, 'a' },
	{ "hosted", required_argument,	NULL, 'c' },
	{ "distributed", required_argument,	NULL, 'd' },
	{},
};

static void print_help(char *lpszName)
{
	cout << "Configuration files validator tool" << endl;
	cout << endl;
	cout << "Usage:" << endl;
	cout << lpszName << " [options]" << endl;
	cout << endl;
	cout << "[-l|--ldap] <file>\tLocation of LDAP plugin configuration file" << endl;
	cout << "[-u|--unix] <file>\tLocation of Unix plugin configuration file" << endl;
	cout << "[-s|--server] <file>\tLocation of kopano-server configuration file" << endl;
	cout << "[-g|--gateway] <file>\tLocation of kopano-gateway configuration file" << endl;
	cout << "[-i|--ical] <file>\tLocation of kopano-ical configuration file" << endl;
	cout << "[-m|--monitor] <file>\tLocation of kopano-monitor configuration file" << endl;
	cout << "[-p|--spooler] <file>\tLocation of kopano-spooler configuration file" << endl;
	cout << "[-a|--dagent] <file>\tLocation of kopano-dagent configuration file" << endl;
	cout << "[-c|--hosted] [y|n]\tForce multi-company/hosted support to \'on\' or \'off\'" << endl;
	cout << "[-d|--distributed] [y|n]\tForce multi-server/distributed support to \'on\' or \'off\'" << endl;
	cout << "[-h|--help]\t\tPrint this help text" << endl;
}

int main(int argc, char* argv[])
{
	list<ECConfigCheck*> check;
	std::string strHosted, strMulti;
	bool bHosted = false;
	bool bMulti = false;

	while (true) {
		char c = getopt_long(argc, argv, "l:u:s:g:i:m:p:a:c:d:h", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'l':
			check.push_back(new LDAPConfigCheck(optarg));
			break;
		case 'u':
			check.push_back(new UnixConfigCheck(optarg));
			break;
		case 's':
			check.push_back(new ServerConfigCheck(optarg));
			/* Check if hosted is enabled, make sure we don't overwrite commandline */
			if (strHosted.empty())
				strHosted = (*check.rbegin())->getSetting("enable_hosted_kopano");
			break;
		case 'g':
			check.push_back(new GatewayConfigCheck(optarg));
			break;
		case 'i':
			check.push_back(new IcalConfigCheck(optarg));
			break;
		case 'm':
			check.push_back(new MonitorConfigCheck(optarg));
			break;
		case 'p':
			check.push_back(new SpoolerConfigCheck(optarg));
			break;
		case 'a':
			check.push_back(new DAgentConfigCheck(optarg));
			break;
		case 'c':
			strHosted = optarg;
			break;
		case 'd':
			strMulti = optarg;
			break;
		case 'h':
		default:
			print_help(argv[0]);
			return 0;
		}
	}

	if (check.empty()) {
		print_help(argv[0]);
		return 1;
	}

	bHosted = (strHosted[0] == 'y' || strHosted[0] == 'Y' ||
			   strHosted[0] == 't' || strHosted[0] == 'T' ||
			   strHosted[0] == '1');
	bMulti = (strMulti[0] == 'y' || strMulti[0] == 'Y' ||
			   strMulti[0] == 't' || strMulti[0] == 'T' ||
			   strMulti[0] == '1');

	for (const auto &it : check) {
		if (it->isDirty())
			continue;
		it->setHosted(bHosted);
		it->setMulti(bMulti);
		it->loadChecks();
		it->validate();
		/* We are only looping through the list once, just cleanup
		 * and don't care about leaving broken pointers in the list. */
		delete it;
	}

	return 0;
}

