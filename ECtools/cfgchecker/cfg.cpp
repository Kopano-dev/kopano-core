/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <iostream>
#include <list>
#include <memory>
#include <getopt.h>
#include "ECConfigCheck.h"

using std::cout;
using std::endl;

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
	std::list<std::unique_ptr<ECConfigCheck>> check;
	std::string strHosted, strMulti;

	while (true) {
		char c = getopt_long(argc, argv, "l:u:s:g:i:m:p:a:c:d:h", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'l':
			check.emplace_back(new LDAPConfigCheck(optarg));
			break;
		case 'u':
			check.emplace_back(new UnixConfigCheck(optarg));
			break;
		case 's':
			check.emplace_back(new ServerConfigCheck(optarg));
			/* Check if hosted is enabled, make sure we don't overwrite commandline */
			if (strHosted.empty())
				strHosted = check.back()->getSetting("enable_hosted_kopano");
			break;
		case 'g':
		case 'i':
			fprintf(stderr, "-%c option is currently ignored because no checks have been implemented", c);
			break;
		case 'm':
			check.emplace_back(new MonitorConfigCheck(optarg));
			break;
		case 'p':
			check.emplace_back(new SpoolerConfigCheck(optarg));
			break;
		case 'a':
			check.emplace_back(new DAgentConfigCheck(optarg));
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

	bool bHosted = (strHosted[0] == 'y' || strHosted[0] == 'Y' ||
			   strHosted[0] == 't' || strHosted[0] == 'T' ||
			   strHosted[0] == '1');
	bool bMulti = (strMulti[0] == 'y' || strMulti[0] == 'Y' ||
			   strMulti[0] == 't' || strMulti[0] == 'T' ||
			   strMulti[0] == '1');

	for (const auto &it : check) {
		if (it->isDirty())
			continue;
		it->setHosted(bHosted);
		it->setMulti(bMulti);
		it->loadChecks();
		it->validate();
	}
	return 0;
}
