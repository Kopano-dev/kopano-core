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

#include <kopano/platform.h>
#include <memory>
#include <new>
#include <arpa/inet.h>
#include "ECFifoBuffer.h"
#include "ECSerializer.h"

namespace KC {

ECStreamSerializer::ECStreamSerializer(IStream *lpBuffer)
{
	SetBuffer(lpBuffer);
	m_ulRead = m_ulWritten = 0;
}

ECRESULT ECStreamSerializer::SetBuffer(void *lpBuffer)
{
	m_lpBuffer = (IStream *)lpBuffer;
	return erSuccess;
}

ECRESULT ECStreamSerializer::Write(const void *ptr, size_t size, size_t nmemb)
{
	ECRESULT er = erSuccess;
	ULONG cbWritten = 0;
	union {
		char c[8];
		short s;
		int i;
		long long ll;
	} tmp;

	if (ptr == NULL)
		return KCERR_INVALID_PARAMETER;

	switch (size) {
	case 1:
		er = m_lpBuffer->Write(ptr, nmemb, &cbWritten);
		break;
	case 2:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.s = htons(((const short *)ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, &cbWritten);
		}
		break;
	case 4:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.i = htonl(((const int *)ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, &cbWritten);
		}
		break;
	case 8:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.ll = htonll(((const long long *)ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, &cbWritten);
		}
		break;
	default:
		er = KCERR_INVALID_PARAMETER;
		break;
	}
	
	m_ulWritten += size * nmemb;

	return er;
}

ECRESULT ECStreamSerializer::Read(void *ptr, size_t size, size_t nmemb)
{
	ECRESULT er;
	ULONG cbRead = 0;

	if (ptr == NULL)
		return KCERR_INVALID_PARAMETER;

	er = m_lpBuffer->Read(ptr, size * nmemb, &cbRead);
	if (er != erSuccess)
		return er;

	m_ulRead += cbRead;

	if (cbRead != size * nmemb)
		return KCERR_CALL_FAILED;

	switch (size) {
	case 1: break;
	case 2:
		for (size_t x = 0; x < nmemb; ++x)
			((short *)ptr)[x] = ntohs(((short *)ptr)[x]);
		break;
	case 4:
		for (size_t x = 0; x < nmemb; ++x)
			((int *)ptr)[x] = ntohl(((int *)ptr)[x]);
		break;
	case 8:
		for (size_t x = 0; x < nmemb; ++x)
			((long long *)ptr)[x] = ntohll(((long long *)ptr)[x]);
		break;
	default:
		er = KCERR_INVALID_PARAMETER;
		break;
	}
	return er;
}

ECRESULT ECStreamSerializer::Skip(size_t size, size_t nmemb)
{
	ECRESULT er = erSuccess;
	char buffer[4096];
	ULONG read = 0;
	size_t total = 0;

	while(total < (nmemb*size)) {
		er = m_lpBuffer->Read(buffer, std::min(sizeof(buffer), (size * nmemb) - total), &read);
		if(er != erSuccess)
			return er;
		total += read;
	}
	return er;
}

ECRESULT ECStreamSerializer::Flush()
{
	ECRESULT er;
	ULONG cbRead = 0;
	char buf[16384];
	
	while(true) {
		er = m_lpBuffer->Read(buf, sizeof(buf), &cbRead);
		if (er != erSuccess)
			return er;

		m_ulRead += cbRead;

		if(cbRead < sizeof(buf))
			break;
	}
	return er;
}

ECRESULT ECStreamSerializer::Stat(ULONG *lpcbRead, ULONG *lpcbWrite)
{
	if(lpcbRead)
		*lpcbRead = m_ulRead;
		
	if(lpcbWrite)
		*lpcbWrite = m_ulWritten;
		
	return erSuccess;
}

ECFifoSerializer::ECFifoSerializer(ECFifoBuffer *lpBuffer, eMode mode)
{
	SetBuffer(lpBuffer);
	m_mode = mode;
	m_ulRead = m_ulWritten = 0;
}

ECFifoSerializer::~ECFifoSerializer()
{
	if (m_lpBuffer) {
		ECFifoBuffer::close_flags flags = (m_mode == serialize ? ECFifoBuffer::cfWrite : ECFifoBuffer::cfRead);
		m_lpBuffer->Close(flags);
	}
}

ECRESULT ECFifoSerializer::SetBuffer(void *lpBuffer)
{
	m_lpBuffer = (ECFifoBuffer *)lpBuffer;
	return erSuccess;
}

ECRESULT ECFifoSerializer::Write(const void *ptr, size_t size, size_t nmemb)
{
	ECRESULT er = erSuccess;
	union {
		char c[8];
		short s;
		int i;
		long long ll;
	} tmp;

	if (m_mode != serialize)
		return KCERR_NO_SUPPORT;

	if (ptr == NULL)
		return KCERR_INVALID_PARAMETER;

	switch (size) {
	case 1:
		er = m_lpBuffer->Write(ptr, nmemb, STR_DEF_TIMEOUT, NULL);
		break;
	case 2:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.s = htons(((const short *)ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, STR_DEF_TIMEOUT, NULL);
		}
		break;
	case 4:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.i = htonl(((const int *)ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, STR_DEF_TIMEOUT, NULL);
		}
		break;
	case 8:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.ll = htonll(((const long long *)ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, STR_DEF_TIMEOUT, NULL);
		}
		break;
	default:
		er = KCERR_INVALID_PARAMETER;
		break;
	}
	
	m_ulWritten += size * nmemb;

	return er;
}

ECRESULT ECFifoSerializer::Read(void *ptr, size_t size, size_t nmemb)
{
	ECRESULT er;
	ECFifoBuffer::size_type cbRead = 0;

	if (m_mode != deserialize)
		return KCERR_NO_SUPPORT;
	if (ptr == NULL)
		return KCERR_INVALID_PARAMETER;

	er = m_lpBuffer->Read(ptr, size * nmemb, STR_DEF_TIMEOUT, &cbRead);
	if (er != erSuccess)
		return er;

	m_ulRead += cbRead;

	if (cbRead != size * nmemb)
		return KCERR_CALL_FAILED;
	

	switch (size) {
	case 1: break;
	case 2:
		for (size_t x = 0; x < nmemb; ++x)
			((short *)ptr)[x] = ntohs(((short *)ptr)[x]);
		break;
	case 4:
		for (size_t x = 0; x < nmemb; ++x) 
			((int *)ptr)[x] = ntohl(((int *)ptr)[x]);
		break;
	case 8:
		for (size_t x = 0; x < nmemb; ++x)
			((long long *)ptr)[x] = ntohll(((long long *)ptr)[x]);
		break;
	default:
		er = KCERR_INVALID_PARAMETER;
		break;
	}
	return er;
}

ECRESULT ECFifoSerializer::Skip(size_t size, size_t nmemb)
{
	std::unique_ptr<char[]> buf(new(std::nothrow) char[size*nmemb]);
	if (buf == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	return Read(buf.get(), size, nmemb);
}

ECRESULT ECFifoSerializer::Flush()
{
	ECRESULT er;
	size_t cbRead = 0;
	char buf[16384];
	
	while(true) {
		er = m_lpBuffer->Read(buf, sizeof(buf), STR_DEF_TIMEOUT, &cbRead);
		if (er != erSuccess)
			return er;

		m_ulRead += cbRead;

		if(cbRead < sizeof(buf))
			break;
	}
	return er;
}

ECRESULT ECFifoSerializer::Stat(ULONG *lpcbRead, ULONG *lpcbWrite)
{
	if(lpcbRead)
		*lpcbRead = m_ulRead;
		
	if(lpcbWrite)
		*lpcbWrite = m_ulWritten;
		
	return erSuccess;
}

} /* namespace */
