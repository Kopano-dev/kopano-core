/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright 2019, Kopano and its licensors
 */
#include <set>
#include <string>
#include <vector>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <kopano/UnixUtil.h>
#include <kopano/scope.hpp>

using namespace std::string_literals;

static std::set<pid_t> se_term_pids, se_wait_pids;

static uint16_t next_free_port_2()
{
	auto fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		perror("nfp2: socket");
		return 0;
	}
	int y = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(y)) < 0) {
		perror("nfp2: setsockopt");
		return 0;
	}
	struct sockaddr_in6 sk{};
	sk.sin6_family = AF_INET6;
	sk.sin6_addr   = IN6ADDR_LOOPBACK_INIT;
	if (bind(fd, reinterpret_cast<const struct sockaddr *>(&sk), sizeof(sk)) < 0) {
		perror("nfp2: bind");
		return 0;
	}
	socklen_t slen = sizeof(sk);
	if (getsockname(fd, reinterpret_cast<struct sockaddr *>(&sk), &slen) < 0) {
		perror("nfp2: getsockname");
		return 0;
	}
	close(fd);
	/* It is not guaranteed to be free, but the chance is really high. */
	return ntohs(sk.sin6_port);
}

static uint16_t next_free_port()
{
	auto p = next_free_port_2();
	if (p == 0)
		abort();
	return p;
}

static pid_t launch(const char *const *args, bool autowait = true)
{
	auto pid = KC::unix_fork_function([](void *p) -> void * {
		auto args = static_cast<char **>(p);
		execvp(args[0], &args[0]);
		fprintf(stderr, "Failed to start %s: %s\n", args[0], strerror(errno));
		return nullptr;
	}, const_cast<char **>(args), 0, nullptr);
	if (pid == -1)
		return pid;
	fprintf(stderr, "Started %s (pid %u)\n", args[0], pid);
	se_term_pids.emplace(pid);
	if (autowait)
		se_wait_pids.emplace(pid);
	return pid;
}

static pid_t launch(const std::vector<const char *> &args, bool autoreap = true)
{
	if (args.size() == 0 || args[args.size()-1] != nullptr) {
		fprintf(stderr, "args not proper for launch()\n");
		return -1;
	}
	return launch(&args[0], autoreap);
}

static void terminate_children(int s)
{
	for (auto pid : se_term_pids) {
		fprintf(stderr, "Signal %u to PID %u\n", s, pid);
		kill(pid, s);
	}
	se_term_pids.clear();
}

static void wait_children()
{
	while (se_wait_pids.size() > 0) {
		fprintf(stderr, "Waiting for e.g. %u\n", *se_wait_pids.cbegin());
		auto pid = wait(nullptr);
		fprintf(stderr, "PID %u has ended\n", pid);
		se_wait_pids.erase(pid);
	}
}

int main(int argc, char **argv)
{
	char cwd[512];
	auto cwdret = getcwd(cwd, sizeof(cwd));
	if (cwdret == nullptr) {
		perror("getcwd");
		return EXIT_FAILURE;
	}

	setenv("MAPI_CONFIG_PATH", (cwdret + "/provider/client:"s + cwdret + "/provider/contacts").c_str(), true);
	mkdir("ts-attach", 0777);
	auto fp = fopen("ts-server.cfg", "w");
	fprintf(fp, "log_level=6\n");
	fprintf(fp, "run_as_user=\n");
	fprintf(fp, "run_as_group=\n");
	fprintf(fp, "local_admin_users=root %u\n", getuid());
	fprintf(fp, "pid_file=%s/ts-server.pid\n", cwd);
	fprintf(fp, "server_listen=[::1]:%u\n", next_free_port());
	fprintf(fp, "server_pipe_name=%s/ts-server.sock\n", cwd);
	fprintf(fp, "server_pipe_priority=%s/ts-prio.sock\n", cwd);
	fprintf(fp, "attachment_path=%s/ts-attach\n", cwd);
	fclose(fp);
	fp = fopen("ts-dagent.cfg", "w");
	fprintf(fp, "run_as_user=\n");
	fprintf(fp, "run_as_group=\n");
	fprintf(fp, "pid_file=%s/ts-dagent.pid\n", cwd);
	fprintf(fp, "lmtp_listen=[::1]:%u unix:%s/ts-dagent.sock\n", next_free_port(), cwd);
	fprintf(fp, "server_socket=file://%s/ts-server.sock\n", cwd);
	fclose(fp);
	fp = fopen("ts-spooler.cfg", "w");
	fprintf(fp, "run_as_user=\n");
	fprintf(fp, "run_as_group=\n");
	fprintf(fp, "pid_file=%s/ts-spooler.pid\n", cwd);
	fprintf(fp, "server_socket=file://%s/ts-server.sock\n", cwd);
	fclose(fp);

	struct sigaction act{};
	act.sa_handler = terminate_children;
	act.sa_flags   = SA_RESTART | SA_RESETHAND;
	sigaction(SIGINT, &act, nullptr);
	sigaction(SIGTERM, &act, nullptr);

	auto cleanup = KC::make_scope_success([]() {
		terminate_children(SIGTERM);
		wait_children();
	});
	if (!launch({"./kopano-server", "-c", "ts-server.cfg", nullptr}) ||
	    !launch({"./kopano-dagent", "-lc", "ts-dagent.cfg", nullptr}) ||
	    !launch({"./kopano-spooler", "-c", "ts-spooler.cfg", nullptr}))
		return EXIT_FAILURE;

	++argv;
	--argc;
	printf("Running subordinate program...\n");
	setenv("KOPANO_SOCKET", ("file://"s + cwd + "/ts-server.sock").c_str(), true);
	auto pid = argc == 0 ? launch({"/bin/sh", nullptr}, false) : launch(argv, false);
	int status;
	waitpid(pid, &status, 0);
	fprintf(stderr, "Reaped %u\n", pid);
	se_term_pids.erase(pid);
	se_wait_pids.erase(pid);
	printf("Leaving setupenv\n");
	return WIFEXITED(status) && status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
