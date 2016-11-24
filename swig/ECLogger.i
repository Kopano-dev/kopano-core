// ECLogger director
%{
#include <kopano/ECLogger.h>

class IECSimpleLogger {
public:
	virtual ~IECSimpleLogger() {};
	virtual HRESULT Log(unsigned int loglevel, const char *szMessage) = 0;
};

#include <kopano/swig_iunknown.h>
typedef IUnknownImplementor<IECSimpleLogger> ECSimpleLogger;

class ECLoggerProxy : public KC::ECLogger {
public:
	static HRESULT Create(unsigned int ulLevel, ECSimpleLogger *lpSimpleLogger, ECLoggerProxy **lppProxy) {
		ECLoggerProxy *lpProxy = new ECLoggerProxy(ulLevel, lpSimpleLogger);
		//lpProxy->AddRef();
		*lppProxy = lpProxy;
		return hrSuccess;
	}

	~ECLoggerProxy() {
		if (m_lpLogger)
			m_lpLogger->Release();
	};

	virtual void Reset() { };
	virtual void Log(unsigned int loglevel, const std::string &message) { Log(loglevel, "%s", message.c_str()); };
	virtual void Log(unsigned int Loglevel, const char *format, ...) __LIKE_PRINTF(3, 4) { 
		va_list va;

		va_start(va, format);
		LogVA(Loglevel, format, va);
		va_end(va);
	};
	virtual void LogVA(unsigned int loglevel, const char *format, va_list& va) {
		if (m_lpLogger) {
			char buf[4096];
			vsnprintf(buf, sizeof(buf), format, va);
			m_lpLogger->Log(loglevel, buf);
		}
	};

private:
	ECLoggerProxy(unsigned int ulLevel, ECSimpleLogger *lpSimpleLogger) : ECLogger(ulLevel), m_lpLogger(lpSimpleLogger) {
		if (m_lpLogger)
			m_lpLogger->AddRef();
	};
	
	ECSimpleLogger *m_lpLogger;
};

%}

// Directors for ECLogger

%feature("director") ECSimpleLogger;
class ECSimpleLogger : public IECSimpleLogger{
public:
	virtual HRESULT Log(unsigned int loglevel, const char *szMessage) = 0;
};

%include "cstring.i"
%include "cwstring.i"

class ECLogger {
public:
    virtual bool Log(unsigned int loglevel) = 0;
    virtual void Reset() = 0;
    virtual int GetFileDescriptor() = 0;

    %extend {
        ~ECLogger() { self->Release(); }

        void Log(unsigned int loglevel, const char *szMessage) {
            self->Log(loglevel, "%s", szMessage);
        }
    }

};

