#!/bin/bash
# $Id: configure-replication.sh,v 1.1.2.2 2006-12-14 22:31:19 cbbrowne Exp $

# Global defaults
CLUSTER=${CLUSTER:-"slonytest"}
NUMNODES=${NUMNODES:-"2"}

# Defaults - origin node
DB1=${DB1:-${PGDATABASE:-"slonytest"}}
HOST1=${HOST1:-`hostname`}
USER1=${USER1:-${PGUSER:-"slony"}}
PORT1=${PORT1:-${PGPORT:-"5432"}}

# Defaults - node 2
DB2=${DB2:-${PGDATABASE:-"slonytest"}}
HOST2=${HOST2:-"backup.example.info"}
USER2=${USER2:-${PGUSER:-"slony"}}
PORT2=${PORT2:-${PGPORT:-"5432"}}

# Defaults - node 3
DB3=${DB3:-${PGDATABASE:-"slonytest"}}
HOST3=${HOST3:-"backup3.example.info"}
USER3=${USER3:-${PGUSER:-"slony"}}
PORT3=${PORT3:-${PGPORT:-"5432"}}

# Defaults - node 4
DB4=${DB4:-${PGDATABASE:-"slonytest"}}
HOST4=${HOST4:-"backup4.example.info"}
USER4=${USER4:-${PGUSER:-"slony"}}
PORT4=${PORT4:-${PGPORT:-"5432"}}

# Defaults - node 5
DB5=${DB5:-${PGDATABASE:-"slonytest"}}
HOST5=${HOST5:-"backup5.example.info"}
USER5=${USER5:-${PGUSER:-"slony"}}
PORT5=${PORT5:-${PGPORT:-"5432"}}

store_path()
{

echo "include <${PREAMBLE}>;" > $mktmp/store_paths.slonik
  i=1
  while : ; do
    eval db=\$DB${i}
    eval host=\$HOST${i}
    eval user=\$USER${i}
    eval port=\$PORT${i}

    if [ -n "${db}" -a "${host}" -a "${user}" -a "${port}" ]; then
      j=1
      while : ; do
        if [ ${i} -ne ${j} ]; then
          eval bdb=\$DB${j}
          eval bhost=\$HOST${j}
          eval buser=\$USER${j}
          eval bport=\$PORT${j}
          if [ -n "${bdb}" -a "${bhost}" -a "${buser}" -a "${bport}" ]; then
            echo "STORE PATH (SERVER=${i}, CLIENT=${j}, CONNINFO='dbname=${db} host=${host} user=${user} port=${port}');" >> $mktmp/store_paths.slonik          else
            echo "STORE PATH (SERVER=${i}, CLIENT=${j}, CONNINFO='dbname=${db} host=${host} user=${user} port=${port}');" >> $mktmp/store_paths.slonik
          fi
        fi
        if [ ${j} -ge ${NUMNODES} ]; then
          break;
        else
          j=$((${j} + 1))
        fi
      done
      if [ ${i} -ge ${NUMNODES} ]; then
        break;
      else
        i=$((${i} +1))
      fi
    else
      echo "no DB"
    fi
  done
}

mktmp=`mktemp -d -t slonytest-temp.XXXXXX`
if [ $MY_MKTEMP_IS_DECREPIT ] ; then
       mktmp=`mktemp -d /tmp/slonytest-temp.XXXXXX`
fi

PREAMBLE=${mktmp}/preamble.slonik

echo "cluster name=${CLUSTER};" > $PREAMBLE

alias=1

while : ; do
  eval db=\$DB${alias}
  eval host=\$HOST${alias}
  eval user=\$USER${alias}
  eval port=\$PORT${alias}

  if [ -n "${db}" -a "${host}" -a "${user}" -a "${port}" ]; then
    conninfo="dbname=${db} host=${host} user=${user} port=${port}"
    echo "NODE ${alias} ADMIN CONNINFO = '${conninfo}';" >> $PREAMBLE
    if [ ${alias} -ge ${NUMNODES} ]; then
      break;
    else
      alias=`expr ${alias} + 1`
    fi   
  else
    break;
  fi
done

# The following schema is based on that of LedgerSMB

ALTTABLES1="acc_trans ap ar assembly audittrail business chart
 custom_field_catalog custom_table_catalog customer customertax
 defaults department dpt_trans employee exchangerate gifi gl inventory
 invoice jcitems language makemodel oe orderitems parts partscustomer
 partsgroup partstax partsvendor pricegroup project recurring
 recurringemail recurringprint session shipto sic status tax
 transactions translation vendor vendortax warehouse yearend"

for t in `echo $ALTTABLES1`; do
  ALTTABLES="$ALTTABLES public.${t}"
done
  
ALTSEQUENCES1="acc_trans_entry_id_seq audittrail_entry_id_seq
 custom_field_catalog_field_id_seq custom_table_catalog_table_id_seq
 id inventory_entry_id_seq invoiceid jcitemsid orderitemsid
 partscustomer_entry_id_seq partsvendor_entry_id_seq
 session_session_id_seq shipto_entry_id_seq"

for s in `echo $ALTSEQUENCES1`; do
  ALTSEQUENCES="$ALTSEQUENCES public.${s}"
done

TABLES=${TABLES:-${ALTTABLES}}
SEQUENCES=${SEQUENCES:-${ALTSEQUENCES}}

SETUPSET=${mktmp}/create_set.slonik

echo "include <${PREAMBLE}>;" > $SETUPSET
echo "create set (id=1, origin=1, comment='${CLUSTER} Tables and Sequences');" >> $SETUPSET

tnum=1

for table in `echo $TABLES`; do
    echo "set add table (id=${tnum}, set id=1, origin=1, fully qualified name='${table}', comment='${CLUSTER} table ${table}');" >> $SETUPSET
    tnum=`expr ${tnum} + 1`
done

snum=1
for seq in `echo $SEQUENCES`; do
    echo "set add sequence (id=${snum}, set id=1, origin=1, fully qualified name='${seq}', comment='${CLUSTER} sequence ${seq}');" >> $SETUPSET    snum=`expr ${snum} + 1`
done

NODEINIT=$mktmp/create_nodes.slonik
echo "include <${PREAMBLE}>;" > $NODEINIT
echo "init cluster (id=1, comment='${CLUSTER} node 1');" >> $NODEINIT

node=2
while : ; do
    SUBFILE=$mktmp/subscribe_set_${node}.slonik
    echo "include <${PREAMBLE}>;" > $SUBFILE
    echo "store node (id=${node}, comment='${CLUSTER} subscriber node ${node}');" >> $NODEINIT
    echo "subscribe set (id=1, provider=1, receiver=${node}, forward=yes);" >> $SUBFILE
    if [ ${node} -ge ${NUMNODES} ]; then
      break;
    else
      node=`expr ${node} + 1`
    fi   
done

store_path

echo "
$0 has generated Slony-I slonik scripts to initialize replication for SlonyTest.

Cluster name: ${CLUSTER}
Number of nodes: ${NUMNODES}
Scripts are in ${mktmp}
=====================
"
ls -l $mktmp

echo "
=====================
Be sure to verify that the contents of $PREAMBLE very carefully, as
the configuration there is used widely in the other scripts.
=====================
====================="
