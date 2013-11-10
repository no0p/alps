-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION alps" to load this file. \quit


--CREATE FUNCTION predict(VARIADIC anyarray)
--  RETURNS text AS 'MODULE_PATHNAME'
--      LANGUAGE C STABLE;

-- Unused.
CREATE FUNCTION alps_launch(pg_catalog.int4)
  RETURNS pg_catalog.int4 STRICT
    AS 'MODULE_PATHNAME'
      LANGUAGE C;
