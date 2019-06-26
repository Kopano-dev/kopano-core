#ifndef KC_DOVESTORE_H
#define KC_DOVESTORE_H 1

/* Dovecot's bad header file quality requires an extra separation layer. >:-( */

#ifdef __cplusplus
extern "C" {
#endif

extern void *kpxx_login(void);
extern void kpxx_logout(void *);
extern void *kpxx_store_get(void *);
extern void kpxx_store_put(void *);
extern char **kpxx_hierarchy_list(void *);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KC_DOVEAUTH_H */
