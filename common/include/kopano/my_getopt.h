#ifndef MY_GETOPT_H_INCLUDED
#define MY_GETOPT_H_INCLUDED

#include <kopano/zcdefs.h>
#include <getopt.h>

#ifdef __cplusplus
extern "C" {
#endif

struct option;

/* Permit unknown long options, move them to end of argv like arguments */
extern _kc_export int my_getopt_long_permissive(int, char **, const char *, const struct option *, int *);

#ifdef __cplusplus
}
#endif

#endif /* MY_GETOPT_H_INCLUDED */
