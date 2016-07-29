#ifndef LOCAL_HXTIME_H
#define LOCAL_HXTIME_H 1

#ifdef __cplusplus
extern "C" {
#endif

extern struct timespec *HX_timespec_neg(struct timespec *, const struct timespec *);
extern struct timespec *HX_timespec_add(struct timespec *, const struct timespec *, const struct timespec *);
extern struct timespec *HX_timespec_sub(struct timespec *, const struct timespec *, const struct timespec *);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOCAL_HXTIME_H */
