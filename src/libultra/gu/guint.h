/**************************************************************************
 *									  *
 *		 Copyright (C) 1994, Silicon Graphics, Inc.		  *
 *									  *
 *  These coded instructions, statements, and computer programs  contain  *
 *  unpublished  proprietary  information of Silicon Graphics, Inc., and  *
 *  are protected by Federal copyright law.  They  may  not be disclosed  *
 *  to  third  parties  or copied or duplicated in any form, in whole or  *
 *  in part, without the prior written consent of Silicon Graphics, Inc.  *
 *									  *
 **************************************************************************/

#include "mbi.h"
#include "gu.h"

typedef union
{
	struct
	{
#ifdef __sgi
		unsigned int hi;
		unsigned int lo;
#else
		/* the {hi, lo} initializers throughout gu assume big-endian
		 * double word order; swap the members so the same constant
		 * tables build correct doubles natively */
		unsigned int lo;
		unsigned int hi;
#endif
	} word;

	double	d;
} du;

/* constant-table entry: written (hi, lo) everywhere; expands to match the
 * member order above on each side */
#ifdef __sgi
#define DU(h, l) {h, l}
#else
#define DU(h, l) {l, h}
#endif

typedef union
{
	unsigned int	i;
	float		f;
} fu;

#ifndef __GL_GL_H__

typedef	float	Matrix[4][4];

#endif

#define ROUND(d)	(int)(((d) >= 0.0) ? ((d) + 0.5) : ((d) - 0.5))
#define	ABS(d)		((d) > 0) ? (d) : -(d)

extern float	__libm_qnan_f;
