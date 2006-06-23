. support_funcs.sh

init_dml()
{
  echo "init_dml()"
}

begin()
{
  echo "begin()"
}

rollback()
{
  echo "rollback()"
}

commit()
{
  echo "commit()"
}

generate_initdata()
{
  GENDATA="$mktmp/generate.data"
  for datestyle in DMY European German ISO MDY NonEuropean Postgres SQL US YMD; do
     status "generating tranactions of date data for datestyle ${datestyle}"
     echo "SET DATESTYLE TO ${datestyle};" >> $GENDATA
     echo "insert into table1(ts, tsz, ds) values ('infinity', 'infinity', now());" >> $GENDATA
     echo "insert into table1(ts, tsz, ds) values ('-infinity', '-infinity', now());" >> $GENDATA
     # Final second of last year
     TS="(date_trunc('years', now())-'1 second'::interval)"
     echo "insert into table1(ts, tsz, ds) values ($TS, $TS, $TS);" >> $GENDATA
     # Final second of last month
     TS="(date_trunc('months', now())-'1 second'::interval)"
     echo "insert into table1(ts, tsz, ds) values ($TS, $TS, $TS);" >> $GENDATA
     # Sorta February 29th...
     TS="(date_trunc('years', now())+'60 days'::interval)"
     echo "insert into table1(ts, tsz, ds) values ($TS, $TS, $TS);" >> $GENDATA
     # Sorta February 29th...
     TS="(date_trunc('years', now())+'60 days'::interval+'1 year'::interval)"
     echo "insert into table1(ts, tsz, ds) values ($TS, $TS, $TS);" >> $GENDATA
     # Sorta February 29th...
     TS="(date_trunc('years', now())+'60 days'::interval+'2 years'::interval)"
     echo "insert into table1(ts, tsz, ds) values ($TS, $TS, $TS);" >> $GENDATA
     # Sorta February 29th...
     TS="(date_trunc('years', now())+'60 days'::interval+'3 years'::interval)"
     echo "insert into table1(ts, tsz, ds) values ($TS, $TS, $TS);" >> $GENDATA
     # Sorta February 29th...
     TS="(date_trunc('years', now())+'60 days'::interval+'4 years'::interval)"
     echo "insert into table1(ts, tsz, ds) values ($TS, $TS, $TS);" >> $GENDATA
     # Now
     TS="now()"
     echo "insert into table1(ts, tsz, ds) values ($TS, $TS, $TS);" >> $GENDATA

     # Now, go for a period of 1000 days, and generate an entry for each
     # day.
     for d1 in 0 1 2 3 4 5 6 7 8 9; do
       for d2 in 0 1 2 3 4 5 6 7 8 9; do
          for d3 in 0 1 2 3 4 5 6 7 8 9; do
             NUM="${d1}${d2}${d3}"
	     TS="(date_trunc('years',now())+'${NUM} days'::interval)"
	     echo "insert into table1(ts, tsz, ds) values ($TS, $TS, $TS);" >> $GENDATA
	  done
       done
     done     
  done
  status "done"
}

do_initdata()
{
   originnode=${ORIGINNODE:-"1"}
  eval db=\$DB${originnode}
   eval host=\$HOST${originnode}
  eval user=\$USER${originnode}
  eval port=\$PORT${originnode}
  generate_initdata
  launch_poll
  status "loading data from $mktmp/generate.data"
  $pgbindir/psql -h $host -p $port -d $db -U $user < $mktmp/generate.data 1> $mktmp/initdata.log 2> $mktmp/initdata.log
  if [ $? -ne 0 ]; then
    warn 3 "do_initdata failed, see $mktmp/initdata.log for details"
  fi 
  status "done"
}
