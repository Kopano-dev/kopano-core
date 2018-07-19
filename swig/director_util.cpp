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

static void make_key() {
	pthread_key_create(&g_key, NULL);	// We need cleanup here
}

static thread_info *get_thread_info() {
	thread_info *pti = NULL;
	
	pthread_once(&g_key_once, make_key);
	if ((pti = (thread_info *)pthread_getspecific(g_key)) == NULL) {
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
	thread_info *pti = NULL;
	
	pthread_once(&g_key_once, make_key);
	pti = (thread_info *)pthread_getspecific(g_key);

	return pti && pti->bCalledFromPython;
}
