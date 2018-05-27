/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1996, 2017 Oracle and/or its affiliates.  All rights reserved.
 *
 * $Id$
 */

#ifndef _DB_GLOBALS_H_
#define	_DB_GLOBALS_H_

#if defined(__cplusplus)
extern "C" {
#endif

/*******************************************************
 * Global variables.
 *
 * Held in a single structure to minimize the name-space pollution.
 *******************************************************/
#ifdef HAVE_VXWORKS
#include "semLib.h"
#endif

struct __envq {
	struct __env *tqh_first;
	struct __env **tqh_last;
};

typedef struct __db_globals {
#ifdef HAVE_VXWORKS
	uint32 db_global_init;	/* VxWorks: inited */
	SEM_ID db_global_lock;		/* VxWorks: global semaphore */
#endif
#ifdef DB_WIN32
#ifndef DB_WINCE
	/*
	 * These fields are used by the Windows implementation of mutexes.
	 * Usually they are initialized by the first DB API call to lock a
	 * mutex. If that would result in the mutexes being inaccessible by
	 * other threads (e.g., ones which have lesser privileges) the
	 * application may first call db_env_set_win_security().
	 */
	SECURITY_DESCRIPTOR win_default_sec_desc;
	SECURITY_ATTRIBUTES win_default_sec_attr;
#endif
	SECURITY_ATTRIBUTES * win_sec_attr;
#endif
	/* TAILQ_HEAD(__envq, __dbenv) envq; */
	struct __envq envq;
	char *db_line;			/* DB display string. */
	char error_buf[40];		/* Error string buffer. */
	int random_seeded;		/* Has __os_srandom been called? */
#if defined(HAVE_RANDOM_R)
	struct random_data random_data;	/* srandom_r/random_r argument */
	char random_state[64];		/* random number state */
#elif !defined(HAVE_RAND) && !defined(HAVE_RANDOM)
	u_long rand_next;		/* next rand value for clib/rand.c */
#endif
	uint32 fid_serial;		/* file id counter */
	int db_errno;			/* Errno value if not available */
	char *saved_errstr;		/* saved error string from backup */
	char *time_format;		/* strftime-format for printing dates */
#if defined(HAVE_ERROR_HISTORY) && defined(HAVE_PTHREAD_SELF)
	pthread_key_t msgs_key;
	pthread_once_t thread_once;
#endif

	/* Underlying OS interface jump table.*/
	void	(*j_assert)(const char *, const char *, int);
	int	(*j_close)(int);
	void	(*j_dirfree)(char **, int);
	int	(*j_dirlist)(const char *, char ***, int *);
	int	(*j_exists)(const char *, int *);
	void	(*j_free)(void *);
	int	(*j_fsync)(int);
	int	(*j_ftruncate)(int, off_t);
	int	(*j_ioinfo)(const char *, int, uint32 *, uint32 *, uint32 *);
	void   *(*j_malloc)(size_t);
	int	(*j_file_map)(DB_ENV *, char *, size_t, int, void **);
	int	(*j_file_unmap)(DB_ENV *, void *);
	int	(*j_open)(const char *, int, ...);
	ssize_t	(*j_pread)(int, void *, size_t, off_t);
	ssize_t	(*j_pwrite)(int, const void *, size_t, off_t);
	ssize_t	(*j_read)(int, void *, size_t);
	void   *(*j_realloc)(void *, size_t);
	int	(*j_region_map)(DB_ENV *, char *, size_t, int *, void **);
	int	(*j_region_unmap)(DB_ENV *, void *);
	int	(*j_rename)(const char *, const char *);
	int	(*j_seek)(int, off_t, int);
	int	(*j_unlink)(const char *);
	ssize_t	(*j_write)(int, const void *, size_t);
	int	(*j_yield)(u_long, u_long);
} DB_GLOBALS;

extern	DB_GLOBALS	__db_global_values;
#define	DB_GLOBAL(v)	__db_global_values.v

#if defined(__cplusplus)
}
#endif
#endif /* !_DB_GLOBALS_H_ */
