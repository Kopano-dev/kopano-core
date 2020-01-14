/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2016+, Kopano and its licensors
 */
#include <chrono>
#include <memory>
#include <mutex>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <getopt.h>
#include <pthread.h>
#include <spawn.h>
#include <unistd.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/ECMemTable.h>
#include <kopano/ECRestriction.h>
#include <kopano/automapi.hpp>
#include <kopano/ecversion.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include <kopano/timeutil.hpp>
#include <kopano/IECInterfaces.hpp>
#ifdef HAVE_CURL_CURL_H
#	include <curl/curl.h>
#endif
#define NO_NOTIFY EC_PROFILE_FLAGS_NO_NOTIFICATIONS

using namespace KC;
using clk = std::chrono::steady_clock;
using duration = decltype(time_point()-time_point());

struct mpt_stat_entry {
	duration delta;
};

class mpt_job {
	public:
	virtual HRESULT init();
	virtual HRESULT run() = 0;
	private:
	AutoMAPI m_mapi;
};

static pthread_t mpt_ticker;
static bool mpt_ticker_quit;
static std::list<struct mpt_stat_entry> mpt_stat_list;
static duration mpt_stat_time;
static std::mutex mpt_stat_lock;
static const char *mpt_user, *mpt_pass, *mpt_socket;
static size_t mpt_repeat = ~0U;
static int mpt_loglevel = EC_LOGLEVEL_NOTICE;

static void *mpt_stat_dump(void *)
{
	for (; !mpt_ticker_quit; sleep(1)) {
		std::unique_lock<std::mutex> locker(mpt_stat_lock);
		auto dt = mpt_stat_time;
		size_t z = mpt_stat_list.size();
		if (z == 0)
			continue;
		locker.unlock();
		if (dt.count() == 0)
			continue;
		printf("\r\x1b\x5b""2K%.1f per second", z / std::chrono::duration_cast<std::chrono::duration<double>>(dt).count());
		fflush(stdout);
	}
	printf("\n");
	return nullptr;
}

static void mpt_stat_record(const duration &delta, size_t limit = 300)
{
	std::lock_guard<std::mutex> locker(mpt_stat_lock);
	mpt_stat_time += delta;
	mpt_stat_list.emplace_back(mpt_stat_entry{std::move(delta)});
	if (mpt_stat_list.size() > limit) {
		mpt_stat_time -= mpt_stat_list.cbegin()->delta;
		mpt_stat_list.pop_front();
	}
}

static int mpt_main_init(void)
{
	while (mpt_repeat-- > 0) {
		auto start = clk::now();
		HRESULT ret = MAPIInitialize(NULL);
		if (ret == erSuccess)
			MAPIUninitialize();
		auto stop = clk::now();
		mpt_stat_record(stop - start);
	}
	return EXIT_SUCCESS;
}

HRESULT mpt_job::init()
{
	auto ret = m_mapi.Initialize();
	if (ret != hrSuccess)
		return kc_perror("MAPIInitialize", ret);
	return hrSuccess;
}

static int mpt_basic_open(object_ptr<IMAPISession> &ses,
    object_ptr<IMsgStore> &store)
{
	auto ret = HrOpenECSession(&~ses, PROJECT_VERSION, "mapitime", mpt_user,
	           mpt_pass, mpt_socket, NO_NOTIFY, nullptr, nullptr);
	if (ret != hrSuccess) {
		fprintf(stderr, "Logon failed: %s\n", GetMAPIErrorMessage(ret));
		sleep(1);
		return ret;
	}
	ret = HrOpenDefaultStore(ses, &~store);
	if (ret != hrSuccess) {
		fprintf(stderr, "OpenDefaultStore: %s\n", GetMAPIErrorMessage(ret));
		sleep(1);
		return ret;
	}
	return hrSuccess;
}

static HRESULT mpt_basic_work(IMsgStore *store)
{
	memory_ptr<ENTRYID> eid;
	ULONG neid = 0;
	auto ret = store->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &neid, &~eid, nullptr);
	if (ret != hrSuccess)
		return kc_perror("GetReceiveFolder", ret);
	object_ptr<IMAPIFolder> folder;
	ULONG type = 0;
	ret = store->OpenEntry(0, nullptr, &iid_of(folder), MAPI_MODIFY, &type, &~folder);
	if (ret != hrSuccess)
		return kc_perror("OpenEntry", ret);
	return hrSuccess;
}

class mpt_open1 : public mpt_job {
	public:
	HRESULT run() override;
};

HRESULT mpt_open1::run()
{
	object_ptr<IMAPISession> ses;
	object_ptr<IMsgStore> store;
	auto ret = mpt_basic_open(ses, store);
	if (ret != hrSuccess)
		return kc_perror("mpt_basic_open", ret);
	return mpt_basic_work(store);
}

class mpt_open2 : public mpt_job {
	public:
	HRESULT init() override;
	HRESULT run() override;
	private:
	std::string m_data;
};

HRESULT mpt_open2::init()
{
	auto ret = mpt_job::init();
	if (ret != hrSuccess)
		return kc_perror("mpt_job::init", ret);
	object_ptr<IMAPISession> ses;
	object_ptr<IMsgStore> store;
	ret = mpt_basic_open(ses, store);
	if (ret != hrSuccess)
		return kc_perror("mpt_basic_open", ret);
	ret = kc_session_save(ses, m_data);
	if (ret != hrSuccess)
		return kc_perror("kc_session_save", ret);
	return hrSuccess;
}

HRESULT mpt_open2::run()
{
	object_ptr<IMAPISession> ses;
	auto ret = kc_session_restore(m_data, &~ses);
	if (ret != hrSuccess)
		return kc_perror("kc_session_restore", ret);
	object_ptr<IMsgStore> store;
	ret = HrOpenDefaultStore(ses, &~store);
	if (ret != hrSuccess)
		return kc_perror("OpenDefaultStore", ret);
	return mpt_basic_work(store);
}

class mpt_proplist : public mpt_job {
	public:
	HRESULT init() override;
	HRESULT run() override;

	protected:
	object_ptr<IMAPIFolder> m_inbox;
	rowset_ptr m_rows;
};

HRESULT mpt_proplist::init()
{
	auto ret = mpt_job::init();
	if (ret != hrSuccess)
		return kc_perror("mpt_job::init", ret);

	object_ptr<IMAPISession> ses;
	object_ptr<IMsgStore> store;
	ret = mpt_basic_open(ses, store);
	if (ret != hrSuccess)
		return kc_perror("mpt_basic_open", ret);

	memory_ptr<ENTRYID> eid;
	unsigned int neid = 0;
	ret = store->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &neid, &~eid, nullptr);
	if (ret != hrSuccess)
		return kc_perror("GetReceiveFolder", ret);
	unsigned int type = 0;
	ret = store->OpenEntry(neid, eid, &iid_of(m_inbox), MAPI_MODIFY, &type, &~m_inbox);
	if (ret != hrSuccess)
		return kc_perror("OpenEntry", ret);

	object_ptr<IMAPITable> tbl;
	ret = m_inbox->GetContentsTable(MAPI_UNICODE, &~tbl);
	if (ret != hrSuccess)
		return kc_perror("GetContentTable", ret);
	ret = tbl->QueryRows(-1, 0, &~m_rows);
	if (ret != hrSuccess)
		return kc_perror("QueryRows", ret);
	if (m_rows == nullptr || m_rows.size() == 0) {
		printf("You need at least one message in inbox.\n");
		return MAPI_E_NOT_FOUND;
	}
	return hrSuccess;
}

HRESULT mpt_proplist::run()
{
	unsigned int type = 0;
	object_ptr<IMessage> msg;

	for (unsigned int i = 0; i < m_rows.size(); ++i) {
		auto prop = m_rows[i].find(PR_ENTRYID);
		if (prop == nullptr)
			continue;
		auto ret = m_inbox->OpenEntry(prop->Value.bin.cb,
		           reinterpret_cast<const ENTRYID *>(prop->Value.bin.lpb),
		           &iid_of(msg), MAPI_MODIFY, &type, &~msg);
		if (ret != hrSuccess)
			return kc_perror("OpenEntry", ret);
		memory_ptr<SPropTagArray> spta;
		ret = msg->GetPropList(MAPI_UNICODE, &~spta);
		if (ret != hrSuccess)
			return kc_perror("GetPropList", ret);
	}
	return hrSuccess;
}

class mpt_proplist1 : public mpt_proplist {
	public:
	HRESULT init() override;
	HRESULT run() override;

	protected:
	object_ptr<IMessage> m_msg;
};

HRESULT mpt_proplist1::init()
{
	auto ret = mpt_proplist::init();
	if (ret != hrSuccess)
		return kc_perror("mpt_proplist::init", ret);
	auto prop = m_rows[0].find(PR_ENTRYID);
	if (prop == nullptr)
		return kc_perror("PR_ENTRYID", MAPI_E_NOT_FOUND);
	unsigned int type = 0;
	ret = m_inbox->OpenEntry(prop->Value.bin.cb,
	      reinterpret_cast<const ENTRYID *>(prop->Value.bin.lpb),
	      &iid_of(m_msg), MAPI_MODIFY, &type, &~m_msg);
	if (ret != hrSuccess)
		return kc_perror("OpenEntry", ret);
	return hrSuccess;
}

HRESULT mpt_proplist1::run()
{
	memory_ptr<SPropTagArray> spta;
	auto ret = m_msg->GetPropList(MAPI_UNICODE, &~spta);
	if (ret != hrSuccess)
		return kc_perror("GetPropList", ret);
	return hrSuccess;
}

class mpt_search final : public mpt_job {
	public:
	HRESULT init() override;
	HRESULT run() override;

	private:
	object_ptr<IMAPIFolder> m_findroot, m_find;
	memory_ptr<ENTRYLIST> m_scanfld;
	memory_ptr<SRestriction> m_rst;
};

HRESULT mpt_search::init()
{
	auto ret = mpt_job::init();
	if (ret != hrSuccess)
		return kc_perror("mpt_job::init", ret);
	object_ptr<IMAPISession> ses;
	object_ptr<IMsgStore> store;
	ret = mpt_basic_open(ses, store);
	if (ret != hrSuccess)
		return kc_perror("mpt_basic_open", ret);

	/* Set up INBOX for scanning */
	unsigned int inbox_sz = 0;
	memory_ptr<ENTRYID> inbox;
	ret = store->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &inbox_sz, &~inbox, nullptr);
	if (ret != hrSuccess)
		return kc_perror("GetReceiveFolder", ret);
	fprintf(stderr, "inbox    %s\n", bin2hex(inbox_sz, inbox).c_str());
	object_ptr<IMAPIFolder> fld;
	unsigned int objtype;
	ret = store->OpenEntry(inbox_sz, inbox, &IID_IMAPIFolder, MAPI_MODIFY, &objtype, &~fld);
	if (ret != hrSuccess)
		return kc_perror("OpenEntry", ret);
	ret = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~m_scanfld);
	if (ret != hrSuccess)
		return kc_perrorf("malloc", ret);
	m_scanfld->cValues = 1;
	ret = MAPIAllocateMore(sizeof(SBinary) * 1, m_scanfld, reinterpret_cast<void **>(&m_scanfld->lpbin));
	if (ret != hrSuccess)
		return kc_perrorf("malloc", ret);
	m_scanfld->lpbin[0].cb = 0;
	m_scanfld->lpbin[0].lpb = nullptr;
	ret = MAPIAllocateMore(inbox_sz, m_scanfld, reinterpret_cast<void **>(&m_scanfld->lpbin[0].lpb));
	if (ret != hrSuccess)
		return kc_perrorf("malloc", ret);
	m_scanfld->lpbin[0].cb = inbox_sz;
	memcpy(m_scanfld->lpbin[0].lpb, inbox.get(), inbox_sz);

	/* Create search folder */
	memory_ptr<SPropValue> pv;
	ret = HrGetOneProp(store, PR_FINDER_ENTRYID, &~pv);
	if (ret != hrSuccess)
		return kc_perrorf("FINDER_ENTRYID", ret);
	fprintf(stderr, "findroot %s\n", bin2hex(pv->Value.bin.cb, pv->Value.bin.lpb).c_str());
	ret = ses->OpenEntry(pv->Value.bin.cb, reinterpret_cast<ENTRYID *>(pv->Value.bin.lpb), &IID_IMAPIFolder, MAPI_MODIFY, &objtype, &~m_findroot);
	if (ret != hrSuccess)
		return kc_perrorf("OpenEntry", ret);

	char name[32];
	snprintf(name, sizeof(name), "sf0x%x", rand());
	printf(">> %s\n", name);
	ret = m_findroot->CreateFolder(FOLDER_SEARCH, reinterpret_cast<const TCHAR *>(name), reinterpret_cast<const TCHAR *>(L""), &IID_IMAPIFolder, 0, &~m_find);
	if (ret != hrSuccess)
		return kc_perrorf("CreateFolder", ret);
	return hrSuccess;
}

HRESULT mpt_search::run()
{
	/* Do it like WebApp does */
	SPropValue cls[7], spv[] = {
		{PR_SUBJECT}, {PR_SENDER_NAME}, {PR_SENDER_EMAIL_ADDRESS},
		{PR_SENT_REPRESENTING_NAME},
		{PR_SENT_REPRESENTING_EMAIL_ADDRESS}, {PR_BODY},
		{PR_DISPLAY_TO}, {PR_DISPLAY_NAME},
		{PR_CALLBACK_TELEPHONE_NUMBER}, {PR_GENERATION},
		{PR_BUSINESS_TELEPHONE_NUMBER}, {PR_HOME_TELEPHONE_NUMBER},
		{PR_COMPANY_NAME}, {PR_PRIMARY_TELEPHONE_NUMBER},
		{PR_BUSINESS2_TELEPHONE_NUMBER}, {PR_MOBILE_TELEPHONE_NUMBER},
		{PR_RADIO_TELEPHONE_NUMBER}, {PR_CAR_TELEPHONE_NUMBER},
		{PR_OTHER_TELEPHONE_NUMBER}, {PR_PAGER_TELEPHONE_NUMBER},
		{PR_PRIMARY_FAX_NUMBER}, {PR_BUSINESS_FAX_NUMBER},
		{PR_HOME_FAX_NUMBER}, {PR_TELEX_NUMBER},
		{PR_ASSISTANT_TELEPHONE_NUMBER}, {PR_HOME2_TELEPHONE_NUMBER},
		{PR_DISPLAY_NAME}, {PR_TTYTDD_PHONE_NUMBER},
		{PR_COMPANY_MAIN_PHONE_NUMBER}, {PR_DISPLAY_CC}
	};
	for (size_t i = 0; i < ARRAY_SIZE(spv); ++i)
		spv[i].Value.lpszW = const_cast<wchar_t *>(L"dreck");
	for (size_t i = 0; i < ARRAY_SIZE(cls); ++i)
		cls[i].ulPropTag = PR_MESSAGE_CLASS;
	cls[0].Value.lpszW = const_cast<wchar_t *>(L"IPM.Note");
	cls[1].Value.lpszW = const_cast<wchar_t *>(L"IPM.Appointment");
	cls[2].Value.lpszW = const_cast<wchar_t *>(L"IPM.Schedule");
	cls[3].Value.lpszW = const_cast<wchar_t *>(L"IPM.Contact");
	cls[4].Value.lpszW = const_cast<wchar_t *>(L"IPM.DistList");
	cls[5].Value.lpszW = const_cast<wchar_t *>(L"IPM.Task");
	cls[6].Value.lpszW = const_cast<wchar_t *>(L"IPM.StickyNote");
	auto ret = ECAndRestriction(ECOrRestriction(
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_SUBJECT, &spv[0], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_SENDER_NAME, &spv[1], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_SENDER_EMAIL_ADDRESS, &spv[2], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_SENT_REPRESENTING_NAME, &spv[3], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_SENT_REPRESENTING_EMAIL_ADDRESS, &spv[4], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_BODY, &spv[5], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_DISPLAY_TO, &spv[6], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_DISPLAY_NAME, &spv[7], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_CALLBACK_TELEPHONE_NUMBER, &spv[8], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_GENERATION, &spv[9], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_BUSINESS_TELEPHONE_NUMBER, &spv[10], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_HOME_TELEPHONE_NUMBER, &spv[11], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_COMPANY_NAME, &spv[12], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_PRIMARY_TELEPHONE_NUMBER, &spv[13], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_BUSINESS2_TELEPHONE_NUMBER, &spv[14], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_MOBILE_TELEPHONE_NUMBER, &spv[15], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_RADIO_TELEPHONE_NUMBER, &spv[16], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_CAR_TELEPHONE_NUMBER, &spv[17], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_OTHER_TELEPHONE_NUMBER, &spv[18], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_PAGER_TELEPHONE_NUMBER, &spv[19], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_PRIMARY_FAX_NUMBER, &spv[20], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_BUSINESS_FAX_NUMBER, &spv[21], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_HOME_FAX_NUMBER, &spv[22], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_TELEX_NUMBER, &spv[23], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_ASSISTANT_TELEPHONE_NUMBER, &spv[24], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_HOME2_TELEPHONE_NUMBER, &spv[25], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_DISPLAY_NAME, &spv[26], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_TTYTDD_PHONE_NUMBER, &spv[27], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_COMPANY_MAIN_PHONE_NUMBER, &spv[28], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_SUBSTRING, PR_DISPLAY_CC, &spv[29], ECRestriction::Cheap)
	) + ECOrRestriction(
		ECContentRestriction(FL_IGNORECASE | FL_PREFIX, PR_MESSAGE_CLASS, &cls[0], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_PREFIX, PR_MESSAGE_CLASS, &cls[1], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_PREFIX, PR_MESSAGE_CLASS, &cls[2], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_PREFIX, PR_MESSAGE_CLASS, &cls[3], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_PREFIX, PR_MESSAGE_CLASS, &cls[4], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_PREFIX, PR_MESSAGE_CLASS, &cls[5], ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE | FL_PREFIX, PR_MESSAGE_CLASS, &cls[6], ECRestriction::Cheap)
	))
	.CreateMAPIRestriction(&~m_rst, ECRestriction::Cheap);
	if (ret != hrSuccess)
		return kc_perrorf("CreateMAPIRestriction", ret);
	ret = m_find->SetSearchCriteria(m_rst, m_scanfld, RECURSIVE_SEARCH | RESTART_SEARCH);
	if (ret != hrSuccess)
		return kc_perrorf("SetSearchCriteria", ret);
	return hrSuccess;
}

static int mpt_runner(mpt_job &&fct)
{
	auto ret = fct.init();
	if (ret != hrSuccess) {
		kc_perror("mpt_job::init", ret);
		return EXIT_FAILURE;
	}
	while (mpt_repeat-- > 0) {
		auto start = clk::now();
		ret = fct.run();
		if (ret != hrSuccess) {
			kc_perror("mpt_job::run", ret);
			return EXIT_FAILURE;
		}
		auto stop = clk::now();
		mpt_stat_record(stop - start);
	}
	return EXIT_SUCCESS;
}

static int mpt_main_pagetime(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Need URL to test\n");
		return EXIT_FAILURE;
	}
#ifndef HAVE_CURL_CURL_H
	fprintf(stderr, "Not built with curl support\n");
	return EXIT_FAILURE;
#else
	auto curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, true);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, true);
	curl_easy_setopt(curl, CURLOPT_URL, argv[1]);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, static_cast<curl_write_callback>([](char *, size_t, size_t n, void *) { return n; }));
	while (mpt_repeat-- > 0) {
		auto start = clk::now();
		curl_easy_perform(curl);
		auto stop = clk::now();
		mpt_stat_record(stop - start);
	}
	return EXIT_SUCCESS;
#endif
}

static int mpt_main_exectime(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Need program to test\n");
		return EXIT_FAILURE;
	}
	--argc;
	++argv; // skip "exectime"
	while (mpt_repeat-- > 0) {
		pid_t pid;
		int st;

		auto start = clk::now();
		if (posix_spawn(&pid, argv[0], nullptr, nullptr, const_cast<char **>(argv), nullptr) == 0)
			wait(&st);
		auto stop = clk::now();
		mpt_stat_record(stop - start);
	}
	return EXIT_SUCCESS;
}

static int mpt_main_cast(bool which)
{
	AutoMAPI mapiinit;
	HRESULT ret = mapiinit.Initialize();
	if (ret != hrSuccess) {
		perror("MAPIInitialize");
		return EXIT_FAILURE;
	}
	object_ptr<IProfAdmin> profadm;
	ret = MAPIAdminProfiles(0, &~profadm);
	if (ret != hrSuccess)
		return EXIT_FAILURE;
	object_ptr<IUnknown> unk;
	ret = profadm->QueryInterface(IID_IUnknown, &~unk);
	if (ret != hrSuccess)
		return EXIT_FAILURE;

	if (which == 0) { /* qicast */
		while (mpt_repeat-- > 0) {
			unsigned int rep = 100000;
			auto start = clk::now();
			while (rep-- > 0)
				unk->QueryInterface(IID_IProfAdmin, &~profadm);
			auto stop = clk::now();
			mpt_stat_record(stop - start);
		}
	} else if (which == 1) { /* dycast */
		while (mpt_repeat-- > 0) {
			unsigned int rep = 100000;
			auto start = clk::now();
			while (rep-- > 0)
				profadm.reset(dynamic_cast<IProfAdmin *>(unk.get()));
			auto stop = clk::now();
			mpt_stat_record(stop - start);
		}
	}
	return EXIT_SUCCESS;
}

static int mpt_main_malloc(void)
{
	while (mpt_repeat-- > 0) {
		auto start = clk::now();
		memory_ptr<MAPIUID> base;
		auto ret = MAPIAllocateBuffer(sizeof(MAPIUID), &~base);
		if (ret != hrSuccess)
			return EXIT_FAILURE;
		for (unsigned int i = 0; i < 10000; ++i) {
			void *x = nullptr;
			ret = MAPIAllocateMore(sizeof(MAPIUID), base.get(), &x);
			if (ret != hrSuccess)
				return EXIT_FAILURE;
		}
		base.reset();
		auto stop = clk::now();
		mpt_stat_record(stop - start);
	}
	return EXIT_SUCCESS;
}

static int mpt_main_bin2hex()
{
	static constexpr const size_t bufsize = 1048576;
	auto temp = std::make_unique<char[]>(bufsize);
	memset(temp.get(), 0, bufsize);
	while (mpt_repeat-- > 0) {
		auto start = clk::now();
		bin2hex(bufsize, reinterpret_cast<const unsigned char *>(temp.get()));
		auto stop = clk::now();
		mpt_stat_record(stop - start);
	}
	return EXIT_SUCCESS;
}

static void mpt_usage(void)
{
	fprintf(stderr, "mapitime [-p pass] [-s server] [-u username] [-z count] benchmark_choice\n");
	fprintf(stderr, "  -z count    Run this many iterations (default: finite but almost forever)\n");
	fprintf(stderr, "Benchmark choices:\n");
	fprintf(stderr, "  init        Just the library initialization\n");
	fprintf(stderr, "  open1       Measure: init, login, open store, open root container\n");
	fprintf(stderr, "  open2       Like open1, but use Save-Restore\n");
	fprintf(stderr, "  proplist    Measure GetPropList over inbox\n");
	fprintf(stderr, "  proplist1   Measure IMessage::GetPropList over first message\n");
	fprintf(stderr, "  pagetime    Measure webpage retrieval time\n");
	fprintf(stderr, "  exectime    Measure process runtime\n");
	fprintf(stderr, "  qicast      Measure QueryInterface throughput\n");
	fprintf(stderr, "  dycast      Measure dynamic_cast<> throughput\n");
	fprintf(stderr, "  malloc      Measure MAPIAllocateMore throughput\n");
	fprintf(stderr, "  bin2hex     Measure bin2hex throughput\n");
}

static int mpt_option_parse(int argc, char **argv)
{
	int c;
	if (argc < 2) {
		mpt_usage();
		return EXIT_FAILURE;
	}
	while ((c = getopt(argc, argv, "p:s:u:vz:")) != -1) {
		if (c == 'p') {
			mpt_pass = optarg;
		} else if (c == 'u') {
			mpt_user = optarg;
		} else if (c == 's') {
			mpt_socket = optarg;
		} else if (c == 'v') {
			if (mpt_loglevel <= EC_LOGLEVEL_DEBUG)
			++mpt_loglevel;
		} else if (c == 'z') {
			mpt_repeat = strtoul(optarg, NULL, 0);
		} else {
			fprintf(stderr, "Error: unknown option -%c\n", c);
			mpt_usage();
		}
	}
	ec_log_get()->SetLoglevel(mpt_loglevel);
	if (mpt_user == nullptr) {
		mpt_user = "foo";
		fprintf(stderr, "Info: defaulting to username \"foo\"\n");
	}
	if (mpt_pass == nullptr) {
		mpt_pass = "xfoo";
		fprintf(stderr, "Info: defaulting to password \"xfoo\"\n");
	}
	if (mpt_socket == NULL) {
		mpt_socket = "http://localhost:236/";
		fprintf(stderr, "Info: defaulting to %s\n", mpt_socket);
	}
	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	int ret = mpt_option_parse(argc, argv);
	if (ret != EXIT_SUCCESS)
		return ret;
	argc -= optind - 1;
	argv += optind - 1;
	if (argc < 2) {
		mpt_usage();
		return EXIT_FAILURE;
	}

	ret = pthread_create(&mpt_ticker, nullptr, mpt_stat_dump, nullptr);
	if (ret != 0) {
		perror("pthread_create");
		return EXIT_FAILURE;
	}
	ret = EXIT_FAILURE;
	if (strcmp(argv[1], "init") == 0 || strcmp(argv[1], "i") == 0)
		ret = mpt_main_init();
	else if (strcmp(argv[1], "open1") == 0)
		ret = mpt_runner(mpt_open1());
	else if (strcmp(argv[1], "open2") == 0)
		ret = mpt_runner(mpt_open2());
	else if (strcmp(argv[1], "proplist") == 0)
		ret = mpt_runner(mpt_proplist());
	else if (strcmp(argv[1], "proplist1") == 0)
		ret = mpt_runner(mpt_proplist1());
	else if (strcmp(argv[1], "exectime") == 0)
		ret = mpt_main_exectime(argc - 1, argv + 1);
	else if (strcmp(argv[1], "pagetime") == 0)
		ret = mpt_main_pagetime(argc - 1, argv + 1);
	else if (strcmp(argv[1], "qicast") == 0)
		ret = mpt_main_cast(0);
	else if (strcmp(argv[1], "dycast") == 0)
		ret = mpt_main_cast(1);
	else if (strcmp(argv[1], "malloc") == 0)
		ret = mpt_main_malloc();
	else if (strcmp(argv[1], "bin2hex") == 0)
		ret = mpt_main_bin2hex();
	else if (strcmp(argv[1], "search") == 0)
		ret = mpt_runner(mpt_search());
	else
		mpt_usage();
	mpt_ticker_quit = true;
	pthread_join(mpt_ticker, nullptr);
	return ret;
}
