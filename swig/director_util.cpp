/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <pthread.h>

static pthread_key_t	g_key;
static pthread_once_t	g_key_once = PTHREAD_ONCE_INIT;

struct thread_info {
	bool bCalledFromPython;
};

void destructor(void *t) {
	delete static_cast<thread_info *>(t);
}

static void make_key() {
	pthread_key_create(&g_key, destructor);
}

static thread_info *get_thread_info() {
	pthread_once(&g_key_once, make_key);
	auto pti = static_cast<thread_info *>(pthread_getspecific(g_key));
	if (pti == nullptr) {
		pti = new thread_info;
		pthread_setspecific(g_key, (void *)pti);
	}
	return pti;
}

void mark_call_from_python() {
	thread_info *pti = get_thread_info();
	pti->bCalledFromPython = true;
}

void unmark_call_from_python() {
	thread_info *pti = get_thread_info();
	pti->bCalledFromPython = false;
}

bool check_call_from_python() {
	pthread_once(&g_key_once, make_key);
	auto pti = static_cast<thread_info *>(pthread_getspecific(g_key));
	return pti && pti->bCalledFromPython;
}
