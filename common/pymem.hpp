#ifndef KC_PYMEM_HPP
#define KC_PYMEM_HPP 1

#include <kopano/memory.hpp>

namespace KC {

class kcpy_delete {
	public:
	void operator()(PyObject *obj) const { Py_DECREF(obj); }
};

typedef KC::memory_ptr<PyObject, kcpy_delete> pyobj_ptr;

} /* namespace */

#endif /* KC_PYMEM_HPP */
