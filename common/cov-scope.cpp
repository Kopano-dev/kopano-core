/*
 *	Trigger Coverity WRAPPER_ESCAPE reporting
 *
 *	We believe Coverity is misidentifying WRAPPER_ESCAPE in certain
 *	circumstances, and this test program tries to evoke it.
 *
 *	"Wrapper object use after free (WRAPPER_ESCAPE
 *	 escape: The internal representation of local `hel_w` escapes into
 *	 `prop[x].Value.lpszW`, but is destroyed when it exits scope."
 */
#include <kopano/platform.h>
#include <string>
#include <cstdlib>
#include <mapidefs.h>
class I {
	public:
	virtual ~I(void) {}
	/* ptr-to-non-const on purpose */
	virtual void SetProps(SPropValue *, size_t) {}
};
class X : public I {
	public:
	virtual void SetProps(SPropValue *, size_t) {}
};
class Y : public I {
	public:
	virtual void SetProps(SPropValue *, size_t) {}
};
int main(void)
{
	SPropValue prop[8];
	std::string hel = "Hello World";
	std::wstring hel_w = L"Hello World";
	memset(prop, 0, sizeof(prop));
	prop[0].Value.lpszA = (char *)hel.c_str();
	prop[1].Value.lpszW = (WCHAR *)hel.c_str();
	prop[2].Value.lpszA = (char *)hel_w.c_str();
	prop[3].Value.lpszW = (WCHAR *)hel_w.c_str();
	prop[4].Value.lpszA = const_cast<char *>(hel.c_str());
	prop[5].Value.lpszW = reinterpret_cast<wchar_t *>(const_cast<char *>(hel.c_str()));
	prop[6].Value.lpszA = reinterpret_cast<char *>(const_cast<wchar_t *>(hel_w.c_str()));
	prop[7].Value.lpszW = const_cast<wchar_t *>(hel_w.c_str());

	I *obj;
	if (rand() & 1)
		obj = new X();
	else
		obj = new Y();
	obj->SetProps(prop, ARRAY_SIZE(prop));
	memset(prop, '\0', sizeof(prop));
	delete obj;
	return 0;
}
