#include "alps.h"

void train_logit_model(char *schemaname, char *tablename, char* colname, char* coltype, char *support) {
  StringInfoData buf;
  
  /* Train Model */
  elog(LOG, "training logistic regression model %s.%s %s", schemaname, tablename, colname);
  initStringInfo(&buf);
  appendStringInfo(&buf,
     "DROP TABLE IF EXISTS alps.\"%s_%s_%s_logit\";" 
     " DO $$ BEGIN"
     " PERFORM madlib.logregr_train("
     " '%s.%s', 'alps.\"%s_%s_%s_logit\"', '\"%s\"', 'ARRAY[1,%s]'"
     " , NULL, 20, 'irls');"
     " EXCEPTION when others then END $$;", 
     schemaname, 
     tablename,
     colname,
     schemaname, 
     tablename, 
     schemaname,
     tablename, 
     colname,
     colname, 
     support);
     
  SPI_execute(buf.data, false, 0);
}

void train_regression_model(char *schemaname, char *tablename, char* colname, char* coltype, char *support) {
  StringInfoData buf;
  
  elog(LOG, "training linear regression model %s.%s %s", schemaname, tablename, colname);
  
  initStringInfo(&buf);
  appendStringInfo(&buf, 
     " DROP TABLE IF EXISTS alps.\"%s_%s_%s_linregr\"; "
     " DO $$ BEGIN"
     " PERFORM madlib.linregr_train( '%s.%s',"
     " 'alps.\"%s_%s_%s_linregr\"',"
     " '\"%s\"',"
     " 'ARRAY[1,%s]');"
     " EXCEPTION when others then END $$;", 
     schemaname, 
     tablename,
     colname,
     schemaname, 
     tablename,
     schemaname,  
     tablename, 
     colname,
     colname, 
     support);
  //elog(LOG, "%s", buf.data);
        
  SPI_execute(buf.data, false, 0);
}


void apply_logit_model_to_columns(char *schemaname, char *tablename, char* colname, char* coltype, char *support) {
  StringInfoData buf;
  int ret;
    
  add_prediction_column(schemaname, tablename, colname, coltype);
 
  /* Populate predicted column */
  initStringInfo(&buf);
  appendStringInfo(&buf,
     "UPDATE %s.%s SET \"%s__predicted\" = madlib.logregr_predict(ARRAY[1, %s], m.coef) FROM alps.\"%s_%s_%s_logit\" m",
     schemaname, tablename, colname, support, schemaname, tablename, colname
  );
  elog(LOG, "About to execute");
  ret = SPI_execute(buf.data, false, 0);
  if (ret != SPI_OK_UPDATE) 
    elog(FATAL, "unable to update predicted values.");
}

void apply_regression_model_to_column(char *schemaname, char *tablename, char* colname, char* coltype, char *support) {
  
  StringInfoData buf;
  int ret;
  
  ret = add_prediction_column(schemaname, tablename, colname, coltype);
  
  /* Populate predicted column */ 
  initStringInfo(&buf);
  appendStringInfo(&buf,
     "UPDATE %s.%s SET \"%s__predicted\" = madlib.linregr_predict(ARRAY[1, %s], m.coef) FROM alps.\"%s_%s_%s_linregr\" m",
     schemaname, tablename, colname, support, schemaname, tablename, colname
  );
  //elog(LOG, "About to execute");
  ret = SPI_execute(buf.data, false, 0);
  if (ret != SPI_OK_UPDATE) 
    elog(FATAL, "unable to update predicted values.");
  
}

void apply_logit_model_to_join_table(char *schemaname, char *tablename, char* colname, char* coltype, char *support) {
  StringInfoData buf;

  add_prediction_join_table(schemaname, tablename);
  initStringInfo(&buf);
  
  
}

/* Not yet implemented */
void apply_regression_model_to_join_table(char *schemaname, char *tablename, char* colname, char* coltype, char *support) {
  StringInfoData buf;

  add_prediction_join_table(schemaname, tablename);
  initStringInfo(&buf);

}

/* Executes SQL to add a prediction join table if one does not exist for the table */
void add_prediction_join_table(char *schemaname, char *tablename) {
  StringInfoData buf;

  //todo capture schema modifications by dropping alps_prediction_tables periodically?
  //  or inherits with some defaults so results do not show up in parent?
  initStringInfo(&buf);
  appendStringInfo(&buf,
     "CREATE SCHEMA IF NOT EXISTS alps_prediction_tables; " 
     "CREATE TABLE IF NOT EXISTS alps_prediction_tables.\"%s_%s\" (LIKE \"%s\".\"%s\");", 
     schemaname, tablename, schemaname, tablename
  );
  
  SPI_execute(buf.data, false, 0);
}

/* Add "colname__predict" columns to table.  One day want to do this better.*/
int add_prediction_column(char *schemaname, char *tablename, char* colname, char* coltype) {   
  int ret;
  bool  isnull;
  int ntup;
  StringInfoData buf;

  initStringInfo(&buf);
  appendStringInfo(&buf,
     "SELECT count(*) FROM pg_attribute "
     "  INNER JOIN pg_class on pg_class.oid = pg_attribute.attrelid"
     "  INNER JOIN pg_namespace on pg_namespace.oid = pg_class.relnamespace"
     " WHERE pg_attribute.attname = '%s__predicted'"
     "   AND pg_class.relname = '%s'"
     "   AND pg_namespace.nspname = '%s';"
     , colname, tablename, schemaname
  );
  
  ret = SPI_execute(buf.data, true, 0);
  if (ret != SPI_OK_SELECT)
    elog(FATAL, "predict column lookup failed: error code %d", ret);

  if (SPI_processed != 1)
    elog(FATAL, "invalid result from predict column lookup");

  ntup = DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &isnull));
  if (isnull)
    elog(FATAL, "null result");

  if (ntup == 0) {
    resetStringInfo(&buf);
    appendStringInfo(&buf, "ALTER TABLE %s.%s ADD COLUMN \"%s__predicted\" %s;", schemaname, tablename, colname, coltype);
    ret = SPI_execute(buf.data, false, 0); // false / true in same function?
    if (ret != SPI_OK_UTILITY) 
      elog(FATAL, "alter table failed return code:  %d", ret);
  }
  
  return 0;
}
