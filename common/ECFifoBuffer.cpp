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

#include <chrono>
#include <kopano/platform.h>
#include "ECFifoBuffer.h"

namespace KC {

ECFifoBuffer::ECFifoBuffer(size_type ulMaxSize)
	: m_ulMaxSize(ulMaxSize)
{
}

/**
 * Write data into the FIFO.
 *
 * @param[in]	lpBuf			Pointer to the data being written.
 * @param[in]	cbBuf			The amount of data to write (in bytes).
 * @param[out]	lpcbWritten		The amount of data actually written.
 * @param[in]	ulTimeoutMs		The maximum amount that this function may block.
 *
 * @retval	erSuccess					The data was successfully written.
 * @retval	KCERR_INVALID_PARAMETER	lpBuf is NULL.
 * @retval	KCERR_NOT_ENOUGH_MEMORY	There was not enough memory available to store the data.
 * @retval	KCERR_TIMEOUT			Not all data was written within the specified time limit.
 *										The amount of data that was written is returned in lpcbWritten.
 * @retval	KCERR_NETWORK_ERROR		The buffer was closed prior to this call.
 */
ECRESULT ECFifoBuffer::Write(const void *lpBuf, size_type cbBuf, unsigned int ulTimeoutMs, size_type *lpcbWritten)
{
	ECRESULT			er = erSuccess;
	size_type			cbWritten = 0;
	auto lpData = reinterpret_cast<const unsigned char *>(lpBuf);

	if (lpBuf == NULL)
		return KCERR_INVALID_PARAMETER;

	if (IsClosed(cfWrite))
	    return KCERR_NETWORK_ERROR;

	if (cbBuf == 0) {
		if (lpcbWritten)
			*lpcbWritten = 0;
		return erSuccess;
	}

	ulock_normal locker(m_hMutex);
	while (cbWritten < cbBuf) {
		while (IsFull()) {
		    if (IsClosed(cfRead)) {
				er = KCERR_NETWORK_ERROR;
				goto exit;
			}

			if (ulTimeoutMs > 0) {
				if (m_hCondNotFull.wait_for(locker,
				    std::chrono::milliseconds(ulTimeoutMs)) ==
				    std::cv_status::timeout) {
					er = KCERR_TIMEOUT;
					goto exit;
				}
			} else
				m_hCondNotFull.wait(locker);
		}

		const size_type cbNow = std::min(cbBuf - cbWritten, m_ulMaxSize - m_storage.size());
		try {
			m_storage.insert(m_storage.end(), lpData + cbWritten, lpData + cbWritten + cbNow);
		} catch (const std::bad_alloc &) {
			er = KCERR_NOT_ENOUGH_MEMORY;
			goto exit;
		}
		m_hCondNotEmpty.notify_one();
		cbWritten += cbNow;
	}

exit:
	locker.unlock();
	if (lpcbWritten && (er == erSuccess || er == KCERR_TIMEOUT))
		*lpcbWritten = cbWritten;

	return er;
}

/**
 * Read data from the FIFO.
 *
 * @param[in,out]	lpBuf			Pointer to where the data should be stored.
 * @param[in]		cbBuf			The amount of data to read (in bytes).
 * @param[out]		lpcbWritten		The amount of data actually read.
 * @param[in]		ulTimeoutMs		The maximum amount that this function may block.
 *
 * @retval	erSuccess					The data was successfully written.
 * @retval	KCERR_INVALID_PARAMETER	lpBuf is NULL.
 * @retval	KCERR_TIMEOUT			Not all data was written within the specified time limit.
 *										The amount of data that was written is returned in lpcbWritten.
 */
ECRESULT ECFifoBuffer::Read(void *lpBuf, size_type cbBuf, unsigned int ulTimeoutMs, size_type *lpcbRead)
{
	ECRESULT		er = erSuccess;
	size_type		cbRead = 0;
	auto lpData = reinterpret_cast<unsigned char *>(lpBuf);

	if (lpBuf == NULL)
		return KCERR_INVALID_PARAMETER;

	if (IsClosed(cfRead))
		return KCERR_NETWORK_ERROR;

	if (cbBuf == 0) {
		if (lpcbRead)
			*lpcbRead = 0;
		return erSuccess;
	}

	ulock_normal locker(m_hMutex);
	while (cbRead < cbBuf) {
		while (IsEmpty()) {
			if (IsClosed(cfWrite)) 
				goto exit;

			if (ulTimeoutMs > 0) {
				if (m_hCondNotEmpty.wait_for(locker,
				    std::chrono::milliseconds(ulTimeoutMs)) ==
				    std::cv_status::timeout) {
					er = KCERR_TIMEOUT;
					goto exit;
				}
			} else
				m_hCondNotEmpty.wait(locker);
		}

		const size_type cbNow = std::min(cbBuf - cbRead, m_storage.size());
		auto iEndNow = m_storage.begin() + cbNow;
		std::copy(m_storage.begin(), iEndNow, lpData + cbRead);
		m_storage.erase(m_storage.begin(), iEndNow);
		m_hCondNotFull.notify_one();
		cbRead += cbNow;
	}
	
	if (IsEmpty() && IsClosed(cfWrite))
		m_hCondFlushed.notify_one();
exit:
	locker.unlock();
	if (lpcbRead && (er == erSuccess || er == KCERR_TIMEOUT))
		*lpcbRead = cbRead;

	return er;
}

/**
 * Close a buffer.
 * This causes new writes to the buffer to fail with KCERR_NETWORK_ERROR and all
 * (pending) reads on the buffer to return immediately.
 *
 * @retval	erSucces (never fails)
 */
ECRESULT ECFifoBuffer::Close(close_flags flags)
{
	scoped_lock locker(m_hMutex);
	if (flags & cfRead) {
		m_bReaderClosed = true;
		m_hCondNotFull.notify_one();
		if(IsEmpty())
			m_hCondFlushed.notify_one();
	}
	if (flags & cfWrite) {
		m_bWriterClosed = true;
		m_hCondNotEmpty.notify_one();
	}
	return erSuccess;
}

/**
 * Wait for the stream to be flushed
 *
 * This guarantees that the reader has read all the data from the fifo or
 * the reader endpoint is closed.
 *
 * The writer endpoint must be closed before calling this method.
 */
ECRESULT ECFifoBuffer::Flush()
{
	if (!IsClosed(cfWrite))
		return KCERR_NETWORK_ERROR;

	ulock_normal locker(m_hMutex);
	m_hCondFlushed.wait(locker,
		[this](void) { return IsClosed(cfWrite) || IsEmpty(); });
	return erSuccess;
}

} /* namespace */
