#include "wkb.h"
#include <stddef.h>
#include <string.h>
#include "extension.h"

#define WKB_BYTE_SIZE 1
#define WKB_INT_SIZE 4
#define WKB_DOUBLE_SIZE 8

#define WKB_NDR 1
#define WKB_XDR 0

#define WKB_POLYGON_TYPE 3
#define WKB_MULTIPOLYGON_TYPE 6

#define WKB_SRID_FLAG 0x20000000

#define WKB_SRID_DEFAULT 4326

#define ASSERT_WKB_DATA_WRITTEN(wkb, data) \
	ASSERT_EXTERNAL( \
		(uint8 *)wkb + VARSIZE(wkb) == data, \
		"# of written bytes (%d) must match allocation size (%d)",	\
		(int)(data - (uint8 *)wkb), VARSIZE(wkb))

static bool geo_boundary_is_empty(const GeoBoundary *boundary);
static bool geo_boundary_is_closed(const GeoBoundary *boundary);

static size_t geo_boundary_array_data_size(const GeoBoundary **boundaries, int num);
static size_t geo_boundary_data_size(const GeoBoundary *boundary);

static uint8* wkb_write_geo_boundary_array_data(uint8 *data, const GeoBoundary **boundaries, int num);
static uint8* wkb_write_geo_boundary_data(uint8 *data, const GeoBoundary *boundary);
static uint8* wkb_write_geo_coord_array(uint8 *data, const GeoCoord *coord, int num);
static uint8* wkb_write_geo_coord(uint8 *data, const GeoCoord *coord);

static uint8* wkb_write_endian(uint8 *data);
static uint8* wkb_write_int(uint8 *data, uint32 value);
static uint8* wkb_write(uint8 *data, const void *value, size_t size);

bytea* geo_boundary_array_to_wkb(const GeoBoundary **boundaries, size_t num)
{
	uint8 *data;
	bytea *wkb;
	size_t size = geo_boundary_array_data_size(boundaries, num);

	wkb = palloc(VARHDRSZ + size);
	SET_VARSIZE(wkb, VARHDRSZ + size);

	data = (uint8 *)VARDATA(wkb);
	data = wkb_write_geo_boundary_array_data(data, boundaries, num);

	ASSERT_WKB_DATA_WRITTEN(wkb, data);
	return wkb;
}

bytea* geo_boundary_to_wkb(const GeoBoundary *boundary)
{
	bytea *wkb;
	uint8 *data;
	size_t size = geo_boundary_data_size(boundary);

	wkb = palloc(VARHDRSZ + size);
	SET_VARSIZE(wkb, VARHDRSZ + size);

	data = (uint8 *)VARDATA(wkb);
	data = wkb_write_geo_boundary_data(data, boundary);

	ASSERT_WKB_DATA_WRITTEN(wkb, data);
	return wkb;
}

bool geo_boundary_is_empty(const GeoBoundary *boundary)
{
	return boundary->numVerts < 1;
}

bool geo_boundary_is_closed(const GeoBoundary *boundary)
{
	const GeoCoord *verts;
	int numVerts;

	if (geo_boundary_is_empty(boundary))
		return true;

	verts = boundary->verts;
	numVerts = boundary->numVerts;
	return verts[0].lon == verts[numVerts - 1].lon
		&& verts[1].lat == verts[numVerts - 1].lat;
}

size_t geo_boundary_array_data_size(const GeoBoundary **boundaries, int num)
{
	/* byte order + type + # of polygons */
	size_t size = WKB_BYTE_SIZE + WKB_INT_SIZE * 2;
	for (int i = 0; i < num; i++)
		size += geo_boundary_data_size(boundaries[i]);
	return size;
}

size_t geo_boundary_data_size(const GeoBoundary *boundary)
{
	/* byte order + type + srid + # of rings */
	size_t size = WKB_BYTE_SIZE + WKB_INT_SIZE * 3;
	if (!geo_boundary_is_empty(boundary))
	{
		int numVerts = boundary->numVerts;
		if (!geo_boundary_is_closed(boundary))
			numVerts++;
		/* # of points, point data */
		size += WKB_INT_SIZE + numVerts * WKB_DOUBLE_SIZE * 2;
	}
	return size;
}

uint8* wkb_write_geo_boundary_array_data(uint8 *data, const GeoBoundary **boundaries, int num)
{
	/* byte order */
	data = wkb_write_endian(data);
	/* type */
	data = wkb_write_int(data, WKB_MULTIPOLYGON_TYPE);
	/* # of polygons */
	data = wkb_write_int(data, num);
	for (int i = 0; i < num; i++)
		data = wkb_write_geo_boundary_data(data, boundaries[i]);
	return data;
}

uint8* wkb_write_geo_boundary_data(uint8 *data, const GeoBoundary *boundary)
{
	/* byte order */
	data = wkb_write_endian(data);
	/* type */
	data = wkb_write_int(data, WKB_POLYGON_TYPE | WKB_SRID_FLAG);
	/* SRID */
	data = wkb_write_int(data, WKB_SRID_DEFAULT);
	/* # of rings */
	data = wkb_write_int(data, geo_boundary_is_empty(boundary) ? 0 : 1);
	if (!geo_boundary_is_empty(boundary))
	{
		bool is_closed = geo_boundary_is_closed(boundary);

		data = wkb_write_int(data, boundary->numVerts + (is_closed ? 0 : 1));
		data = wkb_write_geo_coord_array(data, boundary->verts, boundary->numVerts);
		/* close the ring */
		if (!is_closed)
			data = wkb_write_geo_coord(data, &boundary->verts[0]);
	}
	return data;
}

uint8* wkb_write_geo_coord_array(uint8 *data, const GeoCoord *coords, int num)
{
	for (int i = 0; i < num; i++)
		data = wkb_write_geo_coord(data, &coords[i]);
	return data;
}

uint8* wkb_write_geo_coord(uint8 *data, const GeoCoord *coord)
{
	data = wkb_write(data, &coord->lon, sizeof(coord->lon));
	data = wkb_write(data, &coord->lat, sizeof(coord->lat));
	return data;
}

uint8* wkb_write_endian(uint8 *data)
{
	/* Always use native order */
	uint32 order = 0x00000001;
	data[0] = ((uint8 *)&order)[0] ? WKB_NDR : WKB_XDR;
	return data + 1;
}

uint8* wkb_write_int(uint8 *data, uint32 value)
{
	return wkb_write(data, &value, sizeof(value));
}

uint8* wkb_write(uint8 *data, const void *value, size_t size)
{
	memcpy(data, value, size);
	return data + size;
}
