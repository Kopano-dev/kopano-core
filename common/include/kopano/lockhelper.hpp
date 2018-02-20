#ifndef KC_LOCKHELPER_HPP
#define KC_LOCKHELPER_HPP 1

#include <kopano/zcdefs.h>
#include <mutex>
#if __cplusplus >= 201400L
#	include <shared_mutex>
#endif
#include <cassert>
#include <pthread.h>

namespace KC {

#if __cplusplus >= 201700L
using shared_mutex = std::shared_mutex;
#elif __cplusplus >= 201400L
using shared_mutex = std::shared_timed_mutex;
#else
class shared_mutex {
	public:
	~shared_mutex(void) { pthread_rwlock_destroy(&mtx); }
	void lock(void) { pthread_rwlock_wrlock(&mtx); }
	void unlock(void) { pthread_rwlock_unlock(&mtx); }
	void lock_shared(void) { pthread_rwlock_rdlock(&mtx); }
	void unlock_shared(void) { pthread_rwlock_unlock(&mtx); }

	private:
	pthread_rwlock_t mtx = PTHREAD_RWLOCK_INITIALIZER;
};
#endif

#if __cplusplus >= 201400L
template<class Mutex> using shared_lock = std::shared_lock<Mutex>;
#else
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
#endif

typedef std::lock_guard<std::mutex> scoped_lock;
typedef std::lock_guard<std::recursive_mutex> scoped_rlock;
typedef std::unique_lock<std::mutex> ulock_normal;
typedef std::unique_lock<std::recursive_mutex> ulock_rec;

} /* namespace */

#endif /* KC_LOCKHELPER_HPP */
