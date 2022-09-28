/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1997-1999 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYMTAB_H
#define	_SYMTAB_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"


#ifdef	__cplusplus
extern "C" {
#endif

typedef struct entry_t ENTRY; /* Forward declaration */

extern void symtab_new_function(const int, const char *);
extern void symtab_clear_function(void);
extern void symtab_clear_includes(void);
extern void symtab_clear_errval(void);
extern void symtab_clear_exception(void);

/* Generated by m4 -- character string values */
extern void symtab_set_prototype(char *p);
extern char *symtab_get_prototype(void);
extern void symtab_set_formals(char *);
extern char *symtab_get_formals(void);
extern void symtab_set_actuals(char *);
extern char *symtab_get_actuals(void);
extern char *symtab_get_cast(void);
extern void symtab_set_cast(char *);
extern void symtab_set_filename(char const *);
extern char *symtab_get_filename(void);

extern int symtab_get_nonreturn(void);
extern void symtab_set_line(int);
extern int symtab_get_line(void);
extern void symtab_set_skip(int);
extern int symtab_get_skip(void);

/* Manual code */
extern void symtab_set_function(char *, int, char *, char *, char *, int);
extern ENTRY *symtab_get_function(void);

extern void symtab_set_exception(char *, int, char *);
extern ENTRY *symtab_get_exception(void);

extern void symtab_set_errval(char *, int, char *, char *, char *, int);
extern ENTRY *symtab_get_errval(void);

extern void symtab_add_args(char *, int, char *, char *, char *, int);
extern ENTRY *symtab_get_first_arg(void);
extern ENTRY *symtab_get_next_arg(void);
extern ENTRY *symtab_get_last_arg(void);

extern void symtab_add_varargs(char *, int, char *, char *, char *);
extern ENTRY *symtab_get_first_vararg(void);
extern ENTRY *symtab_get_next_vararg(void);
extern void symtab_print_varargs(void);

extern void symtab_add_globals(char *, int, char *, char *, char *, int);
extern ENTRY *symtab_get_first_global(void);
extern ENTRY *symtab_get_next_global(void);

extern void symtab_add_print_types(char *, char *);
extern char *symtab_get_first_print_type(void);
extern char *symtab_get_next_print_type(void);

extern void symtab_add_includes(char *);
extern char *symtab_get_first_include(void);
extern char *symtab_get_next_include(void);
extern void symtab_sort_includes(void);

/* ENTRYs */
extern char *name_of(ENTRY *);
extern int validity_of(ENTRY *);
extern int line_of(ENTRY *);
extern char *file_of(ENTRY *);
extern char *type_of(ENTRY *);
extern char *x_type_of(ENTRY *);
extern char *basetype_of(ENTRY *);
extern int levels_of(ENTRY *);
extern char *inverse_of(ENTRY *);
extern char *selector_of(ENTRY *);
extern int preuses_of(ENTRY *);
extern int postuses_of(ENTRY *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYMTAB_H */
