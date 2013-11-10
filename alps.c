#include "alps.h"

/* Essential for shared libs! */
PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(alps_launch);
PG_FUNCTION_INFO_V1(predict);

/* Signal handling */
volatile sig_atomic_t got_sigterm = false;
volatile sig_atomic_t got_sighup = false;

/* This is the main execution loop for the alps module */
void alps_main(Datum main_arg) {
  elog(LOG, "Everybody Get Up!");
  
  pqsignal(SIGTERM, alps_sigterm);
  pqsignal(SIGHUP, alps_sighup);
  BackgroundWorkerUnblockSignals();
 
  /* Connect to our database */
  BackgroundWorkerInitializeConnection(target_db, NULL);  //TODO db name a GUC
  
  /* Ensure meta tables present, ensure madlib functions accesible */
  initialize_mh();
  
  /* Main loop, periodically check if should rebuild models */
  while (!got_sigterm) {
    int rc;

    rc = WaitLatch(&MyProc->procLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH, alps_poll_seconds * 1000L);
    ResetLatch(&MyProc->procLatch);
    
    if (rc & WL_POSTMASTER_DEATH) {
      elog(LOG, "alps terminating!");
      proc_exit(1);
    }

    /* Build column models */
    process_columns();   
  }
  
  proc_exit(0);
}

/* Gets a list of columns and builds models*/
void process_columns(void) {
  StringInfoData buf;
  
  int  ret, proc, i;
  char *coltype, *support, *relid, *colname, *schema_name, *table_name;
  SPITupleTable *coltuptable;
  model_type_t modelt;
  
  SetCurrentStatementStartTimestamp();
  StartTransactionCommand();
  SPI_connect();
  
  PushActiveSnapshot(GetTransactionSnapshot());
  
  pgstat_report_activity(STATE_RUNNING, "querying for columns");
  
  /* Query for all non-primary key numeric columns and their supports */
  initStringInfo(&buf);
  appendStringInfo(&buf, //TODO can do a select against the main query essence of top 2 queries for numeric only
  "WITH non_index_columns as (SELECT tablez.relname, attname, typname, pg_tables.schemaname, tablez.oid as relid"
  " FROM ("
  "  SELECT relname, oid "
  "    FROM pg_class where relname in ("
  "      SELECT tablename FROM pg_tables WHERE schemaname NOT IN ('pg_catalog', 'information_schema', 'alps')"
  "    ) "
  "  ) AS tablez "
  "    INNER JOIN pg_attribute on pg_attribute.attrelid = tablez.oid"
  "    INNER JOIN pg_type on pg_type.oid = pg_attribute.atttypid"
  "    INNER JOIN pg_tables ON pg_tables.tablename = tablez.relname"
  "    INNER JOIN pg_stat_all_tables ON pg_stat_all_tables.relid = tablez.oid "
  "    LEFT OUTER JOIN alps.model_status mstat ON mstat.relid = tablez.oid "
  " WHERE pg_attribute.attname NOT LIKE '%%__predicted' "
  " AND typname NOT IN ('xid', 'cid', 'tid', 'oid') "
  " AND (mstat.last_trained_at IS NULL OR mstat.last_trained_at < pg_stat_all_tables.last_autoanalyze OR mstat.last_trained_at < pg_stat_all_tables.last_analyze)"
  " AND NOT EXISTS ("
  "  SELECT * FROM pg_index INNER JOIN pg_class ON pg_index.indexrelid = pg_class.oid AND pg_index.indkey::text = pg_attribute.attnum::text"
  " )"
  " ORDER BY relname),"
  " numeric_columns as (SELECT tablez.relname, attname, typname, pg_tables.schemaname, tablez.oid as relid"
  " FROM ("
  "  SELECT relname, oid "
  "    FROM pg_class where relname in ("
  "      SELECT tablename FROM pg_tables WHERE schemaname NOT IN ('pg_catalog', 'information_schema', 'alps')"
  "    ) "
  "  ) AS tablez "
  "    INNER JOIN pg_attribute on pg_attribute.attrelid = tablez.oid"
  "    INNER JOIN pg_type on pg_type.oid = pg_attribute.atttypid"
  "    INNER JOIN pg_tables ON pg_tables.tablename = tablez.relname"
  "    INNER JOIN pg_stat_all_tables ON pg_stat_all_tables.relid = tablez.oid "
  "    LEFT OUTER JOIN alps.model_status mstat ON mstat.relid = tablez.oid "
  " WHERE pg_attribute.attname NOT LIKE '%%__predicted' "
  " AND typname IN ('numeric', 'int', 'float') "
  " AND (mstat.last_trained_at IS NULL OR mstat.last_trained_at < pg_stat_all_tables.last_autoanalyze OR mstat.last_trained_at < pg_stat_all_tables.last_analyze)"
  " AND NOT EXISTS ("
  "  SELECT * FROM pg_index INNER JOIN pg_class ON pg_index.indexrelid = pg_class.oid AND pg_index.indkey::text = pg_attribute.attnum::text"
  " )"
  " ORDER BY relname)"
" SELECT prec.schemaname, prec.relname, prec.attname, prec.typname, '\"' || coalesce(prec.string_agg, '') || (case when prec.string_agg is not null and follow.string_agg is not null then '\",\"' else '' end ) || coalesce(follow.string_agg, '') || '\"', prec.relid"
  " FROM"
    " (SELECT ac.schemaname, ac.relname, ac.attname, ac.typname, ac.relid, string_agg(nc.attname, '\",\"')"
    "  OVER (PARTITION BY ac.relname ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) "
    "    FROM non_index_columns ac"
    "      LEFT OUTER JOIN numeric_columns nc ON ac.relname = nc.relname AND ac.schemaname = nc.schemaname AND ac.attname = nc.attname) as prec"
    " INNER JOIN ("
    "  SELECT ac.schemaname, ac.relname, ac.attname, ac.typname, ac.relid, string_agg(nc.attname, '\",\"')"
    "    OVER (PARTITION BY ac.relname ROWS BETWEEN 1 FOLLOWING AND UNBOUNDED FOLLOWING) "
    "      FROM non_index_columns ac"
    "        LEFT OUTER JOIN numeric_columns nc ON ac.relname = nc.relname AND ac.schemaname = nc.schemaname AND ac.attname = nc.attname"
    ") as follow ON follow.relname = prec.relname AND follow.schemaname = prec.schemaname AND follow.attname = prec.attname;");
  //NOTA BENE: Can add a limit to the above, and order by mstat.last_trained_at ASC nulls first to deal with max locks potentially...spread out work.
  
  ret = SPI_execute(buf.data, false, 0);
  if (ret != SPI_OK_SELECT)
    elog(FATAL, "SPI_execute failed: error code %d", ret);
  
  proc = SPI_processed;
  coltuptable = SPI_tuptable;


  /* If results iterate over and generate models */
  if (coltuptable != NULL) {
    for(i = 0; i < proc; i++) {
      schema_name = SPI_getvalue(coltuptable->vals[i], coltuptable->tupdesc, 1);
      table_name = SPI_getvalue(coltuptable->vals[i], coltuptable->tupdesc, 2);
      colname = SPI_getvalue(coltuptable->vals[i], coltuptable->tupdesc, 3);
      coltype = SPI_getvalue(coltuptable->vals[i], coltuptable->tupdesc, 4);
      support = SPI_getvalue(coltuptable->vals[i], coltuptable->tupdesc, 5);
      relid =   SPI_getvalue(coltuptable->vals[i], coltuptable->tupdesc, 6);
      
      /* Set last trained at for the relation */ 
      resetStringInfo(&buf);
      appendStringInfo(&buf, "UPDATE alps.model_status SET last_trained_at = now() WHERE relid = %s", relid);
      ret = SPI_execute(buf.data, false, 0);
      if (ret != SPI_OK_UPDATE) 
        elog(FATAL, "unable to update alps model_status.");

      if(SPI_processed == 0) {
        resetStringInfo(&buf);
        appendStringInfo(&buf, "INSERT INTO alps.model_status (last_trained_at, relid) VALUES (now(), %s)", relid);
        ret = SPI_execute(buf.data, false, 0);
        
        if (ret != SPI_OK_INSERT)
          elog(FATAL, "unable to insert to alps model_status.");
      }
      
      if(strcmp(support, "\"\"") != 0) { 
      
        /* Select model type */
        if (strcmp(coltype, "numeric") == 0 || strcmp(coltype, "int4") == 0 || strcmp(coltype, "_float8") == 0) {
          modelt = LINEAR_REGRESSION;        
        } else if (strcmp(coltype, "bool") == 0) {
          modelt = LOGISTIC_REGRESSION;
        } else {
          modelt = -1;
          elog(LOG, "no model implemented for column of type: %s", coltype);
        }
        
        /* Train models*/
                   
        if (modelt == LINEAR_REGRESSION) {
          train_regression_model(schema_name, table_name, colname, coltype, support);
        } else if (modelt == LOGISTIC_REGRESSION) {    
          train_logit_model(schema_name, table_name, colname, coltype, support);
        }
        
        delay(); //no starve
        
        /* Apply models */
        /*if (strcmp(method, "join_table") == 0) {
          if (modelt == LINEAR_REGRESSION)
            apply_regression_model_to_join_table(schema_name, table_name, colname, coltype, support);
          else if (modelt == LOGISTIC_REGRESSION)
            apply_logit_model_to_join_table(schema_name, table_name, colname, coltype, support);
        } else */
        if (strcmp(method, "add_columns") == 0) {
          if (modelt == LINEAR_REGRESSION) 
            apply_regression_model_to_column(schema_name, table_name, colname, coltype, support);
          else if (modelt == LOGISTIC_REGRESSION)
            apply_logit_model_to_columns(schema_name, table_name, colname, coltype, support);
            
          delay(); // no starve
        } else {
          elog(LOG, "unknown model application method");
        }
      } else {
        elog(LOG, "column has no support");
      }
    }
  }
      
  SPI_finish();
  PopActiveSnapshot();
  CommitTransactionCommand();
  elog(LOG, "Column Pass Complete");  //remove?  
}


/* Create Status Schema and Status Tables */
void initialize_mh(void) {
  StringInfoData buf;
  int      ret, ntup;
  bool    isnull;
  
  SetCurrentStatementStartTimestamp();
  StartTransactionCommand();
  SPI_connect();
  
  PushActiveSnapshot(GetTransactionSnapshot());
  
  /* Verify dependencies installed */
  pgstat_report_activity(STATE_RUNNING, "verifying madlib present");
  
  initStringInfo(&buf);
  appendStringInfo(&buf, "select count(*) from pg_namespace inner join "
      "pg_proc on pg_proc.pronamespace = pg_namespace.oid where nspname = 'madlib'");
  ret = SPI_execute(buf.data, true, 0);
  if (ret != SPI_OK_SELECT)
    elog(FATAL, "SPI_execute failed: error code %d", ret);
  ntup = DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[0],
                       SPI_tuptable->tupdesc,
                       1, &isnull));
  if (ntup == 0)
    elog(FATAL, "Unable to find functions in madlib schema.");
  
  
  /* Create schema and meta tables if not exist */
  pgstat_report_activity(STATE_RUNNING, "initializing alps schema");
  resetStringInfo(&buf);
  appendStringInfo(&buf, 
    "create schema if not exists alps;"
    "create table if not exists alps.model_status (relid int, last_trained_at timestamptz);"
  );
  
  ret = SPI_execute(buf.data, false, 0);
  if (ret != SPI_OK_UTILITY)
    elog(FATAL, "SPI_execute failed: error code %d", ret);
  
  /* Clean up */
  
  SPI_finish();
  PopActiveSnapshot();
  CommitTransactionCommand();
  elog(LOG, "alps Initialized");
}

/* Signal Handlers*/
void alps_sigterm(SIGNAL_ARGS) {
  int save_errno = errno;
  got_sigterm = true;
  if (MyProc)
    SetLatch(&MyProc->procLatch);
  errno = save_errno;
}

void alps_sighup(SIGNAL_ARGS) {
  got_sighup = true;
  if (MyProc)
    SetLatch(&MyProc->procLatch);
}

void delay(void) {
  WaitLatch(&MyProc->procLatch, WL_LATCH_SET | WL_TIMEOUT, alps_delay * 1L);
  ResetLatch(&MyProc->procLatch);
}

/* This is callback execute when creating the extension.  It registers the background worker
   In the future may make sense to start the background worker at this point, so as not to require
   a server restart.
 */
void _PG_init(void) {
  BackgroundWorker worker;
  
  
  /* Restricts modeling to a specific database */
  DefineCustomStringVariable("alps.target_db",
                              "Database alps Runs In",
                              NULL,
                              &target_db,
                              "postgres",
                              PGC_SIGHUP,
                              0,
                              NULL,
                              NULL,
                              NULL);
                             
  /* Seconds between query to check if modeling work is to be done */
  DefineCustomIntVariable("alps.poll_seconds",
                            "Duration between each check (in seconds).",
                            NULL,
                            &alps_poll_seconds,
                            300,
                            1,
                            INT_MAX,
                            PGC_SIGHUP,
                            0,
                            NULL,
                            NULL,
                            NULL);
                            
  /* Seconds between query to check if modeling work is to be done */
  DefineCustomIntVariable("alps.delay",
                            "Duration between training and update operations (in milli seconds).",
                            NULL,
                            &alps_delay,
                            200,
                            1,
                            INT_MAX,
                            PGC_SIGHUP,
                            0,
                            NULL,
                            NULL,
                            NULL);
                            
  /* How predicted values are stored.  
     Can add columns to tables, or create a join table if primary key is available.
       options are "join_table", "add_columns"
  */                         
  DefineCustomStringVariable("alps.method",
                              "How to store predicted values",
                              NULL,
                              &method,
                              "add_columns",
                              PGC_SIGHUP,
                              0,
                              NULL,
                              NULL,
                              NULL);
  
  worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
  worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
  worker.bgw_main = alps_main;

  snprintf(worker.bgw_name, BGW_MAXLEN, "alps");
  worker.bgw_restart_time = BGW_NEVER_RESTART;
  worker.bgw_main_arg = (Datum) 0;
  RegisterBackgroundWorker(&worker);
}

