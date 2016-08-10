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
#include "ECFifoBuffer.h"


ECFifoBuffer::ECFifoBuffer(size_type ulMaxSize)
	: m_ulMaxSize(ulMaxSize)
	, m_bReaderClosed(false)
	, m_bWriterClosed(false)
{
	pthread_mutex_init(&m_hMutex, NULL);
	pthread_cond_init(&m_hCondNotFull, NULL);
	pthread_cond_init(&m_hCondNotEmpty, NULL);
	pthread_cond_init(&m_hCondFlushed, NULL);
}

ECFifoBuffer::~ECFifoBuffer()
{
	pthread_mutex_destroy(&m_hMutex);
	pthread_cond_destroy(&m_hCondNotFull);
	pthread_cond_destroy(&m_hCondNotEmpty);
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
 * @retval	KCERR_TIMEOUT			Not all data was writting within the specified time limit.
 *										The amount of data that was written is returned in lpcbWritten.
 * @retval	KCERR_NETWORK_ERROR		The buffer was closed prior to this call.
 */
ECRESULT ECFifoBuffer::Write(const void *lpBuf, size_type cbBuf, unsigned int ulTimeoutMs, size_type *lpcbWritten)
{
	ECRESULT			er = erSuccess;
	size_type			cbWritten = 0;
	struct timespec		deadline = {0};
	const unsigned char	*lpData = reinterpret_cast<const unsigned char*>(lpBuf);

	if (lpBuf == NULL)
		return KCERR_INVALID_PARAMETER;

	if (IsClosed(cfWrite))
	    return KCERR_NETWORK_ERROR;

	if (cbBuf == 0) {
		if (lpcbWritten)
			*lpcbWritten = 0;
		return erSuccess;
	}

	if (ulTimeoutMs > 0)
		deadline = GetDeadline(ulTimeoutMs);

	pthread_mutex_lock(&m_hMutex);

	while (cbWritten < cbBuf) {
		while (IsFull()) {
		    if (IsClosed(cfRead)) {
				er = KCERR_NETWORK_ERROR;
				goto exit;
			}

			if (ulTimeoutMs > 0) {
				if (pthread_cond_timedwait(&m_hCondNotFull, &m_hMutex, &deadline) == ETIMEDOUT) {
					er = KCERR_TIMEOUT;
					goto exit;
				}
			} else
				pthread_cond_wait(&m_hCondNotFull, &m_hMutex);
		}

		const size_type cbNow = std::min(cbBuf - cbWritten, m_ulMaxSize - m_storage.size());
		try {
			m_storage.insert(m_storage.end(), lpData + cbWritten, lpData + cbWritten + cbNow);
		} catch (const std::bad_alloc &) {
			er = KCERR_NOT_ENOUGH_MEMORY;
			goto exit;
		}
		pthread_cond_signal(&m_hCondNotEmpty);
		cbWritten += cbNow;
	}

exit:
	pthread_mutex_unlock(&m_hMutex);

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
 * @retval	KCERR_TIMEOUT			Not all data was writting within the specified time limit.
 *										The amount of data that was written is returned in lpcbWritten.
 */
ECRESULT ECFifoBuffer::Read(void *lpBuf, size_type cbBuf, unsigned int ulTimeoutMs, size_type *lpcbRead)
{
	ECRESULT		er = erSuccess;
	size_type		cbRead = 0;
	struct timespec	deadline = {0};
	unsigned char	*lpData = reinterpret_cast<unsigned char*>(lpBuf);

	if (lpBuf == NULL)
		return KCERR_INVALID_PARAMETER;

	if (IsClosed(cfRead))
		return KCERR_NETWORK_ERROR;

	if (cbBuf == 0) {
		if (lpcbRead)
			*lpcbRead = 0;
		return erSuccess;
	}

	if (ulTimeoutMs > 0)
		deadline = GetDeadline(ulTimeoutMs);
	
	pthread_mutex_lock(&m_hMutex);

	while (cbRead < cbBuf) {
		while (IsEmpty()) {
			if (IsClosed(cfWrite)) 
				goto exit;

			if (ulTimeoutMs > 0) {
				if (pthread_cond_timedwait(&m_hCondNotEmpty, &m_hMutex, &deadline) == ETIMEDOUT) {
					er = KCERR_TIMEOUT;
					goto exit;
				}
			} else
				pthread_cond_wait(&m_hCondNotEmpty, &m_hMutex);
		}

		const size_type cbNow = std::min(cbBuf - cbRead, m_storage.size());
		storage_type::iterator iEndNow = m_storage.begin() + cbNow;
		std::copy(m_storage.begin(), iEndNow, lpData + cbRead);
		m_storage.erase(m_storage.begin(), iEndNow);
		pthread_cond_signal(&m_hCondNotFull);
		cbRead += cbNow;
	}
	
	if(IsEmpty() && IsClosed(cfWrite)) {
		pthread_cond_signal(&m_hCondFlushed);
	}

exit:
	pthread_mutex_unlock(&m_hMutex);

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
	pthread_mutex_lock(&m_hMutex);
	if (flags & cfRead) {
		m_bReaderClosed = true;
		pthread_cond_signal(&m_hCondNotFull);

		if(IsEmpty())
			pthread_cond_signal(&m_hCondFlushed);
	}
	if (flags & cfWrite) {
		m_bWriterClosed = true;
		pthread_cond_signal(&m_hCondNotEmpty);
	}

	pthread_mutex_unlock(&m_hMutex);
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

	pthread_mutex_lock(&m_hMutex);
	while (!(IsClosed(cfWrite) || IsEmpty()))
		pthread_cond_wait(&m_hCondFlushed, &m_hMutex);
	pthread_mutex_unlock(&m_hMutex);
	
	return erSuccess;
}
