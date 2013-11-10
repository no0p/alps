#ifndef ALPINE_H
#define ALPINE_H

#include "postgres.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

#include "access/xact.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "pgstat.h"
#include "utils/builtins.h"
#include "utils/snapmgr.h"
#include "tcop/utility.h"


/* Entry point of library loading */
void _PG_init(void);

void alps_main(Datum);
void alps_sigterm(SIGNAL_ARGS);
void alps_sighup(SIGNAL_ARGS);

void initialize_mh(void);
void process_columns(void);

Datum alps_launch(PG_FUNCTION_ARGS);
Datum predict(PG_FUNCTION_ARGS);

/* GUC variables */
char* target_db;
char* method;
int alps_poll_seconds;

/* model functions */
void train_logit_model(char *schemaname, char *tablename, char* colname, char* coltype, char *support);
void apply_logit_model_to_columns(char *schemaname, char *tablename, char* colname, char* coltype, char *support);
void apply_logit_model_to_join_table(char *schemaname, char *tablename, char* colname, char* coltype, char *support);

void train_regression_model(char *schemaname, char *tablename, char* colname, char* coltype, char *support);
void apply_regression_model_to_column(char *schemaname, char *tablename, char* colname, char* coltype, char *support);
void apply_regression_model_to_join_table(char *schemaname, char *tablename, char* colname, char* coltype, char *support);

void add_prediction_join_table(char *schemaname, char *tablename);
int add_prediction_column(char *schemaname, char *tablename, char* colname, char* coltype);

/**/
typedef enum {LOGISTIC_REGRESSION, LINEAR_REGRESSION} model_type_t;


#endif
