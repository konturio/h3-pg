/*
 * Copyright 2020-2022 Bytes & Brains
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <postgres.h>		 // Datum, etc.
#include <fmgr.h>			 // PG_FUNCTION_ARGS, etc.

#include <h3api.h> // Main H3 include
#include "extension.h"

PGDLLEXPORT PG_FUNCTION_INFO_V1(h3index_distance);

/* b-tree */
PGDLLEXPORT PG_FUNCTION_INFO_V1(h3index_eq);
PGDLLEXPORT PG_FUNCTION_INFO_V1(h3index_ne);
PGDLLEXPORT PG_FUNCTION_INFO_V1(h3index_lt);
PGDLLEXPORT PG_FUNCTION_INFO_V1(h3index_le);
PGDLLEXPORT PG_FUNCTION_INFO_V1(h3index_gt);
PGDLLEXPORT PG_FUNCTION_INFO_V1(h3index_ge);

/* r-tree */
PGDLLEXPORT PG_FUNCTION_INFO_V1(h3index_overlaps);
PGDLLEXPORT PG_FUNCTION_INFO_V1(h3index_contains);
PGDLLEXPORT PG_FUNCTION_INFO_V1(h3index_contained_by);

/* static helpers */
static int
containment(H3Index a, H3Index b)
{
	H3Error		error = 0;
	H3Index		aParent = a;
	H3Index		bParent = b;
	int			aRes = getResolution(a);
	int			bRes = getResolution(b);

	if (aRes > bRes)
		error = cellToParent(a, bRes, &aParent);
	else if (aRes < bRes)
		error = cellToParent(b, aRes, &bParent);

	H3_ERROR(error, "cellToParent");

	/* a contains b */
	if (a == bParent)
		return 1;

	/* a contained by b */
	if (b == aParent)
		return -1;

	/* no overlap */
	return 0;
}

/* distance operator allowing for different resolutions */
Datum
h3index_distance(PG_FUNCTION_ARGS)
{
	H3Index		a = PG_GETARG_H3INDEX(0);
	H3Index		b = PG_GETARG_H3INDEX(1);
	int			resA = getResolution(a);
	int			resB = getResolution(b);
	H3Error		error;
	int64_t		distance;

	if (resA < resB)
	{
		error = cellToCenterChild(a, resB, &a);
		H3_ERROR(error, "cellToCenterChild");
	}

	if (resB < resA)
	{
		error = cellToCenterChild(b, resA, &b);
		H3_ERROR(error, "cellToCenterChild");
	}

	error = gridDistance(a, b, &distance);
	/* H3_ERROR(error, "gridDistance"); */
	if (error)
		distance = -1;

	PG_RETURN_INT64(distance);
}

/* b-tree operators */
Datum
h3index_eq(PG_FUNCTION_ARGS)
{
	H3Index		a = PG_GETARG_H3INDEX(0);
	H3Index		b = PG_GETARG_H3INDEX(1);
	bool		ret = a == b;

	PG_RETURN_BOOL(ret);
}

Datum
h3index_ne(PG_FUNCTION_ARGS)
{
	H3Index		a = PG_GETARG_H3INDEX(0);
	H3Index		b = PG_GETARG_H3INDEX(1);
	bool		ret = a != b;

	PG_RETURN_BOOL(ret);
}

Datum
h3index_lt(PG_FUNCTION_ARGS)
{
	H3Index		a = PG_GETARG_H3INDEX(0);
	H3Index		b = PG_GETARG_H3INDEX(1);
	bool		ret = a < b;

	PG_RETURN_BOOL(ret);
}

Datum
h3index_le(PG_FUNCTION_ARGS)
{
	H3Index		a = PG_GETARG_H3INDEX(0);
	H3Index		b = PG_GETARG_H3INDEX(1);
	bool		ret = a <= b;

	PG_RETURN_BOOL(ret);
}

Datum
h3index_gt(PG_FUNCTION_ARGS)
{
	H3Index		a = PG_GETARG_H3INDEX(0);
	H3Index		b = PG_GETARG_H3INDEX(1);
	bool		ret = a > b;

	PG_RETURN_BOOL(ret);
}

Datum
h3index_ge(PG_FUNCTION_ARGS)
{
	H3Index		a = PG_GETARG_H3INDEX(0);
	H3Index		b = PG_GETARG_H3INDEX(1);
	bool		ret = a >= b;

	PG_RETURN_BOOL(ret);
}

/* r-tree operators */
Datum
h3index_overlaps(PG_FUNCTION_ARGS)
{
	H3Index		a = PG_GETARG_H3INDEX(0);
	H3Index		b = PG_GETARG_H3INDEX(1);
	bool		ret = containment(a, b) != 0;

	PG_RETURN_BOOL(ret);
}

Datum
h3index_contains(PG_FUNCTION_ARGS)
{
	H3Index		a = PG_GETARG_H3INDEX(0);
	H3Index		b = PG_GETARG_H3INDEX(1);
	bool		ret = containment(a, b) > 0;

	PG_RETURN_BOOL(ret);
}

Datum
h3index_contained_by(PG_FUNCTION_ARGS)
{
	H3Index		a = PG_GETARG_H3INDEX(0);
	H3Index		b = PG_GETARG_H3INDEX(1);
	bool		ret = containment(a, b) < 0;

	PG_RETURN_BOOL(ret);
}
