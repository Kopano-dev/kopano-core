#pragma once
#include <string>
#include <ctime>
#include <kopano/timeutil.hpp>

namespace KC {

struct request_stat {
	/* Walltime when socket event happened */
	time_point sk_wall_start{}, sk_wall_end{};
	time_duration sk_wall_dur{};
	/* Walltime when enqueue happened */
	time_point enq_wall_start{}, enq_wall_end{};
	time_duration enq_wall_dur{};
	/* Workitem processing time (SSL setup, SOAP receive/send & rh1) (start/stop) */
	time_point wi_wall_start{}, wi_wall_end{};
	time_duration wi_wall_dur{};
	struct timespec wi_cpu[3]{};
	/* KCmdService:: request handler time (ECSession lookup & rh2) (start/stop/delta) */
	time_point rh1_wall_start{}, rh1_wall_end{};
	time_duration rh1_wall_dur{};
	struct timespec rh1_cpu[3]{};
	/* KCmdService::<lambda> time (start/stop/delta) */
	time_point rh2_wall_start, rh2_wall_end;
	time_duration rh2_wall_dur{};
	struct timespec rh2_cpu[3]{};

	std::string user, imp, agent;
	const char *func = nullptr;
	int er = 0;
};

} /* namespace */
