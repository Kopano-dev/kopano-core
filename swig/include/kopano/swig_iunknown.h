/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef SWIG_IUNKNOWN_H
#define SWIG_IUNKNOWN_H

#include <set>
#include <stdexcept>
#include <cstdio>

/**
 * IUnknownImplementor takes care of the IUnknown part of an IUnknown
 * derived interface.
 */
template<typename Interface> class IUnknownImplementor : public Interface {
public:
	IUnknownImplementor(ULONG cInterfaces, LPCIID lpInterfaces)
	: m_interfaces(lpInterfaces, lpInterfaces + cInterfaces, &IIDLess)
	{ }

	IUnknownImplementor()
	: m_interfaces(&IID_IUnknown, &IID_IUnknown + 1, &IIDLess)
	{ }

	virtual ~IUnknownImplementor() { 
	}

	ULONG AddRef()
	{
		PyGILState_STATE gstate;
		gstate = PyGILState_Ensure();
		
		auto director = dynamic_cast<Swig::Director *>(this);
		if (director == nullptr)
			throw std::runtime_error("dynamic_cast<> yielded a nullptr");
		auto o = director->swig_get_self();
		if (o == nullptr)
			throw std::runtime_error("swig_get_self yielded a nullptr");
		Py_INCREF(o);

		PyGILState_Release(gstate);
		return o->ob_refcnt;
	}

	ULONG Release()
	{
		PyGILState_STATE gstate;
		gstate = PyGILState_Ensure();
		
		auto director = dynamic_cast<Swig::Director *>(this);
		if (director == nullptr)
			throw std::runtime_error("dynamic_cast<> yielded a nullptr");
		auto o = director->swig_get_self();
		if (o == nullptr)
			throw std::runtime_error("swig_get_self yielded a nullptr");
		ULONG cnt = o->ob_refcnt;
		Py_DECREF(o); // Will delete this because python object will have refcount 0, which deletes this object

		PyGILState_Release(gstate);
		return cnt-1;
	}

	HRESULT QueryInterface(REFIID iid , void **ppvObject)
	{
		if (m_interfaces.find(iid) == m_interfaces.end())
			return MAPI_E_INTERFACE_NOT_SUPPORTED;
			
		AddRef();
		*ppvObject = (void*)this;
		return hrSuccess;
	}
	

private:
	static bool IIDLess(REFIID lhs, REFIID rhs) {
		return memcmp(&lhs, &rhs, sizeof(IID)) < 0;
	}

	std::set<IID, bool(*)(REFIID,REFIID)> m_interfaces;
};

#endif // ndef SWIG_IUNKNOWN_H
