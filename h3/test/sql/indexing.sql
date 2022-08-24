\pset tuples_only on

-- neighbouring indexes (one hexagon, one pentagon) at resolution 3
\set geo POINT(-144.52399108028, 49.7165031828995)
\set hexagon '\'831c02fffffffff\'::h3index'
\set pentagon '\'831c00fffffffff\'::h3index'
\set edgecross '\'8003fffffffffff\'::h3index'
\set resolution 3
\set lat1 84.76455330449812
\set lat2 89.980298101841
\set epsilon 0.0000000000001

--
-- TEST h3_to_geo and h3_geo_to_h3
--

-- convertion to geo works
SELECT h3_to_geo(:hexagon) ~= :geo;

-- convertion to h3 index works
SELECT h3_geo_to_h3(:geo, :resolution) = :hexagon;

-- h3_to_geo is inverse of h3_geo_to_h3
SELECT h3_to_geo(i) ~= :geo AND h3_get_resolution(i) = :resolution FROM (
    SELECT h3_geo_to_h3(:geo, :resolution) AS i
) AS q;
-- h3_geo_to_h3 is inverse of h3_to_geo
SELECT h3_geo_to_h3(g, r) = :hexagon FROM (
    SELECT h3_to_geo(:hexagon) AS g, h3_get_resolution(:hexagon) AS r
) AS q;
-- same for pentagon
SELECT h3_geo_to_h3(g, r) = :pentagon FROM (
    SELECT h3_to_geo(:pentagon) AS g, h3_get_resolution(:pentagon) AS r
) AS q;

--
-- TEST h3_to_geo_boundary
--

-- polyfill of geo boundary returns original index
SELECT h3_polyfill(h3_to_geo_boundary(:hexagon)::geometry::polygon, null, :resolution) = :hexagon;

-- same for pentagon
SELECT h3_polyfill(h3_to_geo_boundary(:pentagon)::geometry::polygon, null, :resolution) = :pentagon;

-- the boundary of an edgecrossing index is different with flag set to true
SELECT h3_to_geo_boundary(:hexagon)::geometry ~= h3_to_geo_boundary(:hexagon, true)::geometry
AND NOT h3_to_geo_boundary(:edgecross)::geometry ~= h3_to_geo_boundary(:edgecross, true)::geometry;

-- the boundary of of a non-edgecrossing index is a polygon
SELECT GeometryType(h3_to_geo_boundary(:pentagon, true)::geometry) LIKE 'POLYGON';

-- the boundary of an edgecrossing index is a multipolygon when split
SELECT GeometryType(h3_to_geo_boundary(:edgecross, true)::geometry) LIKE 'MULTIPOLYGON';

-- the boundary of an edgecrossing index is a polygon when not split
SELECT GeometryType(h3_to_geo_boundary(:edgecross, false)::geometry) LIKE 'POLYGON';

-- check latitude of antimeridian crossing points
SELECT every(ABS(ST_Y(p) - :lat1) < :epsilon OR ABS(ST_Y(p) - :lat2) < :epsilon)
FROM (
    SELECT (dp).geom AS p FROM (
        (SELECT ST_DumpPoints(h3_to_geo_boundary(:edgecross, true)::geometry) AS dp)
    ) AS q1
) AS q2
WHERE ABS(ABS(ST_X(p)) - 180) < :epsilon;
