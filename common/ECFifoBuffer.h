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

#ifndef ECFIFOBUFFER_H
#define ECFIFOBUFFER_H

#include <kopano/zcdefs.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <kopano/kcodes.h>

namespace KC {

// Thread safe buffer for FIFO operations
class _kc_export ECFifoBuffer _kc_final {
public:
	typedef std::deque<unsigned char>	storage_type;
	typedef storage_type::size_type		size_type;
	enum close_flags { cfRead = 1, cfWrite = 2 };

public:
	ECFifoBuffer(size_type ulMaxSize = 131072);
	ECRESULT Write(const void *lpBuf, size_type cbBuf, unsigned int ulTimeoutMs, size_type *lpcbWritten);
	ECRESULT Read(void *lpBuf, size_type cbBuf, unsigned int ulTimeoutMs, size_type *lpcbRead);
	ECRESULT Close(close_flags flags);
	_kc_hidden ECRESULT Flush(void);
	_kc_hidden bool IsClosed(ULONG flags) const;
	_kc_hidden bool IsEmpty(void) const;
	_kc_hidden bool IsFull(void) const;
	
private:
	// prohibit copy
	ECFifoBuffer(const ECFifoBuffer &) = delete;
	ECFifoBuffer &operator=(const ECFifoBuffer &) = delete;
	
private:
	storage_type	m_storage;
	size_type		m_ulMaxSize;
	bool			m_bReaderClosed;
	bool            m_bWriterClosed;

	std::mutex m_hMutex;
	std::condition_variable m_hCondNotEmpty, m_hCondNotFull, m_hCondFlushed;
};


// inlines
inline bool ECFifoBuffer::IsClosed(ULONG flags) const {
	switch (flags) {
	case cfRead:
		return m_bReaderClosed;
	case cfWrite:
		return m_bWriterClosed;
	case cfRead|cfWrite:
		return m_bReaderClosed && m_bWriterClosed;
	default:
		assert(false);
		return false;
	}
}

inline bool ECFifoBuffer::IsEmpty() const {
	return m_storage.empty();
}

inline bool ECFifoBuffer::IsFull() const {
	return m_storage.size() == m_ulMaxSize;
}

} /* namespace */

#endif // ndef ECFIFOBUFFER_H
