#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Code corresponding to smb_apple.h
 * XXX: Could merge this into smb_subr.h
 * as long as that doesn't break smbfs
 */

#ifndef _NETSMB_SMB_OSDEP_H_
#define	_NETSMB_SMB_OSDEP_H_

#ifndef PRIVSYM
#define PRIVSYM 
#endif

#ifndef min
#define	min(a, b)	(((a) < (b)) ? (a) : (b))
#endif

#define	CAST_DOWN(type, addr)  (((type)((uintptr_t)(addr))))
#define	USER_ADDR_NULL  ((user_addr_t)0)
#define	CAST_USER_ADDR_T(a_ptr)   ((user_addr_t)(a_ptr))

/*
 * flags to (BSD) malloc
 */
#define	M_WAITOK	0x0000
#define	M_NOWAIT	0x0001
#define	M_ZERO		0x0004		/* bzero the allocation */

/* Iconv stuff */

/*
 * Some UTF Related stuff. Will be deleting this once compiled and using
 * ienup's code.
 */
/*
 * UTF-8 encode/decode flags
 */
#define	UTF_REVERSE_ENDIAN	0x01    /* reverse UCS-2 byte order */
#define	UTF_NO_NULL_TERM	0x02    /* do not add null termination */
#define	UTF_DECOMPOSED		0x04    /* generate fully decomposed UCS-2 */
#define	UTF_PRECOMPOSED		0x08    /* generate precomposed UCS-2 */

/*
 * These are actually included in sunddi.h. I am getting compilation
 * errors right now. Adding the induvidual defines here again from sunddi.h
 * Unicode encoding conversion functions and their macros.
 */
#define	UCONV_IN_BIG_ENDIAN		0x0001
#define	UCONV_OUT_BIG_ENDIAN		0x0002
#define	UCONV_IN_SYSTEM_ENDIAN		0x0004
#define	UCONV_OUT_SYSTEM_ENDIAN		0x0008
#define	UCONV_IN_LITTLE_ENDIAN		0x0010
#define	UCONV_OUT_LITTLE_ENDIAN		0x0020
#define	UCONV_IGNORE_NULL		0x0040
#define	UCONV_IN_ACCEPT_BOM		0x0080
#define	UCONV_OUT_EMIT_BOM		0x0100

extern int uconv_u8tou16(const uchar_t *, size_t *, uint16_t *, size_t *, int);

/* Legacy type names for Solaris. */
typedef uint64_t u_int64_t;
typedef uint32_t u_int32_t;
typedef uint16_t u_int16_t;
typedef uint8_t u_int8_t;

typedef const char * c_caddr_t;
typedef uint64_t 	user_addr_t;

/*
 * Time related calls.
 */

/* BEGIN CSTYLED */
#define	timespeccmp(tvp, uvp, cmp)                                      \
	(((tvp)->tv_sec == (uvp)->tv_sec) ?                             \
	((tvp)->tv_nsec cmp (uvp)->tv_nsec) :		               	\
	((tvp)->tv_sec cmp (uvp)->tv_sec))
/* END CSTYLED */

#define	timespecadd(vvp, uvp)						\
	{								\
		(vvp)->tv_sec += (uvp)->tv_sec;				\
		(vvp)->tv_nsec += (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec >= 1000000000) {			\
			(vvp)->tv_sec++;				\
			(vvp)->tv_nsec -= 1000000000;			\
		}							\
	}

#define	timespecsub(vvp, uvp)                                           \
	{								\
		(vvp)->tv_sec -= (uvp)->tv_sec;				\
		(vvp)->tv_nsec -= (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_nsec += 1000000000;			\
		}							\
	}

#endif /* _NETSMB_SMB_OSDEP_H_ */
