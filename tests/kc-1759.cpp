#include <mapidefs.h>
#include <cstdlib>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>

using namespace KC;

static const char *user = "user1", *pass = "user1";

static HRESULT make_msg(object_ptr<IMessage> &msg)
{
	KServerContext ctx;
	auto ret = ctx.logon(user, pass);
	if (ret != hrSuccess)
		return kc_perror(".logon", ret);
	object_ptr<IMAPIFolder> inbox;
	ret = ctx.inbox(&~inbox);
	if (ret != hrSuccess)
		return kc_perror(".inbox", ret);
	ret = inbox->CreateMessage(nullptr, 0, &~msg);
	if (ret != hrSuccess)
		return kc_perror("CreateMessage", ret);
	return hrSuccess;
}

static HRESULT make_at(object_ptr<IMessage> msg, unsigned int &anum)
{
	object_ptr<IAttach> at;
	auto ret = msg->CreateAttach(&iid_of(at), MAPI_DEFERRED_ERRORS, &anum, &~at);
	if (ret != hrSuccess)
		return kc_perror("msg.CreateAttach", ret);
	SPropValue pv;
	pv.ulPropTag     = PR_ATTACH_DATA_BIN;
	pv.Value.bin.cb  = 4;
	pv.Value.bin.lpb = reinterpret_cast<BYTE *>(const_cast<char *>("AAAA"));
	ret = at->SetProps(1, &pv, nullptr);
	if (ret != hrSuccess)
		return kc_perror("at.SetProps", ret);
	ret = at->SaveChanges(KEEP_OPEN_READWRITE);
	if (ret != hrSuccess)
		kc_perror("at.SaveChanges", ret);
	ret = msg->SaveChanges(KEEP_OPEN_READWRITE);
	if (ret != hrSuccess)
		kc_perror("msg.SaveChanges", ret);
	return hrSuccess;
}

static HRESULT copy_at(object_ptr<IMessage> msg, unsigned int anum1,
    unsigned int &anum2, bool replace)
{
	object_ptr<IAttach> at1, at2;
	object_ptr<IECSingleInstance> si1, si2;
	auto ret = msg->OpenAttach(anum1, &iid_of(at1), MAPI_DEFERRED_ERRORS | MAPI_MODIFY, &~at1);
	if (ret != hrSuccess)
		return kc_perror("OpenAttach", ret);
	ret = at1->QueryInterface(iid_of(si1), &~si1);
	if (ret != hrSuccess)
		return kc_perror("QueryInterface", ret);

	unsigned int sicb = 0, sicb2 = 0;
	memory_ptr<ENTRYID> sieid, sieid2;
	ret = si1->GetSingleInstanceId(&sicb, &~sieid);
	if (ret != hrSuccess)
		return kc_perror("GSID", ret);
	if (sicb == 0)
		return kc_perror("SIID is zero-length", MAPI_E_CALL_FAILED);

	if (replace)
		ret = msg->OpenAttach(anum2, &iid_of(at2), MAPI_MODIFY | MAPI_DEFERRED_ERRORS, &~at2);
	else
		ret = msg->CreateAttach(&iid_of(at2), MAPI_DEFERRED_ERRORS, &anum2, &~at2);
	if (ret != hrSuccess)
		return kc_perror("CreateAttach2", ret);
	ret = at2->QueryInterface(iid_of(si2), &~si2);
	if (ret != hrSuccess)
		return kc_perror("QueryInterface", ret);
	ret = si2->SetSingleInstanceId(sicb, sieid);
	if (ret != hrSuccess)
		return kc_perror("SSID", ret);
	ret = at2->SaveChanges(KEEP_OPEN_READWRITE);
	if (ret != hrSuccess)
		return kc_perror("at2.SaveChanges", ret);
	ret = msg->SaveChanges(KEEP_OPEN_READWRITE);
	if (ret != hrSuccess)
		return kc_perror("msg.SaveChanges", ret);

	/* Verify SIEID update */
	ret = msg->OpenAttach(anum2, &iid_of(at2), MAPI_MODIFY | MAPI_DEFERRED_ERRORS, &~at2);
	if (ret != hrSuccess)
		return kc_perror("OpenAttach2.2", ret);
	ret = at2->QueryInterface(iid_of(si2), &~si2);
	if (ret != hrSuccess)
		return kc_perror("QueryInterface", ret);
	ret = si2->GetSingleInstanceId(&sicb2, &~sieid2);
	if (ret != hrSuccess)
		return kc_perror("GetSingleInstanceId", ret);
	if (sicb != sicb2 ||
	    (sieid != nullptr && sieid2 != nullptr &&
	    memcmp(sieid.get(), sieid2.get(), sicb) != 0))
		return kc_perror("Server bug: SIEID was not updated to expected value", MAPI_E_CALL_FAILED);
	return hrSuccess;
}

static HRESULT main2()
{
	AutoMAPI am;
	object_ptr<IMessage> msg;
	unsigned int anum1, anum2, anum3;

	auto ret = am.Initialize();
	if (ret != hrSuccess)
		return kc_perror("am.Initialize", ret);
	ret = make_msg(msg);
	if (ret != hrSuccess)
		return ret;
	ret = make_at(msg, anum1);
	if (ret != hrSuccess)
		return ret;
	ret = copy_at(msg, anum1, anum2, false);
	if (ret != hrSuccess)
		return ret;
	ret = make_at(msg, anum3);
	if (ret != hrSuccess)
		return ret;
	ret = copy_at(msg, anum1, anum3, true);
	if (ret != hrSuccess)
		return ret;
	return hrSuccess;
}

int main()
{
	auto s = getenv("KOPANO_USER");
	if (s != nullptr)
		user = s;
	s = getenv("KOPANO_PASS");
	if (s != nullptr)
		pass = s;
	return main2() == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}
