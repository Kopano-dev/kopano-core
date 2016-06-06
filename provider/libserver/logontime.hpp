#ifndef KCSRV_LOGONTIME_H
#define KCSRV_LOGONTIME_H 1

class ECDatabase;
class ECSession;

namespace kcsrv {

extern void sync_logon_times(ECDatabase *);
extern void record_logon_time(ECSession *, bool);

}

#endif /* KCSRV_LOGONTIME_H */
