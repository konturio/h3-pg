#ifndef PGH3_VECT3_H
#define PGH3_VECT3_H

#include <postgres.h>
#include <h3api.h>
#include <fmgr.h>

typedef struct {
    double x;
    double y;
    double z;
} Vect3;

void vect3_from_geo_coord(const GeoCoord *coord, Vect3 *vect);

void vect3_to_geo_coord(const Vect3 *vect, GeoCoord *coord);

void vect3_normalize(Vect3 *vect);

void vect3_cross(const Vect3 *vect1, const Vect3 *vect2, Vect3 *prod);

#endif
