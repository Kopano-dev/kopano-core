#ifndef KC_DOVEAUTH_H
#define KC_DOVEAUTH_H 1

/* Dovecot's bad header file quality requires an extra separation layer. >:-( */

#ifdef __cplusplus
extern "C" {
#endif

extern void auth_request_log_debugcc(void *rq, const char *text);
extern int authdb_mapi_logonxx(void *rq, const char *user, const char *pass);
extern void authdb_mapi_initxx(void);
extern void authdb_mapi_deinitxx(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KC_DOVEAUTH_H */
