#include <type_traits>
#include <cstring>
#include "soapH.h"
#include "soapStub.h"
#define C(X) X::X(void) { static_assert(std::is_standard_layout<X>::value, ""); memset(this, 0, sizeof(*this)); }
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
