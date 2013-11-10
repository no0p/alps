MODULE_big=alps
OBJS=alps.o models.o
EXTENSION=alps
DATA=alps--1.0.sql
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
