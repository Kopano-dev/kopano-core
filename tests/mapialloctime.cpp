/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright 2016, Kopano and its licensors */
#include <chrono>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mapix.h>
/*
 * This program simulates MAPIAllocateMore allocations to measure allocation
 * time. Some preparatory work is needed:
 *
 * == Grab an allocation historgram ==
 * 1. Stick the following line into the MAPIFreeBuffer function:
 * 	fprintf(stderr, "!! %d\n", head->children.size());
 *    This tell us the size of each allocation list at the end of its lifetime.
 *
 * 2. Build KC and run the testsuite, then collect the "!!" lines from stderr
 *    into a file.
 *
 * 3. Make a histogram {bangbangvalue, #occurrences} of the data, and rewrite
 *    dist[] accordingly.
 *
 * == Run ==
 * 4. This program then runs as many allocations as they occurred in the TS,
 *    and prints the walltime.
 *
 * 5. Now go, hack on MAPIAllocateMore, and bring down that walltime.
 *
 * The allocation sizes do not matter, so that is fixed.
 */

/* MAPIAllocateBuffer size distribution */
static constexpr std::pair<unsigned int, unsigned int> dist[] = {
	{0, 590147}, {1, 458653}, {2, 22764}, {3, 13161}, {4, 15343},
	{5, 3952}, {6, 10789}, {7, 9420}, {8, 29092}, {9, 47}, {10, 824},
	{11, 37}, {12, 1424}, {13, 70}, {14, 622}, {15, 38}, {16, 55},
	{17, 20}, {18, 18}, {19, 174}, {20, 78}, {21, 6}, {22, 18}, {23, 91},
	{25, 13}, {28, 7}, {29, 22}, {31, 23}, {33, 5}, {35, 2}, {36, 9},
	{37, 3}, {38, 22}, {39, 13}, {40, 8}, {41, 2}, {42, 6}, {44, 1},
	{46, 2}, {47, 36}, {48, 3}, {49, 7}, {50, 4}, {54, 2}, {59, 20},
	{70, 1}, {72, 2}, {81, 77}, {82, 1}, {84, 1}, {88, 1}, {98, 1},
	{106, 1}, {108, 1}, {115, 1}, {138, 1}, {141, 2}, {152, 1}, {156, 2},
	{162, 1}, {170, 1}, {181, 1}, {186, 1}, {192, 1}, {199, 1}, {226, 2},
	{228, 1}, {229, 1}, {252, 1}, {257, 2}, {258, 2}, {259, 1}, {260, 1},
	{272, 1}, {280, 1}, {281, 1}, {283, 1}, {298, 1}, {312, 1}, {394, 1},
	{406, 1}, {429, 1}, {455, 1}, {468, 1}, {512, 1}, {513, 8}, {514, 5},
	{516, 1}, {530, 1}, {582, 1}, {676, 2}, {730, 1}, {733, 1}, {768, 1},
	{773, 1}, {794, 1}, {808, 1}, {900, 1}, {906, 1}, {934, 1}, {970, 1},
	{1004, 1}, {1034, 1}, {1084, 1}, {1107, 1}, {1114, 1}, {1134, 1},
	{1164, 1}, {1178, 1}, {1216, 1}, {1466, 1}, {1504, 1}, {1526, 1},
	{1714, 1}, {2120, 1}, {2212, 1}, {2226, 1}, {2700, 1}, {3106, 1},
};
static constexpr size_t alloc_size = 32;

int main(void)
{
	struct timespec gstart, gstop, start, stop;
	void *ibuf, *jbuf;
	size_t cnt_alloc = 0, cnt_more = 0;
	clock_gettime(CLOCK_MONOTONIC, &gstart);

	for (const auto &p : dist) {
		decltype(p.first) nchildren = p.first;
		decltype(p.second) noccr = p.second;

		cnt_alloc += noccr;
		cnt_more += noccr * nchildren;
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (decltype(noccr) i = 0; i < noccr; ++i) {
			MAPIAllocateBuffer(alloc_size, &ibuf);
			for (decltype(nchildren) j = 0; j < nchildren; ++j)
				MAPIAllocateMore(alloc_size, ibuf, &jbuf);
			MAPIFreeBuffer(ibuf);
		}
		clock_gettime(CLOCK_MONOTONIC, &stop);
		auto delta = std::chrono::seconds(stop.tv_sec) + std::chrono::nanoseconds(stop.tv_nsec) -
		             (std::chrono::seconds(start.tv_sec) + std::chrono::nanoseconds(start.tv_nsec));
		printf("O-%u× C-%u: %zu ns\n", noccr, nchildren, static_cast<size_t>(delta.count()));
	}
	clock_gettime(CLOCK_MONOTONIC, &gstop);
	auto delta = std::chrono::seconds(gstop.tv_sec) + std::chrono::nanoseconds(gstop.tv_nsec) -
	             (std::chrono::seconds(gstart.tv_sec) + std::chrono::nanoseconds(gstart.tv_nsec));
	printf("all %zu ns\n", static_cast<size_t>(delta.count()));

	printf("MAPIAllocateBuffer calls: %zu\n", cnt_alloc);
	printf("MAPIAllocateMore calls: %zu\n", cnt_more);
	return EXIT_SUCCESS;
}
