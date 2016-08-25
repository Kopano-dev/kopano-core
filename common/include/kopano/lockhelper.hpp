#ifndef KC_LOCKHELPER_HPP
#define KC_LOCKHELPER_HPP 1

#include <kopano/zcdefs.h>
#include <mutex>
#include <pthread.h>

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

#endif /* KC_LOCKHELPER_HPP */
