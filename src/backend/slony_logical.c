/*-------------------------------------------------------------------------
 *
 * 
 *
 * Copyright (c) 2012, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		  
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "catalog/pg_class.h"
#include "catalog/pg_type.h"
#include "catalog/index.h"

#include "replication/output_plugin.h"
#include "replication/snapbuild.h"

#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "utils/typcache.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "fmgr.h"
#include "access/hash.h"
#include "replication/logical.h"
#include "nodes/makefuncs.h"
#include "commands/defrem.h"

PG_MODULE_MAGIC;

void _PG_init(void);


extern void pg_decode_init(LogicalDecodingContext * ctx, bool is_init);

extern bool pg_decode_begin_txn(LogicalDecodingContext * ctx, 
								ReorderBufferTXN* txn);
extern bool pg_decode_commit_txn(LogicalDecodingContext * ctx,
								 ReorderBufferTXN* txn, XLogRecPtr commit_lsn);
extern bool pg_decode_change(LogicalDecodingContext * ctx, 
							 ReorderBufferTXN* txn,
							 Oid tableoid, ReorderBufferChange *change);

extern bool pg_decode_clean(LogicalDecodingContext * ctx);

char * columnAsText(TupleDesc tupdesc, HeapTuple tuple,int idx);

unsigned int local_id=0;

HTAB * replicated_tables=NULL;

typedef struct  {
	const char * key;
	const char * namespace;
	const char * table_name;
	int set;

} replicated_table;

static uint32
replicated_table_hash(const void *kp, Size ksize)
{
	char	   *key = *((char **) kp);
	return hash_any((void *) key, strlen(key));
}
static int
replicated_table_cmp(const void *kp1, const void *kp2, Size ksize)
{
	char	   *key1 = *((char **) kp1);
	char	   *key2 = *((char **) kp2);
	return strcmp(key1, key2);
}

bool is_replicated(const char * namespace,const char * table);


void
_PG_init(void)
{
}




void
pg_decode_init(LogicalDecodingContext * ctx, bool is_init)
{	
	char * table;
	bool found;
	HASHCTL hctl;
	int i ;

	


	List * options = list_make1(makeDefElem("schema_table_1"
											,(Node*)makeString("public")));
	options = lappend(options,makeDefElem("table_1"
										  ,(Node*)makeString("a")));
	options = lappend(options,makeDefElem("schema_table_2"
										  ,(Node*)makeString("public")));
	options = lappend(options,makeDefElem("table_2"
										  ,(Node*)makeString("b")));
	options = lappend(options,makeDefElem("schema_table_3"
										  ,(Node*)makeString("public")));
	options = lappend(options,makeDefElem("table_3"
										  ,(Node*)makeString("c")));


	ctx->output_plugin_private = AllocSetContextCreate(TopMemoryContext,
									 "slony logical  context",
									 ALLOCSET_DEFAULT_MINSIZE,
									 ALLOCSET_DEFAULT_INITSIZE,
									 ALLOCSET_DEFAULT_MAXSIZE);


	AssertVariableIsOfType(&pg_decode_init, LogicalDecodeInitCB);
	MemoryContext context = (MemoryContext)ctx->output_plugin_private;
	MemoryContext old = MemoryContextSwitchTo(context);
											

	/**
	 * query the local database to find
	 * 1. the local_id
	 */
	elog(NOTICE,"inside of pg_decode_init");
	hctl.keysize = sizeof(char*);
	hctl.entrysize = sizeof(replicated_table);
	hctl.hash = replicated_table_hash;
	hctl.match = replicated_table_cmp;

	/**
	 * build a hash table with information on all replicated tables.
	 */
	replicated_tables=hash_create("replicated_tables",10,&hctl,
								  HASH_ELEM | HASH_FUNCTION | HASH_COMPARE);
	for(i = 0; i < options->length; i= i + 2 )
	{
		DefElem * def_schema = (DefElem*) list_nth(options,i);
		DefElem * def_table = (DefElem*) list_nth(options,i+1);
		const char * schema= defGetString(def_schema);
		const char * table_name = defGetString(def_table);

		table = palloc(strlen(schema) + strlen(table_name)+2);
		sprintf(table,"%s.%s",schema,table_name);
		replicated_table * entry=hash_search(replicated_tables,
											 &table,HASH_ENTER,&found);
		entry->key=table;
		entry->namespace=pstrdup(schema);
		entry->table_name=pstrdup(table_name);
		entry->set=1;
	}
	MemoryContextSwitchTo(old);
}


bool
pg_decode_begin_txn(LogicalDecodingContext * ctx, ReorderBufferTXN* txn)
{
	AssertVariableIsOfType(&pg_decode_begin_txn, LogicalDecodeBeginCB);
	/**
	 * we can ignore the begin and commit. slony operates
	 * on SYNC boundaries.
	 */
	elog(NOTICE,"inside of begin");
	ctx->prepare_write(ctx,txn->lsn,txn->xid);
	appendStringInfo(ctx->out, "BEGIN %d", txn->xid);
	ctx->write(ctx,txn->lsn,txn->xid);
	return true;
}

bool
pg_decode_commit_txn( LogicalDecodingContext * ctx,
					 ReorderBufferTXN* txn, XLogRecPtr commit_lsn)
{
	AssertVariableIsOfType(&pg_decode_commit_txn, LogicalDecodeCommitCB);
	/**
	 * we can ignore the begin and commit. slony operates
	 * on SYNC boundaries.
	 */
	elog(NOTICE,"inside of commit");
	ctx->prepare_write(ctx,txn->lsn,txn->xid);
	appendStringInfo(ctx->out, "COMMIT %d", txn->xid);
	ctx->write(ctx,txn->lsn,txn->xid);
	return true;
}



bool
pg_decode_change(LogicalDecodingContext * ctx, ReorderBufferTXN* txn,
				 Oid tableoid, ReorderBufferChange *change)
{

	
	Relation relation = RelationIdGetRelation(tableoid);	
	TupleDesc	tupdesc = RelationGetDescr(relation);
	Form_pg_class class_form = RelationGetForm(relation);
	MemoryContext context = (MemoryContext)ctx->output_plugin_private;
	MemoryContext old = MemoryContextSwitchTo(context);
	int i;
	HeapTuple tuple;
	/**
	 * we build up an array of Datum's so we can convert this
	 * to an array and use array_out to get a text representation.
	 * it might be more efficient to leave everything as 
	 * cstrings and write our own quoting/escaping code
	 */
	Datum		*cmdargs = NULL;
	Datum		*cmdargselem = NULL;
	bool	   *cmdnulls = NULL;
	bool	   *cmdnullselem = NULL;
	int		   cmddims[1];
	int        cmdlbs[1];
	ArrayType  *outvalues;
	const char * array_text;
	Oid arraytypeoutput;
	bool  arraytypeisvarlena;
	HeapTuple array_type_tuple;
	FmgrInfo flinfo;
	char action='?';
	int update_cols=0;
	int table_id=0;
	const char * table_name;
	const char * namespace;
	ctx->prepare_write(ctx,txn->lsn,txn->xid);
	elog(NOTICE,"inside og pg_decode_change");

	
	namespace=get_namespace_name(class_form->relnamespace);
	table_name=NameStr(class_form->relname);

	if( ! is_replicated(namespace,table_name) ) 
	{
		RelationClose(relation);
		MemoryContextSwitchTo(old);
		return false;
	}

	if(change->action == REORDER_BUFFER_CHANGE_INSERT)
	{		
		/**
		 * convert all columns to a pair of arrays (columns and values)
		 */
		tuple=&change->newtuple->tuple;
		action='I';
		cmdargs = cmdargselem = palloc( (relation->rd_att->natts * 2 +2) 
										* sizeof(Datum)  );
		cmdnulls = cmdnullselem = palloc( (relation->rd_att->natts *2 + 2) 
										  * sizeof(bool));
		
		

		for(i = 0; i < relation->rd_att->natts; i++)
		{
			const char * column;			
			const char * value;

			if(tupdesc->attrs[i]->attisdropped)
				continue;
			if(tupdesc->attrs[i]->attnum < 0)
				continue;
			column= NameStr(tupdesc->attrs[i]->attname);
			*cmdargselem++=PointerGetDatum(cstring_to_text(column));
			*cmdnullselem++=false;
			
			value = columnAsText(tupdesc,tuple,i);
			if (value == NULL) 
			{
			  *cmdnullselem++=true;
			  cmdargselem++;
			}
			else
			{
				*cmdnullselem++=false;
				*cmdargselem++=PointerGetDatum(cstring_to_text(value));
			}			
			
		}   
		
		
	}
	else if (change->action == REORDER_BUFFER_CHANGE_UPDATE)
	{
		/**
		 * convert all columns into two pairs of arrays.
		 * one for key columns, one for non-key columns
		 */
		action='U';
		
		/**
		 * we need to fine the columns that make up the unique key.
		 * in the log trigger these were arguments to the trigger
		 * ie (kvvkk).  
		 * our options are:
		 *
		 * 1. search for the unique indexes on the table and pick one
		 * 2. Lookup the index name from sl_table
		 * 3. Have the _init() method load a list of all replicated tables
		 *    for this remote and the associated unique key names.
		 */
		
	}
	else if (change->action == REORDER_BUFFER_CHANGE_DELETE)
	{
	  Relation indexrel;
	  TupleDesc indexdesc;
		/**
		 * convert the key columns to a pair of arrays.
		 */
	  action='D';
	  tuple=&change->oldtuple->tuple;
	  
	  /**
	   * populate relation->rd_primary with the primary or candidate
	   * index used to WAL values that specify which row is being deleted.
	   */
	  RelationGetIndexList(relation);

	  indexrel = RelationIdGetRelation(relation->rd_primary);
	  indexdesc = RelationGetDescr(indexrel);
	  

	  cmdargs = cmdargselem = palloc( (indexrel->rd_att->natts * 2 + 2)
									  * sizeof (Datum));
	  cmdnulls = cmdnullselem = palloc( (indexrel->rd_att->natts * 2 + 2)
										* sizeof(bool));
	  for(i = 0; i < indexrel->rd_att->natts; i++)
	  {
		  const char * column;
		  const char * value;
		  
		  if(indexdesc->attrs[i]->attisdropped)
			  /** you can't drop a column from an index, something is wrong */
			  continue;
		  if(indexdesc->attrs[i]->attnum < 0)
			  continue;
		  column = NameStr(indexdesc->attrs[i]->attname);
		  *cmdargselem++= PointerGetDatum(cstring_to_text(column));
		  *cmdnullselem++=false;
		  value = columnAsText(indexdesc,tuple,i);
		  *cmdnullselem++=false;
		  *cmdargselem++=PointerGetDatum(cstring_to_text(value));
	  }
	  RelationClose(indexrel);
	}
	else
	{
		/**
		 * what else?
		 */
	}   



	cmddims[0] = cmdargselem - cmdargs;
	cmdlbs[0] = 1;
	outvalues= construct_md_array(cmdargs,cmdnulls,1,cmddims,cmdlbs,
								 TEXTOID,-1,false,'i');
	array_type_tuple = SearchSysCache1(TYPEOID,TEXTARRAYOID);
	array_type_tuple->t_tableOid = InvalidOid;
	getTypeOutputInfo(TEXTARRAYOID,&arraytypeoutput,&arraytypeisvarlena);
	fmgr_info(arraytypeoutput,&flinfo);
	array_text = DatumGetCString(FunctionCall1Coll(&flinfo,InvalidOid,
												   PointerGetDatum(outvalues)));
	
	appendStringInfo(ctx->out,"%d,%d,%d,NULL,%s,%s,%c,%d,%s"
					 ,local_id
					 ,txn->xid
					 ,table_id
					 ,namespace
					 ,table_name
					 ,action
					 ,update_cols
					 ,array_text);
	RelationClose(relation);
	ReleaseSysCache(array_type_tuple);
	
	
	
	MemoryContextSwitchTo(old);
	ctx->write(ctx,txn->lsn,txn->xid);
	elog(NOTICE,"leaving og pg_decode_change:");
	return true;
}


bool pg_decode_clean(LogicalDecodingContext * ctx)
{
	return true;
}

/**
 * converts the value stored in the attribute/column specified
 * to a text string.  If the value is NULL then a NULL is returned.
 */
char * columnAsText(TupleDesc tupdesc, HeapTuple tuple,int idx)
{
	Oid typid,typeoutput;
	bool		typisvarlena;
	Form_pg_type ftype;
	bool isnull;
	HeapTuple typeTuple;
	Datum origval,val;
	char * outputstr=NULL;

	typid = tupdesc->attrs[idx]->atttypid;

	typeTuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
	
	if(!HeapTupleIsValid(typeTuple)) 
		elog(ERROR, "cache lookup failed for type %u", typid);
	ftype = (Form_pg_type) GETSTRUCT(typeTuple);
	
	getTypeOutputInfo(typid,
					  &typeoutput, &typisvarlena);

	ReleaseSysCache(typeTuple);
	
	origval = fastgetattr(tuple, idx + 1, tupdesc, &isnull);
	if(typisvarlena && !isnull) 
		val = PointerGetDatum(PG_DETOAST_DATUM(origval));
	else
		val = origval;
	if (isnull)
		return NULL;
	outputstr = OidOutputFunctionCall(typeoutput, val);
	return outputstr;
}

/**
 * checks to see if the table described by class_form is
 * replicated from this origin/provider to the recevier
 * we are running for.
 */
bool is_replicated(const char * namespace,const char * table)
{
	char * search_key = palloc(strlen(namespace) + strlen(table)+2);
	replicated_table * entry;
	bool found;

	sprintf(search_key,"%s.%s",namespace,table);
    entry=hash_search(replicated_tables,
					  &search_key,HASH_FIND,&found);
	
	return found;
	
}