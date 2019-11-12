/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright 2019, Kopano and its licensors
 */
#include <string>
#include <cstdio>
#include <cstdlib>
#include <mapidefs.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECGuid.h>
#include <kopano/MAPIErrors.h>
#include <kopano/mapiguidext.h>
#include <kopano/platform.h>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include "kcore.hpp"

#if __cplusplus >= 201700L
using std::string_view;
#else
using string_view = std::string;
#endif

using namespace KC;

struct ms_muidwrap {
	char flags[4];
	GUID provider_uid;
	uint8_t version, flag;
	char dll_name[14];
	uint32_t wrapped_flags;
	GUID wrapped_provider_uid;
	uint32_t wrapped_type;
	const char *server_short_name() const { return reinterpret_cast<const char *>(this) + offsetof(ms_muidwrap, wrapped_type) + sizeof(wrapped_type); }
	const char *mailbox_dn() const { auto p = server_short_name(); return p + strlen(p) + 1; }
	const char *entryid_v2() const { auto p = mailbox_dn(); return p + strlen(p) + 1; }
};

struct kc_muidwrap {
	char flags[4]{};
	GUID provider_uid{muidStoreWrap};
	uint8_t version, flag;
	const char *dll_name() const { return reinterpret_cast<const char *>(this) + offsetof(kc_muidwrap, flag) + sizeof(flag); }
	const char *original_entryid() const { return dll_name() + strlen(dll_name()) + 1; }
};

static void try_kcwrap(const string_view &s, unsigned int i);

KC_DEFINE_GUID(MUIDEMSAB,
0xc840a7dc, 0x42c0, 0x1a10, 0xb4, 0xb9, 0x08, 0x00, 0x2b, 0x2f, 0xe1, 0x82);
#define OOP_DONT_LOOKUP 0x10000000

static constexpr unsigned int mkind(unsigned int level)
{
	return 4 * level;
}

static constexpr const char *mapitype_str(unsigned int x)
{
#define E(s) case s: return " <" #s ">"
	switch (x) {
	E(MAPI_STORE);
	E(MAPI_ADDRBOOK);
	E(MAPI_FOLDER);
	E(MAPI_ABCONT);
	E(MAPI_MESSAGE);
	E(MAPI_MAILUSER);
	E(MAPI_ATTACH);
	E(MAPI_DISTLIST);
	E(MAPI_PROFSECT);
	default: return "";
#undef E
	}
}

static void dump_guid(const GUID &g)
{
	printf("{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
		le32_to_cpu(g.Data1), le32_to_cpu(g.Data2),
		le32_to_cpu(g.Data3), g.Data4[0], g.Data4[1], g.Data4[2],
		g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}

static void dump_guid_withvar(const GUID &g)
{
	dump_guid(g);
#define F(p, v) if (memcmp(&g, (p v), sizeof(g)) == 0) printf(" <" #v ">");
	F(&, muidStoreWrap);
	F(&, MUIDEMSAB);
	F(&, MUIDECSAB);
	F(&, MUIDZCSAB);
	F(, MUIDECSI_SERVER);
	F(&, KOPANO_SERVICE_GUID);
	F(&, KOPANO_STORE_DELEGATE_GUID);
	F(&, KOPANO_STORE_ARCHIVE_GUID);
	F(, STATIC_GUID_PUBLICFOLDER);
	F(, STATIC_GUID_FAVORITE);
	F(, STATIC_GUID_FAVSUBTREE);
	F(&, pbGlobalProfileSectionGuid);
	F(&, PS_EC_IMAP);
#undef F
}

static void try_guid(const string_view &s, unsigned int i)
{
	if (s.size() != sizeof(GUID)) {
		printf("%-*sNot a GUID (not 16 bytes)\n", mkind(i), "");
		return;
	}
	GUID g;
	memcpy(&g, s.data(), sizeof(g));
	printf("%-*sPossible GUID: ", mkind(i), "");
	dump_guid_withvar(g);
	printf("\n");
}

static void try_af1(const string_view &s, unsigned int i)
{
	if (s.size() < 4) {
		printf("%-*sNot AF1 form: shorter than 4 bytes\n", mkind(i), "");
		return;
	}
	printf("%-*sFulfills AF1 form (at least 4 bytes)\n", mkind(i), "");
	printf("%-*sFlags:", mkind(i + 1), "");
	if (s[0] & MAPI_SHORTTERM)
		printf(" MAPI_SHORTTERM");
	if (s[0] & MAPI_NOTRECIP)
		printf(" MAPI_NOTRECIP");
	if (s[0] & MAPI_THISSESSION)
		printf(" MAPI_THISSESSION");
	if (s[0] & MAPI_NOW)
		printf(" MAPI_NOW");
	if (s[0] & MAPI_NOTRESERVED)
		printf(" MAPI_NOTRESERVED");
	if (s[1] & MAPI_COMPOUND)
		printf(" MAPI_COMPOUND");
	printf("\n");
}

static void try_af2(const string_view &s, unsigned int i)
{
	if (s.size() >= 4 + sizeof(GUID)) {
		printf("%-*sFulfills AF2 form (at least 20 bytes)\n", mkind(i), "");
		printf("%-*sPossible provider: ", mkind(i + 1), "");
		GUID g;
		memcpy(&g, s.data() + 4, sizeof(g));
		dump_guid_withvar(g);
		printf("\n");
	} else {
		printf("%-*sNot AF2 form: shorter than 20 bytes\n", mkind(i), "");
	}
}

static void try_eidv1(const string_view &s, unsigned int i)
{
	if (s.size() < sizeof(EID)) {
		printf("%-*sNot a ZCP/KC EID v1: have %zu bytes, expected at least %zu bytes\n",
		       mkind(i), "", s.size(), sizeof(EID));
		return;
	}
	auto eid = reinterpret_cast<const EID *>(s.data());
	if (get_unaligned_le32(&eid->ulVersion) != 1) {
		printf("%-*sNot a ZCP/KC EID v1: version field shows not version 1\n", mkind(i), "");
		return;
	}
	printf("%-*sPossible ZCP/KC EID v1:\n", mkind(i), "");
	++i;
	int rdsize = s.size() - (eid->szServer - reinterpret_cast<const char *>(eid));
	if (rdsize > 0 && *eid->szServer != '\0')
		printf("%-*sRedirect URL: \"%.*s\"\n", mkind(i), "", rdsize, eid->szServer);
	printf("%-*sStore GUID: ", mkind(i), "");
	dump_guid(eid->guid);
	printf(" b:%s\n", bin2hex(sizeof(eid->guid), &eid->guid).c_str());
	auto type = get_unaligned_le16(&eid->usType);
	printf("%-*sObject type: %u%s\n", mkind(i), "", type, mapitype_str(type));
	printf("%-*sObject GUID: ", mkind(i), "");
	dump_guid(eid->uniqueId);
	printf(" b:%s\n", bin2hex(sizeof(eid->uniqueId), &eid->uniqueId).c_str());
}

static void try_eidv0(const string_view &s, unsigned int i)
{
	if (s.size() < sizeof(EID_V0)) {
		printf("%-*sNot a ZCP/KC EID v0: have %zu bytes, expected at least %zu bytes\n",
		       mkind(i), "", s.size(), sizeof(EID_V0));
		return;
	}
	auto eid = reinterpret_cast<const EID_V0 *>(s.data());
	if (get_unaligned_le32(&eid->ulVersion) != 0) {
		printf("%-*sNot a ZCP/KC EID v0: version field shows not version 0\n", mkind(i), "");
		return;
	}
	printf("%-*sPossible ZCP/KC EID v0:\n", mkind(i), "");
	++i;
	int rdsize = s.data() - eid->szServer;
	if (rdsize > 0 && *eid->szServer != '\0')
		printf("%-*sRedirect URL: %.*s", mkind(i), "", rdsize, eid->szServer);
	printf("%-*sStore GUID: ", mkind(i), "");
	dump_guid(eid->guid);
	printf(" b:%s\n", bin2hex(sizeof(eid->guid), &eid->guid).c_str());
	auto type = get_unaligned_le16(&eid->usType);
	printf("%-*sObject type: %u%s\n", mkind(i), "", type, mapitype_str(type));
	printf("%-*sObject id: %u\n", mkind(i), "", get_unaligned_le32(&eid->ulId));
}

static void try_abeid(const string_view &s, unsigned int i)
{
	if (s.size() < sizeof(ABEID_FIXED)) {
		printf("%-*sNot a ZCP/KC ABEID: have %zu bytes, expected at least %zu bytes\n",
		       mkind(i), "", s.size(), sizeof(ABEID_FIXED));
		return;
	}
	auto eid = reinterpret_cast<const ABEID *>(s.data());
	if (get_unaligned_le32(&eid->ulVersion) != 1 &&
	    get_unaligned_le32(&eid->ulVersion) != 0) {
		printf("%-*sNot a ZCP/KC ABEID: version field shows not version 0/1\n", mkind(i), "");
		return;
	}
	if (memcmp(&eid->guid, &MUIDECSAB, sizeof(MUIDECSAB)) != 0 &&
	    memcmp(&eid->guid, &MUIDZCSAB, sizeof(MUIDZCSAB)) != 0) {
		printf("%-*sNot a ZCP/KC ABEID v1: unrecognized provider ", mkind(i), "");
		dump_guid_withvar(eid->guid);
		printf("\n");
		return;
	}
	printf("%-*sPossible ZCP/KC ABEID v1:\n", mkind(i), "");
	++i;
	printf("%-*sProvider: ", mkind(i), "");
	dump_guid_withvar(eid->guid);
	printf("\n");
	auto type = get_unaligned_le32(&eid->ulType);
	printf("%-*sObject type: %u%s\n", mkind(i), "", type, mapitype_str(type));
	printf("%-*sObject id: %u\n", mkind(i), "", get_unaligned_le32(&eid->ulId));
	int xtsize = s.data() - eid->szExId;
	if (xtsize > 0)
		printf("%-*sExtern id: b:%s\n", mkind(i), "", bin2hex(xtsize, eid->szExId).c_str());
}

static void try_emsab(const string_view &s, unsigned int i)
{
	struct emsabid {
		char flags[4];
		GUID guid;
		uint32_t edkver, type;
		char dn[];
	};
	if (s.size() < sizeof(emsabid)) {
		printf("%-*sNot a EMSAB: have %zu bytes, expected at least %zu\n",
		       mkind(i), "", s.size(), sizeof(emsabid));
		return;
	}
	auto eid = reinterpret_cast<const emsabid *>(s.data());
	if (memcmp(&eid->guid, &MUIDEMSAB, sizeof(MUIDEMSAB)) != 0) {
		printf("%-*sNot a EMSABID: unrecognized provider ", mkind(i), "");
		dump_guid_withvar(eid->guid);
		printf("\n");
		return;
	}
	printf("%-*sExchange Address Entry ID:\n", mkind(i), "");
	++i;
	printf("%-*sVersion: %u\n", mkind(i), "", get_unaligned_le32(&eid->edkver));
	printf("%-*sType: %u\n", mkind(i), "", get_unaligned_le32(&eid->type));
	printf("%-*sX500DN: %.*s\n", mkind(i), "", static_cast<int>(s.size() - sizeof(emsabid)), eid->dn);
}

static void try_oneoff(const string_view &s, unsigned int i)
{
	struct oopid {
		char abflags[4];
		GUID guid;
		uint16_t version;
		/*
		 * MS-OXCDATA §2.2.5.1 specifies flags as 2 8-bit fields.
		 * That only helps in the sense that MS-OXCDATA need not
		 * bother with documenting endianess.
		 */
		uint16_t flags;
		/* char address_type[], emailaddress[]; */
	};
	if (s.size() < sizeof(oopid)) {
		printf("%-*sNot a One Off EntryID: have %zu bytes, expected at least %zu\n",
		       mkind(i), "", s.size(), sizeof(oopid));
		return;
	}
	auto eid = reinterpret_cast<const oopid *>(s.data());
	if (memcmp(&eid->guid, &MUIDOOP, sizeof(MUIDOOP)) != 0) {
		printf("%-*sNot a One-Off EntryID: unrecognized provider ", mkind(i), "");
		dump_guid_withvar(eid->guid);
		printf("\n");
		return;
	}
	printf("%-*sOne-Off EntryID:\n", mkind(i), "");
	++i;
	printf("%-*sVersion: %u\n", mkind(i), "", get_unaligned_le16(&eid->version));
	printf("%-*sFlags:", mkind(i), "");
	auto flags = get_unaligned_le16(&eid->flags);
	if (flags & MAPI_ONE_OFF_UNICODE)
		printf(" MAPI_ONE_OFF_UNICODE");
	if (flags & MAPI_ONE_OFF_NOLOOKUP)
		printf(" OOP_DONT_LOOKUP");
	switch (flags & MAPI_ONE_OFF_MAEMASK) {
	case MAPI_ONE_OFF_BINHEX: printf(" BinHex"); break;
	case MAPI_ONE_OFF_UUENCODE: printf(" UUENCODE"); break;
	case MAPI_ONE_OFF_APPLESINGLE: printf(" AppleSingle"); break;
	case MAPI_ONE_OFF_APPLEDOUBLE: printf(" AppleDouble"); break;
	}
	switch (flags & MAPI_ONE_OFF_FMTMASK) {
	case MAPI_ONE_OFF_TEXTONLY: printf(" TextOnly"); break;
	case MAPI_ONE_OFF_HTMLONLY: printf(" HTMLOnly"); break;
	case MAPI_ONE_OFF_TEXTANDHTML: printf(" TextAndHTML"); break;
	}
	if (flags & MAPI_ONE_OFF_NO_RICH_INFO)
		printf(" MAPI_SEND_NO_RICH_INFO");
	printf(" (use %s)\n", (flags & MAPI_ONE_OFF_NO_RICH_INFO) ? "MIME" : "TNEF");
	std::wstring name, type, email;
	auto ret = ECParseOneOff(reinterpret_cast<const ENTRYID *>(s.data()),
	           s.size(), name, type, email);
	if (ret != hrSuccess) {
		printf("%-*sECParseOneOff failed: %s (%x)\n", mkind(i), "", GetMAPIErrorMessage(ret), ret);
		return;
	}
	printf("%-*sName: \"%s\"\n", mkind(i), "", convert_to<std::string>(name).c_str());
	printf("%-*sType: \"%s\"\n", mkind(i), "", convert_to<std::string>(type).c_str());
	printf("%-*sAddress: \"%s\"\n", mkind(i), "", convert_to<std::string>(email).c_str());
}

static void try_entryid(const string_view &s, unsigned int i)
{
	try_af1(s, i);
	try_af2(s, i);
	try_eidv1(s, i);
	try_eidv0(s, i);
	try_abeid(s, i);
	try_emsab(s, i);
	try_oneoff(s, i);
	try_kcwrap(s, i);
}

static void try_kcwrap(const string_view &s, unsigned int i)
{
	if (s.size() < sizeof(kc_muidwrap)) {
		printf("%-*sNot a ZCP/KC wrapped store entryid: have %zu bytes, expected at least %zu bytes\n",
			mkind(i), "", s.size(), sizeof(kc_muidwrap));
		return;
	}
	auto m = reinterpret_cast<const kc_muidwrap *>(s.data());
	if (memcmp(&m->provider_uid, &muidStoreWrap, sizeof(muidStoreWrap)) != 0) {
		printf("%-*sNot a ZCP/KC wrapped store entryid: wrong provider UID ",
			mkind(i), "");
		dump_guid_withvar(m->provider_uid);
		printf("\n");
		return;
	}
	printf("%-*sWrapped ZCP/KC store entryid:\n", mkind(i), "");
	++i;
	printf("%-*sDLL name: \"%s\"\n", mkind(i), "", m->dll_name());
	int eidsize = s.data() + s.size() - m->original_entryid();
	auto eidptr = m->original_entryid();
	printf("%-*sAnalyzing embedded entryid \"%.*s\":\n", mkind(i), "", eidsize, eidptr);
	try_entryid(string_view(eidptr, eidsize), i + 1);
}

static void try_decompose(const char *s)
{
	printf("%s:\n", s);
	auto b = hex2bin(s);
	unsigned int i = 1;
	printf("%-*sSize: %zu bytes\n", mkind(i), "", b.size());
	try_guid(b, i);
	try_entryid(b, i);
}

int main(int argc, char **argv)
{
	while (--argc > 0)
		try_decompose(*++argv);
	return EXIT_SUCCESS;
}
