\pset tuples_only on

-- neighbouring indexes (one hexagon, one pentagon) at resolution 3
\set hexagon '\'831c02fffffffff\'::h3index'
\set pentagon '\'831c00fffffffff\'::h3index'
\set resolution 3

--
-- TEST h3_cell_to_children, h3_cell_to_parent and h3_to_center_chil
--

-- all parents of one the children of a hexagon, must be the original hexagon
SELECT bool_and(i = :hexagon) FROM (
	SELECT h3_cell_to_parent(h3_cell_to_children(:hexagon)) i
) q;

-- one child of the parent of a hexagon, must be the original hexagon
SELECT bool_or(i = :hexagon) FROM (
	SELECT h3_cell_to_children(h3_cell_to_parent(:hexagon)) i
) q;

-- all parents of one the children of a pentagon, must be the original pentagon
SELECT bool_and(i = :pentagon) FROM (
	SELECT h3_cell_to_parent(h3_cell_to_children(:pentagon)) i
) q;
-- one child of the parent of a pentagon, must be the original pentagon
SELECT bool_or(i = :pentagon) FROM (
	SELECT h3_cell_to_children(h3_cell_to_parent(:pentagon)) i
) q;

-- hexagon has 7 children
SELECT array_length(array_agg(hex), 1) = 7 FROM (
	SELECT h3_cell_to_children(:hexagon) hex
) q;

-- pentagon has 6 children
SELECT array_length(array_agg(hex), 1) = 6 FROM (
	SELECT h3_cell_to_children(:pentagon) hex
) q;

-- parent is one lower resolution
SELECT h3_get_resolution(h3_cell_to_parent(:hexagon)) = :resolution -1;

-- all children is one higher resolution
SELECT bool_and(r = :resolution +1) FROM (
	SELECT h3_get_resolution(h3_cell_to_children(:hexagon)) r
) q;

-- parent of center child should be original index
SELECT :hexagon = h3_cell_to_parent(h3_cell_to_center_child(:hexagon, 15), :resolution);

--
-- TEST h3_compact_cells and h3_uncompact_cells
--

-- compacts the children of two hexes into the original two hexes
SELECT array_agg(result) is null FROM (
	SELECT h3_compact_cells(
		ARRAY(SELECT h3_cell_to_children(:hexagon) UNION SELECT h3_cell_to_children(:pentagon))
	) result
	EXCEPT SELECT unnest(ARRAY[:hexagon, :pentagon]) result
) q;

-- compact is inverse of uncompact
SELECT h3_compact_cells(ARRAY(SELECT h3_uncompact_cells(ARRAY[:hexagon], :resolution))) = :hexagon;

-- uncompacts all to same resolution, gives same result as getting children
SELECT array_agg(result) is null FROM (
	SELECT h3_uncompact_cells(ARRAY(
		SELECT h3_cell_to_children(:hexagon) UNION SELECT :pentagon
	), :resolution +2) result
	EXCEPT (
		SELECT h3_cell_to_children(:hexagon, :resolution +2) result
		UNION SELECT h3_cell_to_children(:pentagon, :resolution +2) result
	)
) q;

--
-- TEST h3_cell_to_children_slow
--

-- h3_cell_to_children_slow and h3_cell_to_children have same result
SELECT array_agg(result) is null FROM (
	SELECT h3_cell_to_children_slow(:hexagon, :resolution + 3) result
	EXCEPT SELECT h3_cell_to_children(:hexagon, :resolution + 3) result
) q;