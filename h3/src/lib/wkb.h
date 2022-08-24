#ifndef PGH3_WKB_H
#define PGH3_WKB_H

#include <postgres.h>
#include <h3api.h>
#include <fmgr.h>

bytea* geo_boundary_array_to_wkb(const GeoBoundary *boundaries, size_t num);
bytea* geo_boundary_to_wkb(const GeoBoundary *boundary);

#endif
