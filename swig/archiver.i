/* SPDX-License-Identifier: AGPL-3.0-only */
/* File : archiver.i */
%module archiver

%{
	#include <kopano/zcdefs.h>
	#include <stdexcept>
	#include "../../ECtools/archiver/Archiver.h"
	#include <kopano/charset/convert.h>
	#define TO_LPTST(s) ((s) ? converter.convert_to<LPTSTR>(s) : NULL)
	using namespace KC;

	class _kc_export_throw ArchiverError : public std::runtime_error {
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
			eResult e = Success;

			e = self->ArchiveAll(bLocalOnly, bAutoAttach, ulFlags);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}
		
		void Archive(const char *lpszUser, bool bAutoAttach = false, unsigned int ulFlags = ArchiveManage::Writable) {
			eResult e = Success;
			convert_context converter;

			e = self->Archive(TO_LPTST(lpszUser), bAutoAttach, ulFlags);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}

		void CleanupAll(bool bLocalOnly) {
			eResult e = Success;

			e = self->CleanupAll(bLocalOnly);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}
		
		void Cleanup(const char *lpszUser) {
			eResult e = Success;
			convert_context converter;

			e = self->Cleanup(TO_LPTST(lpszUser));
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
			eResult e = Success;
			convert_context converter;

			e = self->AttachTo(lpszArchiveServer, TO_LPTST(lpszArchive), TO_LPTST(lpszFolder), ulFlags);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}
		
		void DetachFrom(const char *lpszArchiveServer, const char *lpszArchive, const char *lpszFolder) {
			eResult e = Success;
			convert_context converter;

			e = self->DetachFrom(lpszArchiveServer, TO_LPTST(lpszArchive), TO_LPTST(lpszFolder));
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}

		void DetachFrom(unsigned int ulArchive) {
			eResult e = Success;

			e = self->DetachFrom(ulArchive);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}

		ArchiveList ListArchives(const char *lpszIpmSubtreeSubstitude = NULL) {
			eResult e = Success;
			ArchiveList lst;

			e = self->ListArchives(&lst);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");

			return lst;
		}

		UserList ListAttachedUsers() {
			eResult e = Success;
			UserList lst;

			e = self->ListAttachedUsers(&lst);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");

			return lst;
		}

		void AutoAttach(unsigned int ulFlags = ArchiveManage::Writable) {
			eResult e = Success;

			e = self->AutoAttach(ulFlags);
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
			eResult r = Success;
			std::unique_ptr<Archiver> ptr;

			r = Archiver::Create(&ptr);
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
			eResult r = Success;
			ArchiveControlPtr ptr;

			r = self->GetControl(&ptr, bForceCleanup);
			if (r != Success)
				throw ArchiverError(r, "Failed to get object!");

			return ptr.release();
		}
		
		ArchiveManage *GetManage(const char *lpszUser) {
			eResult r = Success;
			ArchiveManagePtr ptr;
			convert_context converter;

			r = self->GetManage(TO_LPTST(lpszUser), &ptr);
			if (r != Success)
				throw ArchiverError(r, "Failed to get object!");

			return ptr.release();
		}

		void AutoAttach(unsigned int ulFlags = ArchiveManage::Writable) {
			eResult e = Success;

			e = self->AutoAttach(ulFlags);
			if (e != Success)
				throw ArchiverError(e, "Method returned an error!");
		}
    }

private:
	Archiver();
};
