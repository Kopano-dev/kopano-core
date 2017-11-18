#ifndef KC_DOVESTORE_H
#define KC_DOVESTORE_H 1

/* Dovecot's bad header file quality requires an extra separation layer. >:-( */

#ifdef __cplusplus
extern "C" {
#endif

extern void *kpxx_login(void);
extern void kpxx_logout(void *);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KC_DOVEAUTH_H */
