/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <getopt.h>
#include <string.h>
#include <kopano/my_getopt.h>

namespace KC {

int my_getopt_long_permissive(int argc, char **argv, const char *shortopts,
    const struct option *longopts, int *longind)
{
	int opterr_save = opterr, saved_optind = optind;
	opterr = 0;

	int c = getopt_long(argc, argv, shortopts, longopts, longind);
	if (c == '?') {
		// Move this parameter to the end of the list if it a long option
		if (argv[optind - 1][0] == '-' && argv[optind - 1][1] == '-' && argv[optind - 1][2] != '\0') {
			int i = optind - 1;
			/*
			 * Continue parsing at the next argument before moving the unknown
			 * option to the end, otherwise a potentially endless loop could
			 * ensue.
			 */
			c = getopt_long(argc, argv, shortopts, longopts, longind);

			char *tmp = argv[i];

			int move_count = (argc - i) - i;
			if (move_count > 0)
				memmove(&argv[i], &argv[i + 1], move_count * sizeof(char *));

			argv[i] = tmp;
			--optind;
			--saved_optind;
		}
	}

	opterr = opterr_save;
	// Show error
	if (c == '?') {
		optind = saved_optind;
		if (getopt_long(argc, argv, shortopts, longopts, longind) != 0)
			/* ignore return value */;
	}
	return c;
}

} /* namespace */
