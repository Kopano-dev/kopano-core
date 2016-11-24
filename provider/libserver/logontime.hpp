#ifndef KCSRV_LOGONTIME_H
#define KCSRV_LOGONTIME_H 1

namespace KC {

class ECDatabase;
class ECSession;

extern void sync_logon_times(ECDatabase *);
extern void record_logon_time(ECSession *, bool);

} /* namespace */

#endif /* KCSRV_LOGONTIME_H */
