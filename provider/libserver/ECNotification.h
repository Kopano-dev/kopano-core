/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

// ECNotification.h: interface for the ECNotification class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ECNOTIFICATION
#define ECNOTIFICATION

#include <kopano/zcdefs.h>
#include "soapH.h"

struct soap;

namespace KC {

class ECNotification _kc_final {
public:
	ECNotification();
	virtual ~ECNotification();
	ECNotification(const ECNotification &x);
	ECNotification& operator=(const ECNotification &x);
	ECNotification(const notification &);
	ECNotification& operator=(const notification &srcNotification);

	void SetConnection(unsigned int ulConnection);

	void GetCopy(struct soap *, notification &) const;
	size_t GetObjectSize(void) const;

protected:
	void Init();

private:
	notification	*m_lpsNotification;

};

} /* namespace */

#endif // #ifndef ECNOTIFICATION
