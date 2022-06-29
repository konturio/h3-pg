/*
 * Copyright 2018-2020 Bytes & Brains
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
#include <utils/geo_decls.h> // making native points

#include <h3api.h> // Main H3 include
#include "extension.h"
#include <math.h>
#include "wkb.h"

PG_FUNCTION_INFO_V1(h3_geo_to_h3);
PG_FUNCTION_INFO_V1(h3_to_geo);
PG_FUNCTION_INFO_V1(h3_to_geo_boundary);

static bytea* geo_boundary_to_wkb_split(GeoBoundary *boundary, bool extend);  /* TODO: remove this function */
static void geo_boundary_to_degs(GeoBoundary *boundary, bool extend);
static bool geo_boundary_crosses_180(const GeoBoundary *boundary);
static void geo_boundary_split_180(const GeoBoundary *boundary, GeoBoundary *left, GeoBoundary *right);

/* Indexes the location at the specified resolution */
Datum
h3_geo_to_h3(PG_FUNCTION_ARGS)
{
	Point	   *geo = PG_GETARG_POINT_P(0);
	int			resolution = PG_GETARG_INT32(1);

	H3Index		idx;
	GeoCoord	location;

	if (h3_guc_strict)
	{
		ASSERT_EXTERNAL(geo->x >= -180 && geo->x <= 180, "Longitude must be between -180 and 180 degrees inclusive, but got %f.", geo->x);
		ASSERT_EXTERNAL(geo->y >= -90 && geo->y <= 90, "Latitude must be between -90 and 90 degrees inclusive, but got %f.", geo->y);
	}

	location.lon = degsToRads(geo->x);
	location.lat = degsToRads(geo->y);

	idx = geoToH3(&location, resolution);
	ASSERT_EXTERNAL(idx, "Indexing failed at specified resolution.");

	PG_FREE_IF_COPY(geo, 0);
	PG_RETURN_H3INDEX(idx);
}

/* Finds the centroid of the index */
Datum
h3_to_geo(PG_FUNCTION_ARGS)
{
	H3Index		idx = PG_GETARG_H3INDEX(0);

	Point	   *geo = palloc(sizeof(Point));
	GeoCoord	center;

	h3ToGeo(idx, &center);

	geo->x = radsToDegs(center.lon);
	geo->y = radsToDegs(center.lat);

	PG_RETURN_POINT_P(geo);
}

/* Finds the boundary of the index */
Datum
h3_to_geo_boundary(PG_FUNCTION_ARGS)
{
	H3Index		idx = PG_GETARG_H3INDEX(0);
	bool		extend = PG_GETARG_BOOL(1);
	GeoBoundary boundary;

	h3ToGeoBoundary(idx, &boundary);

	PG_RETURN_BYTEA_P(geo_boundary_to_wkb_split(&boundary, extend));
}

bytea* geo_boundary_to_wkb_split(GeoBoundary *boundary, bool extend)
{
	GeoBoundary left, right;
	const GeoBoundary *boundaries[2] = {&left, &right};

    geo_boundary_to_degs(boundary, extend);

	if (!geo_boundary_crosses_180(boundary))
		return geo_boundary_to_wkb(boundary);

	geo_boundary_split_180(boundary, &left, &right);
	return geo_boundary_array_to_wkb(boundaries, 2);
}

void geo_boundary_to_degs(GeoBoundary *boundary, bool extend)
{
	GeoCoord* verts = boundary->verts;
	int numVerts = boundary->numVerts;
	double firstLon, delta;

	ASSERT_EXTERNAL(numVerts > 0, "GeoBoundary must not be empty");

	firstLon = verts[0].lon;
	if (firstLon < 0)
	{
		delta = -2 * M_PI;
	}
	else
	{
		delta = +2 * M_PI;
	}

	for (int v = 0; v < numVerts; v++)
	{
		double lon = verts[v].lon;
		double lat = verts[v].lat;

		/* check if different sign */
		if (extend && fabs(lon - firstLon) > M_PI)
			lon = lon + delta;

		verts[v].lon = radsToDegs(lon);
		verts[v].lat = radsToDegs(lat);
	}
}

bool geo_boundary_crosses_180(const GeoBoundary *boundary)
{
	const int numVerts = boundary->numVerts;
	const GeoCoord *verts = boundary->verts;

	int prevPos = 0; /* previous non-zero position relative to 180 (-1/0/1) */
	for (int v = 0; v <= numVerts; v++)
	{
		int cur = v % numVerts;
		double lon = verts[cur].lon;
		double lonAdj = (lon < 0) ? lon + 360 : lon; /* adjust to [0..360) */
		int pos = (lonAdj < 180) ? -1 : (180 < lonAdj) ? 1 : 0; /* compare with tolerance? */

		if (prevPos == 0)
		{
			prevPos = pos;
		}
		else if (pos != 0 && prevPos != pos)
		{
			int prev = (v + numVerts - 1) % numVerts;
			double prevLon = verts[prev].lon;
			double prevLonAdj = (prevLon < 0) ? prevLon + 360 : prevLon;
			if (fabs(lonAdj - prevLonAdj) < 180)
				return true; /* crossing 180 meridian */
			else
				prevPos = pos;
		}
	}
	return false;
}

void geo_boundary_split_180(const GeoBoundary *boundary, GeoBoundary *left, GeoBoundary *right)
{
	const int numVerts = boundary->numVerts;
	const GeoCoord *verts = boundary->verts;

	GeoBoundary *half, *prevHalf;
	GeoCoord middle; /* cut point */
	int prevPos = 0; /* position of points up to current one relative to 180, -1/0/1 */
	int start = 0; /* current batch start */
	left->numVerts = 0;
	right->numVerts = 0;
	for (int v = 0; v <= numVerts; v++)
	{
		int cur = v % numVerts;
		double lon = boundary->verts[cur].lon;
		double lonAdj = (lon < 0) ? lon + 360 : lon; /* adjust to [0..360) */
		int pos = (lonAdj < 180) ? -1 : (180 < lonAdj) ? 1 : 0; /* position of current point */

		if (prevPos != 0 && pos != 0 && pos != prevPos)
		{
			/* Switched from [0..180] to [180..360] lon or vice versa */
			/*
			NOTE: This function is only supposed to be called on boundaries crossing
			180th meridian at least once (see `geo_boundary_crosses_180').
			Here we do not check if 180th or zero meridian (for boundaries around
			the poles) is crossed.
			*/

			int prev = (v + numVerts - 1) % numVerts;
			double prevLon = boundary->verts[prev].lon;
			double prevLonAdj = (prevLon < 0) ? prevLon + 360 : prevLon; /* adjust to [0..360) */
			bool crossesZero = (fabs(lonAdj - prevLonAdj) > 180); /* crossing zero or 180 meridian? */

			prevHalf = (prevPos < 0) ? left : right;
			half = (pos < 0) ? left : right;

			/* Add points to prev half */
			for (int i = start; i < v && i < numVerts; i++)
				prevHalf->verts[prevHalf->numVerts++] = verts[i];

			/* Calc. cut point latitude */
			middle.lat = verts[cur].lat
				- (verts[cur].lat - verts[prev].lat)
				* (lonAdj - 180) / (lonAdj - prevLonAdj);

			/* Add cut point */
			/* prev half */
			middle.lon = crossesZero ? 0 : ((prevHalf == right) ? -180 : 180);
			if (prevLonAdj != middle.lon) /* compare with tolerance? */
				prevHalf->verts[prevHalf->numVerts++] = middle;
			/* current half */
			middle.lon = crossesZero ? 0 : ((half == right) ? -180 : 180);
			half->verts[half->numVerts++] = middle;

			start = v; /* start next batch from current point */
		}

		if (pos != 0)
			prevPos = pos;
	}

	/* Add remaining points */
	half = (prevPos < 0) ? left : right;
	for (int i = start; i < numVerts; i++)
		half->verts[half->numVerts++] = verts[i];
}
