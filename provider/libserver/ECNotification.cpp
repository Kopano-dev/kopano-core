/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// ECNotification.cpp: implementation of the ECNotification class.
//
//////////////////////////////////////////////////////////////////////
#include <kopano/platform.h>

#include "ECNotification.h"
#include "ECMAPI.h"
#include "SOAPUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

ECNotification::ECNotification()
{
	Init();
}

ECNotification::~ECNotification()
{
	FreeNotificationStruct(m_lpsNotification, true);
}

ECNotification::ECNotification(const ECNotification &x)
{
	Init();

	*this = x;
}

ECNotification::ECNotification(notification &notification)
{
	Init();

	*this = notification;
}

void ECNotification::Init()
{
	this->m_lpsNotification = new notification;

	memset(m_lpsNotification, 0, sizeof(notification));
}

ECNotification& ECNotification::operator=(const ECNotification &x)
{
	if(this != &x){
		CopyNotificationStruct(NULL, x.m_lpsNotification, *this->m_lpsNotification);
	}

	return *this;
}

ECNotification& ECNotification::operator=(const notification &srcNotification)
{

	CopyNotificationStruct(NULL, (notification *)&srcNotification, *this->m_lpsNotification);

	return *this;
}

void ECNotification::SetConnection(unsigned int ulConnection)
{
	m_lpsNotification->ulConnection = ulConnection;
}

void ECNotification::GetCopy(struct soap *soap, notification &notification) const
{
	CopyNotificationStruct(soap, this->m_lpsNotification, notification);
}

/**
 * Get object size
 *
 * @return Object size in bytes
 */
unsigned int ECNotification::GetObjectSize(void) const
{
	return NotificationStructSize(m_lpsNotification);
}
