/* SPDX-License-Identifier: AGPL-3.0-only */
%include <kopano/output3.i>
%module archiver

%{
	#include <kopano/zcdefs.h>
	#include <stdexcept>
	#include "../../ECtools/archiver/Archiver.h"
	#include <kopano/charset/convert.h>
	// Note: the lifetime of the return value only lasts until the end of
	// the scope that TO_LPTST is used in */
	#define TO_LPTST(s) ((s) ? convert_to<tstring>(s).c_str() : nullptr)
	using namespace KC;

	class KC_EXPORT_THROW ArchiverError : public std::runtime_error {
	public:
		ArchiverError(eResult e, const std::string &message)
		: std::runtime_error(message)
		, m_e(e)
		{ }

		eResult result() const { return m_e; }

	private:
		eResult m_e;
	};
%}

%include "exception.i"
%exception
{
    try {
   		$action
    } catch (const ArchiverError &ae) {
		SWIG_exception(SWIG_RuntimeError, ae.what());
	}
}

#if SWIGPYTHON
%include "python/archiver_python.i"
#endif

class ArchiveControl {
public:
	%extend {
		void ArchiveAll(bool bLocalOnly, bool bAutoAttach = false, unsigned int ulFlags = ArchiveManage::Writable) {
			auto e = self->ArchiveAll(bLocalOnly, bAutoAttach, ulFlags);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}

		void Archive(const char *lpszUser, bool bAutoAttach = false, unsigned int ulFlags = ArchiveManage::Writable) {
			auto e = self->Archive(TO_LPTST(lpszUser), bAutoAttach, ulFlags);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}

		void CleanupAll(bool bLocalOnly) {
			auto e = self->CleanupAll(bLocalOnly);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}

		void Cleanup(const char *lpszUser) {
			auto e = self->Cleanup(TO_LPTST(lpszUser));
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}
	}

private:
	ArchiveControl();
};




struct ArchiveEntry {
	std::string StoreName;
	std::string FolderName;
	std::string StoreOwner;
	unsigned Rights;
};
typedef std::list<ArchiveEntry> ArchiveList;

struct UserEntry {
	std::string UserName;
};
typedef std::list<UserEntry> UserList;


class ArchiveManage {
public:
	enum {
		UseIpmSubtree = 1,
		Writable = 2,
		ReadOnly = 4
	};

	%extend {
		void AttachTo(const char *lpszArchiveServer, const char *lpszArchive, const char *lpszFolder, unsigned int ulFlags) {
			auto e = self->AttachTo(lpszArchiveServer, TO_LPTST(lpszArchive), TO_LPTST(lpszFolder), ulFlags);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}

		void DetachFrom(const char *lpszArchiveServer, const char *lpszArchive, const char *lpszFolder) {
			auto e = self->DetachFrom(lpszArchiveServer, TO_LPTST(lpszArchive), TO_LPTST(lpszFolder));
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}

		void DetachFrom(unsigned int ulArchive) {
			auto e = self->DetachFrom(ulArchive);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}

		ArchiveList ListArchives(const char *lpszIpmSubtreeSubstitude = NULL) {
			ArchiveList lst;
			auto e = self->ListArchives(&lst);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
			return lst;
		}

		UserList ListAttachedUsers() {
			UserList lst;
			auto e = self->ListAttachedUsers(&lst);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
			return lst;
		}

		void AutoAttach(unsigned int ulFlags = ArchiveManage::Writable) {
			auto e = self->AutoAttach(ulFlags);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}
	}

private:
	ArchiveManage();
};



class Archiver {
public:
	enum {
		RequireConfig		= 0x00000001,
		AttachStdErr		= 0x00000002,
		InhibitErrorLogging	= 0x40000000
	};

	%newobject Create;
	%newobject GetControl;
	%newobject GetManage;

	%extend {
		static Archiver *Create(const char *lpszAppName, const char *lpszConfig, unsigned int ulFlags = 0) {
			std::unique_ptr<Archiver> ptr;
			auto r = Archiver::Create(&ptr);
			if (r != Success)
				throw ArchiverError(r, "Failed to instantiate object!");
			if (lpszAppName == nullptr)
				lpszAppName = "python-unspecified";
			r = ptr->Init(lpszAppName, lpszConfig, NULL, ulFlags);
			if (r != Success)
				throw ArchiverError(r, "Failed to initialize object!");
			return ptr.release();
		}

		ArchiveControl *GetControl(bool bForceCleanup = false) {
			std::unique_ptr<ArchiveControl> ptr;
			auto r = self->GetControl(&ptr, bForceCleanup);
			if (r != Success)
				throw ArchiverError(r, "Failed to get object!");
			return ptr.release();
		}

		ArchiveManage *GetManage(const char *lpszUser) {
			std::unique_ptr<ArchiveManage> ptr;
			auto r = self->GetManage(TO_LPTST(lpszUser), &ptr);
			if (r != Success)
				throw ArchiverError(r, "Failed to get object!");
			return ptr.release();
		}

		void AutoAttach(unsigned int ulFlags = ArchiveManage::Writable) {
			auto e = self->AutoAttach(ulFlags);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}
    }

private:
	Archiver();
};
