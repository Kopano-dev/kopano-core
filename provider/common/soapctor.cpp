#include <type_traits>
#include <cstring>
#include "soapH.h"
#include "soapStub.h"
#define C(X) X::X(void) { static_assert(std::is_standard_layout<X>::value, ""); memset(this, 0, sizeof(*this)); }
C(_act)
C(company)
//gsoap-AG//C(exportMessageChangesAsStreamResponse)
//gsoap-AG//C(getChangeInfoResponse)
//gsoap-AG//C(getServerDetailsResponse)
//gsoap-AG//C(getSyncStatesReponse)
C(group)
C(notification)
C(notificationICS)
C(notifySubscribe)
C(notifySubscribeArray)
C(notifySyncState)
C(propTagArray)
C(propVal)
C(propValArray)
C(propValData)
//gsoap-AG//C(resetFolderCountResponse)
//gsoap-AG//C(resolvePseudoUrlResponse)
C(rights)
C(sortOrder)
C(sortOrderArray)
C(tableMultiRequest)
//gsoap-AG//C(tableMultiResponse)
C(tableOpenRequest)
C(user)
C(xsd__Binary)

mv_binary::mv_binary() : __ptr(), __size() {}
mv_double::mv_double() : __ptr(), __size() {}
mv_hiloLong::mv_hiloLong() : __ptr(), __size() {}
mv_i2::mv_i2() : __ptr(), __size() {}
mv_i8::mv_i8() : __ptr(), __size() {}
mv_long::mv_long() : __ptr(), __size() {}
mv_long::mv_long(unsigned int *a, int b) : __ptr(a), __size(b) {}
mv_r4::mv_r4() : __ptr(), __size() {}
mv_string8::mv_string8() : __ptr(), __size() {}

propTagArray::propTagArray(unsigned int *a, int b) :
	__ptr(a), __size(b)
{}

sortOrder::sortOrder(unsigned int a, unsigned int b) :
	ulPropTag(a), ulOrder(b)
{}

sortOrderArray::sortOrderArray(struct sortOrder *a, int b) :
	__ptr(a), __size(b)
{}

notifySyncState::notifySyncState(unsigned int a, unsigned int b) :
	ulSyncId(a), ulChangeId(b)
{}

rights::rights(unsigned int a, unsigned int b, unsigned int c, unsigned int d) :
	ulUserid(a), ulType(b), ulRights(c), ulState(d)
{
	sUserId.__ptr = nullptr;
	sUserId.__size = 0;
}

xsd__base64Binary::xsd__base64Binary() : __ptr(), __size() {}
xsd__base64Binary::xsd__base64Binary(unsigned char *a, int b) : __ptr(a), __size(b) {}
