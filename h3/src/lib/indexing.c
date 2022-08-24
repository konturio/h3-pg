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
#include "vect3.h"
#include "wkb.h"

#define SIGN(x) ((x < 0) ? -1 : (x > 0) ? 1 : 0)

PG_FUNCTION_INFO_V1(h3_geo_to_h3);
PG_FUNCTION_INFO_V1(h3_to_geo);
PG_FUNCTION_INFO_V1(h3_to_geo_boundary);

/* Coverts GeoBoundary coordinates to degrees in place */
static void
geo_boundary_to_degs(GeoBoundary *boundary);

/* Checks if GeoBoundary is crossed by antimeridian */
static bool
geo_boundary_crosses_180(const GeoBoundary *boundary);

/* Splits GeoBoundary by antimeridian (and 0 meridian around poles) */
static void
geo_boundary_split_180(const GeoBoundary *boundary, GeoBoundary *left, GeoBoundary *right);

/* Calculates latitude of intersection point between segment and antimeridian or 0 meridian */
static double
split_180_lat(const GeoCoord *coord1, const GeoCoord *coord2);


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

/* Finds the boundary of the index, returns EWKB, optionally split the boundary by 180 meridian */
Datum
h3_to_geo_boundary(PG_FUNCTION_ARGS)
{
	H3Index		idx = PG_GETARG_H3INDEX(0);
    bool		split = PG_GETARG_BOOL(1);
	GeoBoundary boundary;
	bytea		*ewkb;

	h3ToGeoBoundary(idx, &boundary);

	if (split && geo_boundary_crosses_180(&boundary))
	{
		GeoBoundary boundaries[2];
		geo_boundary_split_180(&boundary, &boundaries[0], &boundaries[1]);

		geo_boundary_to_degs(&boundaries[0]);
		geo_boundary_to_degs(&boundaries[1]);
		ewkb = geo_boundary_array_to_wkb(boundaries, 2);
	}
	else
	{
        geo_boundary_to_degs(&boundary);
		ewkb = geo_boundary_to_wkb(&boundary);
	}

	PG_RETURN_BYTEA_P(ewkb);
}

void
geo_boundary_to_degs(GeoBoundary *boundary)
{
	GeoCoord* verts = boundary->verts;
	const int numVerts = boundary->numVerts;

	ASSERT_EXTERNAL(numVerts > 0, "GeoBoundary must not be empty");

	for (int v = 0; v < numVerts; v++)
	{
		verts[v].lon = radsToDegs(verts[v].lon);
		verts[v].lat = radsToDegs(verts[v].lat);
	}
}

bool
geo_boundary_crosses_180(const GeoBoundary *boundary)
{
	const int numVerts = boundary->numVerts;
	const GeoCoord *verts = boundary->verts;

	int prevSign = 0;
	for (int v = 0; v <= numVerts; v++)
	{
		int cur = v % numVerts;
		double lon = verts[cur].lon;
        int sign = SIGN(lon);

		if (prevSign == 0)
		{
			prevSign = sign;
		}
		else if (sign != 0 && prevSign != sign)
		{
			int prev = (v + numVerts - 1) % numVerts;
			double prevLon = verts[prev].lon;

			if (fabs(lon) + fabs(prevLon) > M_PI)
				return true;
			else
				prevSign = sign;
		}
	}
	return false;
}

void
geo_boundary_split_180(const GeoBoundary *boundary, GeoBoundary *left, GeoBoundary *right)
{
	const int numVerts = boundary->numVerts;
	const GeoCoord *verts = boundary->verts;

	GeoBoundary *half, *prevHalf;
	GeoCoord split;
	int prevSign = 0;
	int start = 0; /* current batch start */
	left->numVerts = 0;
	right->numVerts = 0;
	for (int v = 0; v <= numVerts; v++)
	{
		int cur = v % numVerts;
		double lon = verts[cur].lon;
		int sign = SIGN(lon);

		if (prevSign != 0 && sign != 0 && sign != prevSign)
		{
			/* Crossing 0 or 180 meridian */
			/*
				Assuming boundary is crossed by 180 meridian at least once,
				so segment has to be split by either anti or 0 meridian
			*/

			int prev = (v + numVerts - 1) % numVerts;
			double prevLon = verts[prev].lon;
			bool crossesZero = (fabs(lon) + fabs(prevLon) < M_PI);

			prevHalf = (prevSign < 0) ? left : right;
			half = (sign < 0) ? left : right;

			/* Add points to prev. half */
			for (int i = start; i < v && i < numVerts; i++)
				prevHalf->verts[prevHalf->numVerts++] = verts[i];

			/* Calc. split point latitude */
			split.lat = split_180_lat(&verts[cur], &verts[prev]);

			/* Add split point */
			/* prev. half */
			split.lon = crossesZero ? 0 : (prevLon < 0) ? -M_PI : M_PI;;
			prevHalf->verts[prevHalf->numVerts++] = split;
			/* current half */
			split.lon = crossesZero ? 0 : -split.lon;
			half->verts[half->numVerts++] = split;

			start = v; /* start next batch from current point */
		}

		if (sign != 0)
			prevSign = sign;
	}

	/* Add remaining points */
	half = (prevSign < 0) ? left : right;
	for (int i = start; i < numVerts; i++)
		half->verts[half->numVerts++] = verts[i];
}

double
split_180_lat(const GeoCoord *coord1, const GeoCoord *coord2)
{
	Vect3 p1, p2, normal, s;
    double y;

	/* Normal of circle containing points: normal = p1 x p2 */
	vect3_from_geo_coord(coord1, &p1);
	vect3_from_geo_coord(coord2, &p2);
	vect3_cross(&p1, &p2, &normal);

	/* y coordinate of 0/180 meridian circle normal */
	y = (coord1->lon < 0 || coord2->lon > 0) ? -1 : 1;

	/* Circle plane intersection vector: s = (p1 x p2) x {0, y, 0} */
	s.x = -(normal.z * y);
	s.y = 0;
	s.z = normal.x * y;
	vect3_normalize(&s); /* intersection point coordinates on unit sphere */

    return asin(s.z); /* latitude */
}
