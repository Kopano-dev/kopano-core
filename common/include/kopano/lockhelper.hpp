#ifndef KC_LOCKHELPER_HPP
#define KC_LOCKHELPER_HPP 1

#include <kopano/zcdefs.h>
#include <mutex>
#include <new>
#include <cassert>
#include <pthread.h>

namespace KC {

/* std::shared_mutex only available from C++17 onwards */

class shared_mutex {
	public:
	~shared_mutex(void) { pthread_rwlock_destroy(&mtx); }
	void lock(void) { pthread_rwlock_wrlock(&mtx); }
	void unlock(void) { pthread_rwlock_unlock(&mtx); }
	void lock_shared(void) { pthread_rwlock_rdlock(&mtx); }
	void unlock_shared(void) { pthread_rwlock_unlock(&mtx); }

	private:
	pthread_rwlock_t mtx = PTHREAD_RWLOCK_INITIALIZER;
	bool locked = false;
};

template<class _Mutex> class shared_lock {
	public:
	shared_lock(_Mutex &m) : mtx(m), locked(true)
	{
		mtx.lock_shared();
	}
	~shared_lock(void)
	{
		if (locked)
			mtx.unlock_shared();
	}
	void lock(void)
	{
		assert(!locked);
		mtx.lock_shared();
		locked = true;
	}
	void unlock(void)
	{
		assert(locked);
		mtx.unlock_shared();
		locked = false;
	}
	private:
	_Mutex &mtx;
	bool locked = false;
};

template<int (*fnlock)(pthread_rwlock_t *)> class scoped_rwlock _kc_final {
	public:
	scoped_rwlock(pthread_rwlock_t &rwlock) : m_rwlock(rwlock)
	{
		fnlock(&m_rwlock);
	}

	~scoped_rwlock(void)
	{
		pthread_rwlock_unlock(&m_rwlock);
	}

	private:
	scoped_rwlock(const scoped_rwlock &) = delete;
	scoped_rwlock &operator=(const scoped_rwlock &) = delete;

	pthread_rwlock_t &m_rwlock;
};

typedef scoped_rwlock<pthread_rwlock_wrlock> scoped_exclusive_rwlock;
typedef scoped_rwlock<pthread_rwlock_rdlock> scoped_shared_rwlock;
typedef std::lock_guard<std::mutex> scoped_lock;
typedef std::lock_guard<std::recursive_mutex> scoped_rlock;
typedef std::unique_lock<std::mutex> ulock_normal;
typedef std::unique_lock<std::recursive_mutex> ulock_rec;

} /* namespace */

#endif /* KC_LOCKHELPER_HPP */
