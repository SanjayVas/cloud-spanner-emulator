/*-------------------------------------------------------------------------
 *
 * readfuncs.c
 *	  Reader functions for Postgres tree nodes.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/nodes/readfuncs.c
 *
 * NOTES
 *	  Path nodes do not have any readfuncs support, because we never
 *	  have occasion to read them in.  (There was once code here that
 *	  claimed to read them, but it was broken as well as unused.)  We
 *	  never read executor state trees, either.
 *
 *	  Parse location fields are written out by outfuncs.c, but only for
 *	  debugging use.  When reading a location field, we normally discard
 *	  the stored value and set the location field to -1 (ie, "unknown").
 *	  This is because nodes coming from a stored rule should not be thought
 *	  to have a known location in the current query's text.
 *	  However, if restore_location_fields is true, we do restore location
 *	  fields from the string.  This is currently intended only for use by the
 *	  WRITE_READ_PARSE_PLAN_TREES test code, which doesn't want to cause
 *	  any change in the node contents.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <math.h>

#include "miscadmin.h"
#include "nodes/extensible.h"
#include "nodes/parsenodes.h"
#include "nodes/plannodes.h"
#include "nodes/readfuncs.h"

/*
 * Macros to simplify reading of different kinds of fields.  Use these
 * wherever possible to reduce the chance for silly typos.  Note that these
 * hard-wire conventions about the names of the local variables in a Read
 * routine.
 */

/* Macros for declaring appropriate local variables */

/* A few guys need only local_node */
#define READ_LOCALS_NO_FIELDS(nodeTypeName) \
	nodeTypeName *local_node = makeNode(nodeTypeName)

/* And a few guys need only the pg_strtok support fields */
#define READ_TEMP_LOCALS()	\
	const char *token;		\
	int			length

/* ... but most need both */
#define READ_LOCALS(nodeTypeName)			\
	READ_LOCALS_NO_FIELDS(nodeTypeName);	\
	READ_TEMP_LOCALS()

/* Read an integer field (anything written as ":fldname %d") */
#define READ_INT_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = atoi(token)

/* Read an unsigned integer field (anything written as ":fldname %u") */
#define READ_UINT_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = atoui(token)

/* Read an unsigned integer field (anything written using UINT64_FORMAT) */
#define READ_UINT64_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = strtou64(token, NULL, 10)

/* Read a long integer field (anything written as ":fldname %ld") */
#define READ_LONG_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = atol(token)

/* Read an OID field (don't hard-wire assumption that OID is same as uint) */
#define READ_OID_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = atooid(token)

/* Read a char field (ie, one ascii character) */
#define READ_CHAR_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	/* avoid overhead of calling debackslash() for one char */ \
	local_node->fldname = (length == 0) ? '\0' : (token[0] == '\\' ? token[1] : token[0])

/* Read an enumerated-type field that was written as an integer code */
#define READ_ENUM_FIELD(fldname, enumtype) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = (enumtype) atoi(token)

/* Read a float field */
#define READ_FLOAT_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = atof(token)

/* Read a boolean field */
#define READ_BOOL_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = strtobool(token)

/* Read a character-string field */
#define READ_STRING_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = nullable_string(token, length)

/* Read a parse location field (and possibly throw away the value) */
#ifdef WRITE_READ_PARSE_PLAN_TREES
#define READ_LOCATION_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = restore_location_fields ? atoi(token) : -1
#else
#define READ_LOCATION_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	(void) token;				/* in case not used elsewhere */ \
	local_node->fldname = atoi(token)
#endif

/* Read a Node field */
#define READ_NODE_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	(void) token;				/* in case not used elsewhere */ \
	local_node->fldname = nodeRead(NULL, 0)

/* Read a bitmapset field */
#define READ_BITMAPSET_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	(void) token;				/* in case not used elsewhere */ \
	local_node->fldname = _readBitmapset()

/* Read an attribute number array */
#define READ_ATTRNUMBER_ARRAY(fldname, len) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	local_node->fldname = readAttrNumberCols(len)

/* Read an oid array */
#define READ_OID_ARRAY(fldname, len) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	local_node->fldname = readOidCols(len)

/* Read an int array */
#define READ_INT_ARRAY(fldname, len) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	local_node->fldname = readIntCols(len)

/* Read a bool array */
#define READ_BOOL_ARRAY(fldname, len) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	local_node->fldname = readBoolCols(len)

/* Routine exit */
#define READ_DONE() \
	return local_node


/*
 * NOTE: use atoi() to read values written with %d, or atoui() to read
 * values written with %u in outfuncs.c.  An exception is OID values,
 * for which use atooid().  (As of 7.1, outfuncs.c writes OIDs as %u,
 * but this will probably change in the future.)
 */
#define atoui(x)  ((unsigned int) strtoul((x), NULL, 10))

#define strtobool(x)  ((*(x) == 't') ? true : false)

#define nullable_string(token,length)  \
	((length) == 0 ? NULL : debackslash(token, length))


/*
 * _readBitmapset
 */
static Bitmapset *
_readBitmapset(void)
{
	Bitmapset  *result = NULL;

	READ_TEMP_LOCALS();

	token = pg_strtok(&length);
	if (token == NULL)
		elog(ERROR, "incomplete Bitmapset structure");
	if (length != 1 || token[0] != '(')
		elog(ERROR, "unrecognized token: \"%.*s\"", length, token);

	token = pg_strtok(&length);
	if (token == NULL)
		elog(ERROR, "incomplete Bitmapset structure");
	if (length != 1 || token[0] != 'b')
		elog(ERROR, "unrecognized token: \"%.*s\"", length, token);

	for (;;)
	{
		int			val;
		char	   *endptr;

		token = pg_strtok(&length);
		if (token == NULL)
			elog(ERROR, "unterminated Bitmapset structure");
		if (length == 1 && token[0] == ')')
			break;
		val = (int) strtol(token, &endptr, 10);
		if (endptr != token + length)
			elog(ERROR, "unrecognized integer: \"%.*s\"", length, token);
		result = bms_add_member(result, val);
	}

	return result;
}

/*
 * for use by extensions which define extensible nodes
 */
Bitmapset *
readBitmapset(void)
{
	return _readBitmapset();
}

/*
 * _readQuery
 */
static Query *
_readQuery(void)
{
	READ_LOCALS(Query);

	READ_ENUM_FIELD(commandType, CmdType);
	READ_ENUM_FIELD(querySource, QuerySource);
	local_node->queryId = UINT64CONST(0);	/* not saved in output format */
	READ_BOOL_FIELD(canSetTag);
	READ_NODE_FIELD(utilityStmt);
	READ_INT_FIELD(resultRelation);
	READ_BOOL_FIELD(hasAggs);
	READ_BOOL_FIELD(hasWindowFuncs);
	READ_BOOL_FIELD(hasTargetSRFs);
	READ_BOOL_FIELD(hasSubLinks);
	READ_BOOL_FIELD(hasDistinctOn);
	READ_BOOL_FIELD(hasRecursive);
	READ_BOOL_FIELD(hasModifyingCTE);
	READ_BOOL_FIELD(hasForUpdate);
	READ_BOOL_FIELD(hasRowSecurity);
	READ_BOOL_FIELD(isReturn);
	READ_NODE_FIELD(cteList);
	READ_NODE_FIELD(rtable);
	READ_NODE_FIELD(jointree);
	READ_NODE_FIELD(targetList);
	READ_ENUM_FIELD(override, OverridingKind);
	READ_NODE_FIELD(onConflict);
	READ_NODE_FIELD(returningList);
	READ_NODE_FIELD(groupClause);
	READ_BOOL_FIELD(groupDistinct);
	READ_NODE_FIELD(groupingSets);
	READ_NODE_FIELD(havingQual);
	READ_NODE_FIELD(windowClause);
	READ_NODE_FIELD(distinctClause);
	READ_NODE_FIELD(sortClause);
	READ_NODE_FIELD(limitOffset);
	READ_NODE_FIELD(limitCount);
	READ_ENUM_FIELD(limitOption, LimitOption);
	READ_NODE_FIELD(rowMarks);
	READ_NODE_FIELD(setOperations);
	READ_NODE_FIELD(constraintDeps);
	READ_NODE_FIELD(withCheckOptions);
	READ_NODE_FIELD(mergeActionList);
	READ_BOOL_FIELD(mergeUseOuterJoin);
	READ_LOCATION_FIELD(stmt_location);
	READ_INT_FIELD(stmt_len);
	READ_NODE_FIELD(statementHints);

	READ_DONE();
}

/*
 * _readNotifyStmt
 */
static NotifyStmt *
_readNotifyStmt(void)
{
	READ_LOCALS(NotifyStmt);

	READ_STRING_FIELD(conditionname);
	READ_STRING_FIELD(payload);

	READ_DONE();
}

/*
 * _readDeclareCursorStmt
 */
static DeclareCursorStmt *
_readDeclareCursorStmt(void)
{
	READ_LOCALS(DeclareCursorStmt);

	READ_STRING_FIELD(portalname);
	READ_INT_FIELD(options);
	READ_NODE_FIELD(query);

	READ_DONE();
}

/* SPANGRES BEGIN */
/*
 * _readSelectStmt
 */
static SelectStmt *
_readSelectStmt(void)
{
       READ_LOCALS(SelectStmt);

       READ_NODE_FIELD(distinctClause);
       READ_NODE_FIELD(intoClause);
       READ_NODE_FIELD(targetList);
       READ_NODE_FIELD(fromClause);
       READ_NODE_FIELD(whereClause);
       READ_NODE_FIELD(groupClause);
			 READ_BOOL_FIELD(groupDistinct);
       READ_NODE_FIELD(havingClause);
       READ_NODE_FIELD(windowClause);
       READ_NODE_FIELD(valuesLists);
       READ_NODE_FIELD(sortClause);
       READ_NODE_FIELD(limitOffset);
       READ_NODE_FIELD(limitCount);
       READ_ENUM_FIELD(limitOption, LimitOption);
       READ_NODE_FIELD(lockingClause);
       READ_NODE_FIELD(withClause);
       READ_ENUM_FIELD(op, SetOperation);
       READ_BOOL_FIELD(all);
       READ_NODE_FIELD(larg);
       READ_NODE_FIELD(rarg);

       READ_DONE();
}
/* SPANGRES END */

/*
 * _readWithCheckOption
 */
static WithCheckOption *
_readWithCheckOption(void)
{
	READ_LOCALS(WithCheckOption);

	READ_ENUM_FIELD(kind, WCOKind);
	READ_STRING_FIELD(relname);
	READ_STRING_FIELD(polname);
	READ_NODE_FIELD(qual);
	READ_BOOL_FIELD(cascaded);

	READ_DONE();
}

/*
 * _readSortGroupClause
 */
static SortGroupClause *
_readSortGroupClause(void)
{
	READ_LOCALS(SortGroupClause);

	READ_UINT_FIELD(tleSortGroupRef);
	READ_OID_FIELD(eqop);
	READ_OID_FIELD(sortop);
	READ_BOOL_FIELD(nulls_first);
	READ_BOOL_FIELD(hashable);

	READ_DONE();
}

/*
 * _readGroupingSet
 */
static GroupingSet *
_readGroupingSet(void)
{
	READ_LOCALS(GroupingSet);

	READ_ENUM_FIELD(kind, GroupingSetKind);
	READ_NODE_FIELD(content);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readWindowClause
 */
static WindowClause *
_readWindowClause(void)
{
	READ_LOCALS(WindowClause);

	READ_STRING_FIELD(name);
	READ_STRING_FIELD(refname);
	READ_NODE_FIELD(partitionClause);
	READ_NODE_FIELD(orderClause);
	READ_INT_FIELD(frameOptions);
	READ_NODE_FIELD(startOffset);
	READ_NODE_FIELD(endOffset);
	READ_NODE_FIELD(runCondition);
	READ_OID_FIELD(startInRangeFunc);
	READ_OID_FIELD(endInRangeFunc);
	READ_OID_FIELD(inRangeColl);
	READ_BOOL_FIELD(inRangeAsc);
	READ_BOOL_FIELD(inRangeNullsFirst);
	READ_UINT_FIELD(winref);
	READ_BOOL_FIELD(copiedOrder);

	READ_DONE();
}

/*
 * _readRowMarkClause
 */
static RowMarkClause *
_readRowMarkClause(void)
{
	READ_LOCALS(RowMarkClause);

	READ_UINT_FIELD(rti);
	READ_ENUM_FIELD(strength, LockClauseStrength);
	READ_ENUM_FIELD(waitPolicy, LockWaitPolicy);
	READ_BOOL_FIELD(pushedDown);

	READ_DONE();
}

/*
 * _readCTESearchClause
 */
static CTESearchClause *
_readCTESearchClause(void)
{
	READ_LOCALS(CTESearchClause);

	READ_NODE_FIELD(search_col_list);
	READ_BOOL_FIELD(search_breadth_first);
	READ_STRING_FIELD(search_seq_column);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readCTECycleClause
 */
static CTECycleClause *
_readCTECycleClause(void)
{
	READ_LOCALS(CTECycleClause);

	READ_NODE_FIELD(cycle_col_list);
	READ_STRING_FIELD(cycle_mark_column);
	READ_NODE_FIELD(cycle_mark_value);
	READ_NODE_FIELD(cycle_mark_default);
	READ_STRING_FIELD(cycle_path_column);
	READ_LOCATION_FIELD(location);
	READ_OID_FIELD(cycle_mark_type);
	READ_INT_FIELD(cycle_mark_typmod);
	READ_OID_FIELD(cycle_mark_collation);
	READ_OID_FIELD(cycle_mark_neop);

	READ_DONE();
}

/*
 * _readCommonTableExpr
 */
static CommonTableExpr *
_readCommonTableExpr(void)
{
	READ_LOCALS(CommonTableExpr);

	READ_STRING_FIELD(ctename);
	READ_NODE_FIELD(aliascolnames);
	READ_ENUM_FIELD(ctematerialized, CTEMaterialize);
	READ_NODE_FIELD(ctequery);
	READ_NODE_FIELD(search_clause);
	READ_NODE_FIELD(cycle_clause);
	READ_LOCATION_FIELD(location);
	READ_BOOL_FIELD(cterecursive);
	READ_INT_FIELD(cterefcount);
	READ_NODE_FIELD(ctecolnames);
	READ_NODE_FIELD(ctecoltypes);
	READ_NODE_FIELD(ctecoltypmods);
	READ_NODE_FIELD(ctecolcollations);

	READ_DONE();
}

/*
 * _readMergeWhenClause
 */
static MergeWhenClause *
_readMergeWhenClause(void)
{
	READ_LOCALS(MergeWhenClause);

	READ_BOOL_FIELD(matched);
	READ_ENUM_FIELD(commandType, CmdType);
	READ_ENUM_FIELD(override, OverridingKind);
	READ_NODE_FIELD(condition);
	READ_NODE_FIELD(targetList);
	READ_NODE_FIELD(values);

	READ_DONE();
}

/*
 * _readMergeAction
 */
static MergeAction *
_readMergeAction(void)
{
	READ_LOCALS(MergeAction);

	READ_BOOL_FIELD(matched);
	READ_ENUM_FIELD(commandType, CmdType);
	READ_ENUM_FIELD(override, OverridingKind);
	READ_NODE_FIELD(qual);
	READ_NODE_FIELD(targetList);
	READ_NODE_FIELD(updateColnos);

	READ_DONE();
}

/*
 * _readSetOperationStmt
 */
static SetOperationStmt *
_readSetOperationStmt(void)
{
	READ_LOCALS(SetOperationStmt);

	READ_ENUM_FIELD(op, SetOperation);
	READ_BOOL_FIELD(all);
	READ_NODE_FIELD(larg);
	READ_NODE_FIELD(rarg);
	READ_NODE_FIELD(colTypes);
	READ_NODE_FIELD(colTypmods);
	READ_NODE_FIELD(colCollations);
	READ_NODE_FIELD(groupClauses);

	READ_DONE();
}


/*
 *	Stuff from primnodes.h.
 */

static Alias *
_readAlias(void)
{
	READ_LOCALS(Alias);

	READ_STRING_FIELD(aliasname);
	READ_NODE_FIELD(colnames);

	READ_DONE();
}

static RangeVar *
_readRangeVar(void)
{
	READ_LOCALS(RangeVar);

	/* SPANGRES BEGIN */
	// local_node->catalogname = NULL; /* not currently saved in output format */
	// `catalogname` is changed to be saved in output format in outfuncs.c.
	READ_STRING_FIELD(catalogname);
	/* SPANGRES END */
	READ_STRING_FIELD(schemaname);
	READ_STRING_FIELD(relname);
	READ_BOOL_FIELD(inh);
	READ_CHAR_FIELD(relpersistence);
	READ_NODE_FIELD(alias);
	READ_LOCATION_FIELD(location);
	READ_NODE_FIELD(tableHints);

	READ_DONE();
}

/*
 * _readTableFunc
 */
static TableFunc *
_readTableFunc(void)
{
	READ_LOCALS(TableFunc);

	READ_NODE_FIELD(ns_uris);
	READ_NODE_FIELD(ns_names);
	READ_NODE_FIELD(docexpr);
	READ_NODE_FIELD(rowexpr);
	READ_NODE_FIELD(colnames);
	READ_NODE_FIELD(coltypes);
	READ_NODE_FIELD(coltypmods);
	READ_NODE_FIELD(colcollations);
	READ_NODE_FIELD(colexprs);
	READ_NODE_FIELD(coldefexprs);
	READ_BITMAPSET_FIELD(notnulls);
	READ_INT_FIELD(ordinalitycol);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

static IntoClause *
_readIntoClause(void)
{
	READ_LOCALS(IntoClause);

	READ_NODE_FIELD(rel);
	READ_NODE_FIELD(colNames);
	READ_STRING_FIELD(accessMethod);
	READ_NODE_FIELD(options);
	READ_ENUM_FIELD(onCommit, OnCommitAction);
	READ_STRING_FIELD(tableSpaceName);
	READ_NODE_FIELD(viewQuery);
	READ_BOOL_FIELD(skipData);

	READ_DONE();
}

/*
 * _readVar
 */
static Var *
_readVar(void)
{
	READ_LOCALS(Var);

	READ_INT_FIELD(varno);
	READ_INT_FIELD(varattno);
	READ_OID_FIELD(vartype);
	READ_INT_FIELD(vartypmod);
	READ_OID_FIELD(varcollid);
	READ_UINT_FIELD(varlevelsup);
	READ_UINT_FIELD(varnosyn);
	READ_INT_FIELD(varattnosyn);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/* SPANGRES BEGIN */
/*
 * _readRawStmt
 */
static RawStmt *
_readRawStmt(void)
{
	READ_LOCALS(RawStmt);

	READ_NODE_FIELD(stmt);
	READ_LOCATION_FIELD(stmt_location);
	READ_INT_FIELD(stmt_len);
	READ_NODE_FIELD(statementHints);

	READ_DONE();
}

/*
 * _readResTarget
 */
static ResTarget *
_readResTarget(void)
{
	READ_LOCALS(ResTarget);

	READ_STRING_FIELD(name);
	READ_NODE_FIELD(indirection);
	READ_NODE_FIELD(val);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

static MultiAssignRef *
_readMultiAssignRef(void)
{
	READ_LOCALS(MultiAssignRef);

	READ_NODE_FIELD(source);
	READ_INT_FIELD(colno);
	READ_INT_FIELD(ncolumns);

	READ_DONE();
}

static bool TokenIsInteger(const char* token, size_t len) {
	// Empty strings are not integers
	if (token == NULL || len == 0) {
		return false;
	}
	// Negative integers can still be integers.
	// Also, things that start with digits can be integers.
	// Anything else is definitely not an integer.
	if (token[0] != '-' && (token[0] < '0' || token[0] > '9')) {
		return false;
	}
	// Other than the first character, integers exclusively contain digits.
	for (size_t i = 1; i < len; i++) {
		if (token[i] < '0' || token[i] > '9') {
			return false;
		}
	}
	return true;
}

/*
 * _readValue
 *
 * This function decodes a ":val" field, whose value is a token representing
 * a literal value.  It handles numbers (with a special case for int32),
 * strings, bitstrings, and NULL.
 * It is intended to be an exact inverse of `_outValue()` in outfuncs.c.
 */
static void
_readValue(A_Const *aconst)
{
	READ_TEMP_LOCALS();
	token = pg_strtok(&length); /* check NULL or skip ::val */

	if (length == 4 &&
			token[0] == 'N' && token[1] == 'U' &&
			token[2] == 'L' && token[3] == 'L') {
		aconst->isnull = true;
		return;
	}

	token = pg_strtok(&length);

	if (token[0] == '-' || (token[0] >= '0' && token[0] <= '9')) {
		// Treat integers that don't fit into 32 bits as floats.
		// (We later parse floats as numerics, so no precision is lost.)
		int64 int64_value = atol(token);
		int32 int32_value = int64_value;
		if (TokenIsInteger(token, length) && int64_value == (int64)int32_value) {
			aconst->val.node.type = T_Integer;
			aconst->val.ival.ival = int32_value;
		} else {
			// "Float" really means "Not a simple int32; keep as a string for no"
			aconst->val.node.type = T_Float;
			aconst->val.fval.fval = pnstrdup(token, length);
		}
	}
	else if (token[0] == '"' && token[length-1] == '"') {
		aconst->val.node.type = T_String;
		aconst->val.sval.sval = debackslash(token+1, length-2);
	}
	else if (token[0] == 'b') {
		// Sanity check
		if (token[1] != '"' || token[length-1] != '"') {
		  elog(ERROR, "Unrecognized bit string value at %.32s", token);
		}
		aconst->val.node.type = T_BitString;
		// Strip quotes and backslashes, but keep the leading 'b'.
		char* payload = debackslash(token+2, length-3);
		aconst->val.bsval.bsval = palloc(length-1);
		snprintf(aconst->val.bsval.bsval, length-1, "b%s", payload);
		pfree(payload);
	}
	else if (length == 5 &&
					 token[0] == 'f' && token[1] == 'a' && token[2] == 'l'&&
					 token[3] == 's' && token[4] == 'e') {
		aconst->val.node.type = T_Boolean;
		aconst->val.boolval.boolval = false;
	}
	else if (length == 4 &&
	         token[0] == 't' && token[1] == 'r' &&
					 token[2] == 'u' && token[3] == 'e') {
		aconst->val.node.type = T_Boolean;
		aconst->val.boolval.boolval = true;
	}
	else {
		elog(ERROR, "Unrecognized value at %.32s", token);
	}
}

/*
 * _readAConst
 */
static A_Const *
_readAConst(void)
{
	READ_LOCALS(A_Const);

	_readValue(local_node);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readTypeCast
 */
static TypeCast *
_readTypeCast(void)
{
	READ_LOCALS(TypeCast);

	READ_NODE_FIELD(arg);
	READ_NODE_FIELD(typeName);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readTypeName
 */
static TypeName *
_readTypeName(void)
{
	READ_LOCALS(TypeName);

	READ_NODE_FIELD(names);
	READ_OID_FIELD(typeOid);
	READ_BOOL_FIELD(setof);
	READ_BOOL_FIELD(pct_type);
	READ_NODE_FIELD(typmods);
	READ_INT_FIELD(typemod);
	READ_NODE_FIELD(arrayBounds);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readFuncCall
 */
static FuncCall *
_readFuncCall(void)
{

	READ_LOCALS(FuncCall);

	READ_NODE_FIELD(funcname);
	READ_NODE_FIELD(args);
	READ_NODE_FIELD(agg_order);
	READ_NODE_FIELD(agg_filter);
	READ_NODE_FIELD(over);
	READ_BOOL_FIELD(agg_within_group);
	READ_BOOL_FIELD(agg_star);
	READ_BOOL_FIELD(agg_distinct);
	READ_BOOL_FIELD(func_variadic);
	READ_ENUM_FIELD(funcformat, CoercionForm);
	READ_LOCATION_FIELD(location);
	READ_NODE_FIELD(functionHints);

	READ_DONE();
}

/*
 * _readColumnRef
 */
static ColumnRef *
_readColumnRef(void)
{
	READ_LOCALS(ColumnRef);

	READ_NODE_FIELD(fields);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readParamRef
 */
static ParamRef *
_readParamRef(void)
{
	READ_LOCALS(ParamRef);

	READ_INT_FIELD(number);
  READ_LOCATION_FIELD(location);

  READ_DONE();
}

/* SPANGRES END */

/*
 * _readConst
 */
static Const *
_readConst(void)
{
	READ_LOCALS(Const);

	READ_OID_FIELD(consttype);
	READ_INT_FIELD(consttypmod);
	READ_OID_FIELD(constcollid);
	READ_INT_FIELD(constlen);
	READ_BOOL_FIELD(constbyval);
	READ_BOOL_FIELD(constisnull);
	READ_LOCATION_FIELD(location);

	token = pg_strtok(&length); /* skip :constvalue */
	if (local_node->constisnull)
		token = pg_strtok(&length); /* skip "<>" */
	else
		local_node->constvalue = readDatum(local_node->constbyval);

	READ_DONE();
}

/*
 * _readParam
 */
static Param *
_readParam(void)
{
	READ_LOCALS(Param);

	READ_ENUM_FIELD(paramkind, ParamKind);
	READ_INT_FIELD(paramid);
	READ_OID_FIELD(paramtype);
	READ_INT_FIELD(paramtypmod);
	READ_OID_FIELD(paramcollid);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readAggref
 */
static Aggref *
_readAggref(void)
{
	READ_LOCALS(Aggref);

	READ_OID_FIELD(aggfnoid);
	READ_OID_FIELD(aggtype);
	READ_OID_FIELD(aggcollid);
	READ_OID_FIELD(inputcollid);
	READ_OID_FIELD(aggtranstype);
	READ_NODE_FIELD(aggargtypes);
	READ_NODE_FIELD(aggdirectargs);
	READ_NODE_FIELD(args);
	READ_NODE_FIELD(aggorder);
	READ_NODE_FIELD(aggdistinct);
	READ_NODE_FIELD(aggfilter);
	READ_BOOL_FIELD(aggstar);
	READ_BOOL_FIELD(aggvariadic);
	READ_CHAR_FIELD(aggkind);
	READ_UINT_FIELD(agglevelsup);
	READ_ENUM_FIELD(aggsplit, AggSplit);
	READ_INT_FIELD(aggno);
	READ_INT_FIELD(aggtransno);
	READ_LOCATION_FIELD(location);
	READ_NODE_FIELD(functionHints);

	READ_DONE();
}

/*
 * _readGroupingFunc
 */
static GroupingFunc *
_readGroupingFunc(void)
{
	READ_LOCALS(GroupingFunc);

	READ_NODE_FIELD(args);
	READ_NODE_FIELD(refs);
	READ_NODE_FIELD(cols);
	READ_UINT_FIELD(agglevelsup);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readWindowFunc
 */
static WindowFunc *
_readWindowFunc(void)
{
	READ_LOCALS(WindowFunc);

	READ_OID_FIELD(winfnoid);
	READ_OID_FIELD(wintype);
	READ_OID_FIELD(wincollid);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_NODE_FIELD(aggfilter);
	READ_UINT_FIELD(winref);
	READ_BOOL_FIELD(winstar);
	READ_BOOL_FIELD(winagg);
	READ_LOCATION_FIELD(location);
	READ_NODE_FIELD(functionHints);

	READ_DONE();
}

/*
 * _readSubscriptingRef
 */
static SubscriptingRef *
_readSubscriptingRef(void)
{
	READ_LOCALS(SubscriptingRef);

	READ_OID_FIELD(refcontainertype);
	READ_OID_FIELD(refelemtype);
	READ_OID_FIELD(refrestype);
	READ_INT_FIELD(reftypmod);
	READ_OID_FIELD(refcollid);
	READ_NODE_FIELD(refupperindexpr);
	READ_NODE_FIELD(reflowerindexpr);
	READ_NODE_FIELD(refexpr);
	READ_NODE_FIELD(refassgnexpr);

	READ_DONE();
}

/*
 * _readFuncExpr
 */
static FuncExpr *
_readFuncExpr(void)
{
	READ_LOCALS(FuncExpr);

	READ_OID_FIELD(funcid);
	READ_OID_FIELD(funcresulttype);
	READ_BOOL_FIELD(funcretset);
	READ_BOOL_FIELD(funcvariadic);
	READ_ENUM_FIELD(funcformat, CoercionForm);
	READ_OID_FIELD(funccollid);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);
	READ_NODE_FIELD(functionHints);

	READ_DONE();
}

/*
 * _readNamedArgExpr
 */
static NamedArgExpr *
_readNamedArgExpr(void)
{
	READ_LOCALS(NamedArgExpr);

	READ_NODE_FIELD(arg);
	READ_STRING_FIELD(name);
	READ_INT_FIELD(argnumber);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readOpExpr
 */
static OpExpr *
_readOpExpr(void)
{
	READ_LOCALS(OpExpr);

	READ_OID_FIELD(opno);
	READ_OID_FIELD(opfuncid);
	READ_OID_FIELD(opresulttype);
	READ_BOOL_FIELD(opretset);
	READ_OID_FIELD(opcollid);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readAExpr
 */
static A_Expr*
_readAExpr(void)
{
  READ_LOCALS(A_Expr);

  /* SPANGRES BEGIN */
  // The serializer may write just the name, a token after the name, a token
  // before the name, or just a token. Handle the different cases based on the
  // A_Expr_Kind.
  READ_ENUM_FIELD(kind, A_Expr_Kind);
  switch (local_node->kind)
  {
  case AEXPR_OP: {
	  READ_NODE_FIELD(name);
	  break;
  }
  case AEXPR_OP_ANY:
  case AEXPR_OP_ALL: {
	  READ_NODE_FIELD(name);
	  token = pg_strtok(&length); // skip ANY and ALL after name.
	  break;
  }
  case AEXPR_DISTINCT:
  case AEXPR_NOT_DISTINCT:
  case AEXPR_NULLIF:
  case AEXPR_IN:
  case AEXPR_LIKE:
  case AEXPR_ILIKE:
  case AEXPR_SIMILAR:
  case AEXPR_BETWEEN:
  case AEXPR_NOT_BETWEEN:
  case AEXPR_BETWEEN_SYM:
  case AEXPR_NOT_BETWEEN_SYM: {
	  token = pg_strtok(&length); // skip tokens before name.
	  READ_NODE_FIELD(name);
	  break;
  }
  default: {
	  token = pg_strtok(&length); // skip PAREN and ??. No name for these cases.
	  break;
  }
  }
  /* SPANGRES END */

  READ_NODE_FIELD(lexpr);
  READ_NODE_FIELD(rexpr);
  READ_LOCATION_FIELD(location);

  READ_DONE();
}

/*
 * _readA_Star
 */
static A_Star*
_readA_Star(void)
{
  READ_LOCALS_NO_FIELDS(A_Star);

  READ_DONE();
}

/*
 * _readA_ArrayExpr
 */
static A_ArrayExpr*
_readA_ArrayExpr(void)
{
  READ_LOCALS(A_ArrayExpr);

  READ_NODE_FIELD(elements);
  READ_LOCATION_FIELD(location);

  READ_DONE();
}

/*
 * _readRangeSubselect
 */
static RangeSubselect*
_readRangeSubselect(void)
{
  READ_LOCALS(RangeSubselect);

  READ_BOOL_FIELD(lateral);
  READ_NODE_FIELD(subquery);
  READ_NODE_FIELD(alias);

  READ_DONE();
}

/*
 * _readCreateStmt
 */
static CreateStmt*
_readCreateStmt(void)
{
  READ_LOCALS(CreateStmt);

	READ_NODE_FIELD(relation);
	READ_NODE_FIELD(tableElts);
	READ_NODE_FIELD(inhRelations);
	READ_NODE_FIELD(partspec);
	READ_NODE_FIELD(partbound);
	READ_NODE_FIELD(ofTypename);
	READ_NODE_FIELD(constraints);
	READ_NODE_FIELD(options);
	READ_ENUM_FIELD(oncommit, OnCommitAction);
	READ_STRING_FIELD(tablespacename);
	READ_NODE_FIELD(locality_group_name);
	READ_STRING_FIELD(accessMethod);
	READ_BOOL_FIELD(if_not_exists);
	READ_NODE_FIELD(interleavespec);
	READ_NODE_FIELD(ttl);

	READ_DONE();
}

/*
 * _readColumnDef
 */
static ColumnDef*
_readColumnDef(void)
{
  READ_LOCALS(ColumnDef);

	READ_STRING_FIELD(colname);
	READ_NODE_FIELD(typeName);
	READ_STRING_FIELD(compression);
	READ_INT_FIELD(inhcount);
	READ_BOOL_FIELD(is_local);
	READ_BOOL_FIELD(is_not_null);
	READ_BOOL_FIELD(is_from_type);
	READ_CHAR_FIELD(storage);
	READ_NODE_FIELD(raw_default);
	READ_NODE_FIELD(cooked_default);
	READ_CHAR_FIELD(identity);
	READ_NODE_FIELD(identitySequence);
	READ_CHAR_FIELD(generated);
	READ_NODE_FIELD(collClause);
	READ_OID_FIELD(collOid);
	READ_NODE_FIELD(constraints);
	READ_NODE_FIELD(fdwoptions);
	READ_LOCATION_FIELD(location);
	READ_NODE_FIELD(locality_group_name);

  READ_DONE();
 }

/*
 * _readDistinctExpr
 */
static DistinctExpr *
_readDistinctExpr(void)
{
	READ_LOCALS(DistinctExpr);

	READ_OID_FIELD(opno);
	READ_OID_FIELD(opfuncid);
	READ_OID_FIELD(opresulttype);
	READ_BOOL_FIELD(opretset);
	READ_OID_FIELD(opcollid);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readNullIfExpr
 */
static NullIfExpr *
_readNullIfExpr(void)
{
	READ_LOCALS(NullIfExpr);

	READ_OID_FIELD(opno);
	READ_OID_FIELD(opfuncid);
	READ_OID_FIELD(opresulttype);
	READ_BOOL_FIELD(opretset);
	READ_OID_FIELD(opcollid);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readScalarArrayOpExpr
 */
static ScalarArrayOpExpr *
_readScalarArrayOpExpr(void)
{
	READ_LOCALS(ScalarArrayOpExpr);

	READ_OID_FIELD(opno);
	READ_OID_FIELD(opfuncid);
	READ_OID_FIELD(hashfuncid);
	READ_OID_FIELD(negfuncid);
	READ_BOOL_FIELD(useOr);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readBoolExpr
 */
static BoolExpr *
_readBoolExpr(void)
{
	READ_LOCALS(BoolExpr);

	/* do-it-yourself enum representation */
	token = pg_strtok(&length); /* skip :boolop */
	token = pg_strtok(&length); /* get field value */
	if (strncmp(token, "and", 3) == 0)
		local_node->boolop = AND_EXPR;
	else if (strncmp(token, "or", 2) == 0)
		local_node->boolop = OR_EXPR;
	else if (strncmp(token, "not", 3) == 0)
		local_node->boolop = NOT_EXPR;
	else
		elog(ERROR, "unrecognized boolop \"%.*s\"", length, token);

	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readSubLink
 */
static SubLink *
_readSubLink(void)
{
	READ_LOCALS(SubLink);

	READ_ENUM_FIELD(subLinkType, SubLinkType);
	READ_INT_FIELD(subLinkId);
	READ_NODE_FIELD(testexpr);
	READ_NODE_FIELD(operName);
	READ_NODE_FIELD(subselect);
	READ_LOCATION_FIELD(location);
	READ_NODE_FIELD(joinHints);

	READ_DONE();
}

/*
 * _readSubPlan is not needed since it doesn't appear in stored rules.
 */

/*
 * _readFieldSelect
 */
static FieldSelect *
_readFieldSelect(void)
{
	READ_LOCALS(FieldSelect);

	READ_NODE_FIELD(arg);
	READ_INT_FIELD(fieldnum);
	READ_OID_FIELD(resulttype);
	READ_INT_FIELD(resulttypmod);
	READ_OID_FIELD(resultcollid);

	READ_DONE();
}

/*
 * _readFieldStore
 */
static FieldStore *
_readFieldStore(void)
{
	READ_LOCALS(FieldStore);

	READ_NODE_FIELD(arg);
	READ_NODE_FIELD(newvals);
	READ_NODE_FIELD(fieldnums);
	READ_OID_FIELD(resulttype);

	READ_DONE();
}

/*
 * _readRelabelType
 */
static RelabelType *
_readRelabelType(void)
{
	READ_LOCALS(RelabelType);

	READ_NODE_FIELD(arg);
	READ_OID_FIELD(resulttype);
	READ_INT_FIELD(resulttypmod);
	READ_OID_FIELD(resultcollid);
	READ_ENUM_FIELD(relabelformat, CoercionForm);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readCoerceViaIO
 */
static CoerceViaIO *
_readCoerceViaIO(void)
{
	READ_LOCALS(CoerceViaIO);

	READ_NODE_FIELD(arg);
	READ_OID_FIELD(resulttype);
	READ_OID_FIELD(resultcollid);
	READ_ENUM_FIELD(coerceformat, CoercionForm);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readArrayCoerceExpr
 */
static ArrayCoerceExpr *
_readArrayCoerceExpr(void)
{
	READ_LOCALS(ArrayCoerceExpr);

	READ_NODE_FIELD(arg);
	READ_NODE_FIELD(elemexpr);
	READ_OID_FIELD(resulttype);
	READ_INT_FIELD(resulttypmod);
	READ_OID_FIELD(resultcollid);
	READ_ENUM_FIELD(coerceformat, CoercionForm);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readConvertRowtypeExpr
 */
static ConvertRowtypeExpr *
_readConvertRowtypeExpr(void)
{
	READ_LOCALS(ConvertRowtypeExpr);

	READ_NODE_FIELD(arg);
	READ_OID_FIELD(resulttype);
	READ_ENUM_FIELD(convertformat, CoercionForm);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readCollateExpr
 */
static CollateExpr *
_readCollateExpr(void)
{
	READ_LOCALS(CollateExpr);

	READ_NODE_FIELD(arg);
	READ_OID_FIELD(collOid);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readCaseExpr
 */
static CaseExpr *
_readCaseExpr(void)
{
	READ_LOCALS(CaseExpr);

	READ_OID_FIELD(casetype);
	READ_OID_FIELD(casecollid);
	READ_NODE_FIELD(arg);
	READ_NODE_FIELD(args);
	READ_NODE_FIELD(defresult);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readCaseWhen
 */
static CaseWhen *
_readCaseWhen(void)
{
	READ_LOCALS(CaseWhen);

	READ_NODE_FIELD(expr);
	READ_NODE_FIELD(result);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readCaseTestExpr
 */
static CaseTestExpr *
_readCaseTestExpr(void)
{
	READ_LOCALS(CaseTestExpr);

	READ_OID_FIELD(typeId);
	READ_INT_FIELD(typeMod);
	READ_OID_FIELD(collation);

	READ_DONE();
}

/*
 * _readArrayExpr
 */
static ArrayExpr *
_readArrayExpr(void)
{
	READ_LOCALS(ArrayExpr);

	READ_OID_FIELD(array_typeid);
	READ_OID_FIELD(array_collid);
	READ_OID_FIELD(element_typeid);
	READ_NODE_FIELD(elements);
	READ_BOOL_FIELD(multidims);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readRowExpr
 */
static RowExpr *
_readRowExpr(void)
{
	READ_LOCALS(RowExpr);

	READ_NODE_FIELD(args);
	READ_OID_FIELD(row_typeid);
	READ_ENUM_FIELD(row_format, CoercionForm);
	READ_NODE_FIELD(colnames);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readRowCompareExpr
 */
static RowCompareExpr *
_readRowCompareExpr(void)
{
	READ_LOCALS(RowCompareExpr);

	READ_ENUM_FIELD(rctype, RowCompareType);
	READ_NODE_FIELD(opnos);
	READ_NODE_FIELD(opfamilies);
	READ_NODE_FIELD(inputcollids);
	READ_NODE_FIELD(largs);
	READ_NODE_FIELD(rargs);

	READ_DONE();
}

/*
 * _readCoalesceExpr
 */
static CoalesceExpr *
_readCoalesceExpr(void)
{
	READ_LOCALS(CoalesceExpr);

	READ_OID_FIELD(coalescetype);
	READ_OID_FIELD(coalescecollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readMinMaxExpr
 */
static MinMaxExpr *
_readMinMaxExpr(void)
{
	READ_LOCALS(MinMaxExpr);

	READ_OID_FIELD(minmaxtype);
	READ_OID_FIELD(minmaxcollid);
	READ_OID_FIELD(inputcollid);
	READ_ENUM_FIELD(op, MinMaxOp);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readSQLValueFunction
 */
static SQLValueFunction *
_readSQLValueFunction(void)
{
	READ_LOCALS(SQLValueFunction);

	READ_ENUM_FIELD(op, SQLValueFunctionOp);
	READ_OID_FIELD(type);
	READ_INT_FIELD(typmod);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readXmlExpr
 */
static XmlExpr *
_readXmlExpr(void)
{
	READ_LOCALS(XmlExpr);

	READ_ENUM_FIELD(op, XmlExprOp);
	READ_STRING_FIELD(name);
	READ_NODE_FIELD(named_args);
	READ_NODE_FIELD(arg_names);
	READ_NODE_FIELD(args);
	READ_ENUM_FIELD(xmloption, XmlOptionType);
	READ_OID_FIELD(type);
	READ_INT_FIELD(typmod);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readNullTest
 */
static NullTest *
_readNullTest(void)
{
	READ_LOCALS(NullTest);

	READ_NODE_FIELD(arg);
	READ_ENUM_FIELD(nulltesttype, NullTestType);
	READ_BOOL_FIELD(argisrow);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readBooleanTest
 */
static BooleanTest *
_readBooleanTest(void)
{
	READ_LOCALS(BooleanTest);

	READ_NODE_FIELD(arg);
	READ_ENUM_FIELD(booltesttype, BoolTestType);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readCoerceToDomain
 */
static CoerceToDomain *
_readCoerceToDomain(void)
{
	READ_LOCALS(CoerceToDomain);

	READ_NODE_FIELD(arg);
	READ_OID_FIELD(resulttype);
	READ_INT_FIELD(resulttypmod);
	READ_OID_FIELD(resultcollid);
	READ_ENUM_FIELD(coercionformat, CoercionForm);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readCoerceToDomainValue
 */
static CoerceToDomainValue *
_readCoerceToDomainValue(void)
{
	READ_LOCALS(CoerceToDomainValue);

	READ_OID_FIELD(typeId);
	READ_INT_FIELD(typeMod);
	READ_OID_FIELD(collation);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readSetToDefault
 */
static SetToDefault *
_readSetToDefault(void)
{
	READ_LOCALS(SetToDefault);

	READ_OID_FIELD(typeId);
	READ_INT_FIELD(typeMod);
	READ_OID_FIELD(collation);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readCurrentOfExpr
 */
static CurrentOfExpr *
_readCurrentOfExpr(void)
{
	READ_LOCALS(CurrentOfExpr);

	READ_UINT_FIELD(cvarno);
	READ_STRING_FIELD(cursor_name);
	READ_INT_FIELD(cursor_param);

	READ_DONE();
}

/*
 * _readNextValueExpr
 */
static NextValueExpr *
_readNextValueExpr(void)
{
	READ_LOCALS(NextValueExpr);

	READ_OID_FIELD(seqid);
	READ_OID_FIELD(typeId);

	READ_DONE();
}

/*
 * _readInferenceElem
 */
static InferenceElem *
_readInferenceElem(void)
{
	READ_LOCALS(InferenceElem);

	READ_NODE_FIELD(expr);
	READ_OID_FIELD(infercollid);
	READ_OID_FIELD(inferopclass);

	READ_DONE();
}

/*
 * _readTargetEntry
 */
static TargetEntry *
_readTargetEntry(void)
{
	READ_LOCALS(TargetEntry);

	READ_NODE_FIELD(expr);
	READ_INT_FIELD(resno);
	READ_STRING_FIELD(resname);
	READ_UINT_FIELD(ressortgroupref);
	READ_OID_FIELD(resorigtbl);
	READ_INT_FIELD(resorigcol);
	READ_BOOL_FIELD(resjunk);

	READ_DONE();
}

/*
 * _readRangeTblRef
 */
static RangeTblRef *
_readRangeTblRef(void)
{
	READ_LOCALS(RangeTblRef);

	READ_INT_FIELD(rtindex);

	READ_DONE();
}

/*
 * _readJoinExpr
 */
static JoinExpr *
_readJoinExpr(void)
{
	READ_LOCALS(JoinExpr);

	READ_ENUM_FIELD(jointype, JoinType);
	READ_BOOL_FIELD(isNatural);
	READ_NODE_FIELD(larg);
	READ_NODE_FIELD(rarg);
	READ_NODE_FIELD(usingClause);
	READ_NODE_FIELD(join_using_alias);
	READ_NODE_FIELD(quals);
	READ_NODE_FIELD(alias);
	READ_INT_FIELD(rtindex);
	READ_NODE_FIELD(joinHints);

	READ_DONE();
}

/*
 * _readFromExpr
 */
static FromExpr *
_readFromExpr(void)
{
	READ_LOCALS(FromExpr);

	READ_NODE_FIELD(fromlist);
	READ_NODE_FIELD(quals);

	READ_DONE();
}

/*
 * _readOnConflictExpr
 */
static OnConflictExpr *
_readOnConflictExpr(void)
{
	READ_LOCALS(OnConflictExpr);

	READ_ENUM_FIELD(action, OnConflictAction);
	READ_NODE_FIELD(arbiterElems);
	READ_NODE_FIELD(arbiterWhere);
	READ_OID_FIELD(constraint);
	READ_NODE_FIELD(onConflictSet);
	READ_NODE_FIELD(onConflictWhere);
	READ_INT_FIELD(exclRelIndex);
	READ_NODE_FIELD(exclRelTlist);

	READ_DONE();
}

/*
 *	Stuff from pathnodes.h.
 *
 * Mostly we don't need to read planner nodes back in again, but some
 * of these also end up in plan trees.
 */

/*
 * _readAppendRelInfo
 */
static AppendRelInfo *
_readAppendRelInfo(void)
{
	READ_LOCALS(AppendRelInfo);

	READ_UINT_FIELD(parent_relid);
	READ_UINT_FIELD(child_relid);
	READ_OID_FIELD(parent_reltype);
	READ_OID_FIELD(child_reltype);
	READ_NODE_FIELD(translated_vars);
	READ_INT_FIELD(num_child_cols);
	READ_ATTRNUMBER_ARRAY(parent_colnos, local_node->num_child_cols);
	READ_OID_FIELD(parent_reloid);

	READ_DONE();
}

/*
 *	Stuff from parsenodes.h.
 */

/*
 * _readRangeTblEntry
 */
static RangeTblEntry *
_readRangeTblEntry(void)
{
	READ_LOCALS(RangeTblEntry);

	/* put alias + eref first to make dump more legible */
	READ_NODE_FIELD(alias);
	READ_NODE_FIELD(eref);
	READ_ENUM_FIELD(rtekind, RTEKind);

	switch (local_node->rtekind)
	{
		case RTE_RELATION:
			READ_OID_FIELD(relid);
			READ_CHAR_FIELD(relkind);
			READ_INT_FIELD(rellockmode);
			READ_NODE_FIELD(tablesample);
			break;
		case RTE_SUBQUERY:
			READ_NODE_FIELD(subquery);
			READ_BOOL_FIELD(security_barrier);
			break;
		case RTE_JOIN:
			READ_ENUM_FIELD(jointype, JoinType);
			READ_INT_FIELD(joinmergedcols);
			READ_NODE_FIELD(joinaliasvars);
			READ_NODE_FIELD(joinleftcols);
			READ_NODE_FIELD(joinrightcols);
			READ_NODE_FIELD(join_using_alias);
			break;
		case RTE_FUNCTION:
			READ_NODE_FIELD(functions);
			READ_BOOL_FIELD(funcordinality);
			break;
		case RTE_TABLEFUNC:
			READ_NODE_FIELD(tablefunc);
			/* The RTE must have a copy of the column type info, if any */
			if (local_node->tablefunc)
			{
				TableFunc  *tf = local_node->tablefunc;

				local_node->coltypes = tf->coltypes;
				local_node->coltypmods = tf->coltypmods;
				local_node->colcollations = tf->colcollations;
			}
			break;
		case RTE_VALUES:
			READ_NODE_FIELD(values_lists);
			READ_NODE_FIELD(coltypes);
			READ_NODE_FIELD(coltypmods);
			READ_NODE_FIELD(colcollations);
			break;
		case RTE_CTE:
			READ_STRING_FIELD(ctename);
			READ_UINT_FIELD(ctelevelsup);
			READ_BOOL_FIELD(self_reference);
			READ_NODE_FIELD(coltypes);
			READ_NODE_FIELD(coltypmods);
			READ_NODE_FIELD(colcollations);
			break;
		case RTE_NAMEDTUPLESTORE:
			READ_STRING_FIELD(enrname);
			READ_FLOAT_FIELD(enrtuples);
			READ_OID_FIELD(relid);
			READ_NODE_FIELD(coltypes);
			READ_NODE_FIELD(coltypmods);
			READ_NODE_FIELD(colcollations);
			break;
		case RTE_RESULT:
			/* no extra fields */
			break;
		default:
			elog(ERROR, "unrecognized RTE kind: %d",
				 (int) local_node->rtekind);
			break;
	}

	READ_BOOL_FIELD(lateral);
	READ_BOOL_FIELD(inh);
	READ_BOOL_FIELD(inFromCl);
	READ_UINT_FIELD(requiredPerms);
	READ_OID_FIELD(checkAsUser);
	READ_BITMAPSET_FIELD(selectedCols);
	READ_BITMAPSET_FIELD(insertedCols);
	READ_BITMAPSET_FIELD(updatedCols);
	READ_BITMAPSET_FIELD(extraUpdatedCols);
	READ_NODE_FIELD(securityQuals);
	READ_NODE_FIELD(tableHints);

	READ_DONE();
}

/*
 * _readRangeTblFunction
 */
static RangeTblFunction *
_readRangeTblFunction(void)
{
	READ_LOCALS(RangeTblFunction);

	READ_NODE_FIELD(funcexpr);
	READ_INT_FIELD(funccolcount);
	READ_NODE_FIELD(funccolnames);
	READ_NODE_FIELD(funccoltypes);
	READ_NODE_FIELD(funccoltypmods);
	READ_NODE_FIELD(funccolcollations);
	READ_BITMAPSET_FIELD(funcparams);

	READ_DONE();
}

/*
 * _readTableSampleClause
 */
static TableSampleClause *
_readTableSampleClause(void)
{
	READ_LOCALS(TableSampleClause);

	READ_OID_FIELD(tsmhandler);
	READ_NODE_FIELD(args);
	READ_NODE_FIELD(repeatable);

	READ_DONE();
}

/*
 * _readDefElem
 */
static DefElem *
_readDefElem(void)
{
	READ_LOCALS(DefElem);

	READ_STRING_FIELD(defnamespace);
	READ_STRING_FIELD(defname);
	READ_NODE_FIELD(arg);
	READ_ENUM_FIELD(defaction, DefElemAction);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/* SPANGRES BEGIN */
static CreateTableAsStmt *
_readCreateTableAsStmt(void)
{
	READ_LOCALS(CreateTableAsStmt);

	READ_NODE_FIELD(query);
	READ_NODE_FIELD(into);
	READ_ENUM_FIELD(objtype, ObjectType);
	READ_BOOL_FIELD(is_select_into);
	READ_BOOL_FIELD(if_not_exists);

	READ_DONE();
}

static CreateSchemaStmt *
_readCreateSchemaStmt(void) {
	READ_LOCALS(CreateSchemaStmt);

	READ_STRING_FIELD(schemaname);
	READ_NODE_FIELD(authrole);
	READ_NODE_FIELD(schemaElts);
	READ_BOOL_FIELD(if_not_exists);

	READ_DONE();
}

static ViewStmt*
_readViewStmt(void)
{
  READ_LOCALS(ViewStmt);

  READ_NODE_FIELD(view);
	READ_NODE_FIELD(aliases);
	READ_NODE_FIELD(query);
	READ_BOOL_FIELD(replace);
	READ_NODE_FIELD(query);
	READ_NODE_FIELD(options);
	READ_ENUM_FIELD(withCheckOption, ViewCheckOption);
	READ_BOOL_FIELD(is_definer);
	READ_STRING_FIELD(query_string);

	READ_DONE();
}

/*
 * _readAlterSpangresStatsStmt
 */
static AlterSpangresStatsStmt*
_readAlterSpangresStatsStmt(void)
{
  READ_LOCALS(AlterSpangresStatsStmt);

	READ_NODE_FIELD(package_name);
	READ_NODE_FIELD(setstmt);

	READ_DONE();
}

/*
 * _readAIndirection
 */
static A_Indirection*
_readAIndirection(void)
{
  READ_LOCALS(A_Indirection);

	READ_NODE_FIELD(arg);
	READ_NODE_FIELD(indirection);

	READ_DONE();
}

/*
 * _readAIndirection
 */
static A_Indices*
_readAIndices(void)
{
  READ_LOCALS(A_Indices);

	READ_BOOL_FIELD(is_slice);
	READ_NODE_FIELD(lidx);
	READ_NODE_FIELD(uidx);

	READ_DONE();
}

static Ttl*
_readTtlSpangres(void)
{
  READ_LOCALS(Ttl);

	READ_STRING_FIELD(name);
	READ_NODE_FIELD(interval);

	READ_DONE();
}

/*
 * _readRangeFunction
 */
static RangeFunction*
_readRangeFunction(void)
{
	READ_LOCALS(RangeFunction);

	READ_BOOL_FIELD(lateral);
	READ_BOOL_FIELD(ordinality);
	READ_BOOL_FIELD(is_rowsfrom);
	READ_NODE_FIELD(functions);
	READ_NODE_FIELD(alias);
	READ_NODE_FIELD(coldeflist);

	READ_DONE();
}

static CreateChangeStreamStmt*
_readCreateChangeStreamStmt(void)
{
  READ_LOCALS(CreateChangeStreamStmt);

  READ_NODE_FIELD(change_stream_name);
  READ_NODE_FIELD(opt_options);
	READ_NODE_FIELD(opt_for_tables);
	READ_BOOL_FIELD(for_all);
	READ_BOOL_FIELD(if_not_exists);

	READ_DONE();
}

static ChangeStreamTrackedTable*
_readChangeStreamTrackedTable(void)
{
  READ_LOCALS(ChangeStreamTrackedTable);


  READ_NODE_FIELD(table_name);
  READ_NODE_FIELD(columns);
  READ_BOOL_FIELD(for_all_columns);

	READ_DONE();
}

static AlterChangeStreamStmt*
_readAlterChangeStreamStmt(void)
{
  READ_LOCALS(AlterChangeStreamStmt);


  READ_NODE_FIELD(change_stream_name);
  READ_NODE_FIELD(opt_options);
  READ_NODE_FIELD(opt_for_tables);
  READ_NODE_FIELD(opt_drop_for_tables);
  READ_NODE_FIELD(opt_reset_options);
  READ_BOOL_FIELD(for_all);
  READ_BOOL_FIELD(drop_for_all);

	READ_DONE();
}

// SPANGRES BEGIN
static CreateSearchIndexStmt*
_readCreateSearchIndexStmt(void)
{
  READ_LOCALS(CreateSearchIndexStmt);
	READ_STRING_FIELD(search_index_name);
	READ_NODE_FIELD(table_name);
	READ_NODE_FIELD(token_columns);
	READ_NODE_FIELD(storing);
	READ_NODE_FIELD(partition);
	READ_NODE_FIELD(order);
	READ_NODE_FIELD(null_filters);
	READ_NODE_FIELD(interleave);
	READ_NODE_FIELD(options);

	READ_DONE();
}

static AlterSearchIndexStmt*
_readAlterSearchIndexStmt(void)
{
  READ_LOCALS(AlterSearchIndexStmt);

	READ_NODE_FIELD(search_index_name);
	READ_NODE_FIELD(alter_search_index_cmd);

	READ_DONE();
}

static AlterSearchIndexCmd*
_readAlterSearchIndexCmd(void)
{
  READ_LOCALS(AlterSearchIndexCmd);

	READ_ENUM_FIELD(cmd_type, AlterSearchIndexCmdType);
	READ_STRING_FIELD(column_name);

	READ_DONE();
}

// SPANGRES END

static LocalityGroupOption* _readLocalityGroupOption(void) {
  READ_LOCALS(LocalityGroupOption);
  READ_STRING_FIELD(value);
	READ_BOOL_FIELD(is_null);

	READ_DONE();
}

static CreateLocalityGroupStmt* _readCreateLocalityGroupStmt(void) {
  READ_LOCALS(CreateLocalityGroupStmt);
  READ_NODE_FIELD(locality_group_name);
  READ_NODE_FIELD(storage);
  READ_NODE_FIELD(ssd_to_hdd_spill_timespan);
  READ_BOOL_FIELD(if_not_exists);

  READ_DONE();
}

static AlterLocalityGroupStmt* _readAlterLocalityGroupStmt(void) {
  READ_LOCALS(AlterLocalityGroupStmt);
  READ_NODE_FIELD(locality_group_name);
  READ_NODE_FIELD(storage);
  READ_NODE_FIELD(ssd_to_hdd_spill_timespan);
  READ_BOOL_FIELD(if_exists);

  READ_DONE();
}

static AlterColumnLocalityGroupStmt* _readAlterColumnLocalityGroupStmt(void) {
  READ_LOCALS(AlterColumnLocalityGroupStmt);
  READ_NODE_FIELD(relation);
  READ_STRING_FIELD(column);
  READ_NODE_FIELD(locality_group_name);

  READ_DONE();
}

static CreateRoleStmt*
_readCreateRoleStmt(void)
{
	READ_LOCALS(CreateRoleStmt);

	READ_ENUM_FIELD(stmt_type, RoleStmtType);
	READ_STRING_FIELD(role);
	READ_NODE_FIELD(options);

	READ_DONE();
}

static DropRoleStmt*
_readDropRoleStmt(void)
{
	READ_LOCALS(DropRoleStmt);

	READ_NODE_FIELD(roles);
	READ_BOOL_FIELD(missing_ok);

	READ_DONE();
}

static GrantStmt*
_readGrantStmt(void)
{
	READ_LOCALS(GrantStmt);

	READ_BOOL_FIELD(is_grant);
	READ_ENUM_FIELD(targtype, GrantTargetType);
	READ_ENUM_FIELD(objtype, ObjectType);
	READ_NODE_FIELD(objects);
	READ_NODE_FIELD(privileges);
	READ_NODE_FIELD(grantees);
	READ_BOOL_FIELD(grant_option);
	READ_ENUM_FIELD(behavior, DropBehavior);

	READ_DONE();
}

static AccessPriv*
_readAccessPriv(void)
{
	READ_LOCALS(AccessPriv);

	READ_STRING_FIELD(priv_name);
	READ_NODE_FIELD(cols);

	READ_DONE();
}

static ObjectWithArgs*
_readObjectWithArgs(void)
{
	READ_LOCALS(ObjectWithArgs);

	READ_NODE_FIELD(objname);
	READ_NODE_FIELD(objargs);
	READ_BOOL_FIELD(args_unspecified);

	READ_DONE();
}

static GrantRoleStmt*
_readGrantRoleStmt(void)
{
	READ_LOCALS(GrantRoleStmt);

	READ_NODE_FIELD(granted_roles);
	READ_NODE_FIELD(grantee_roles);
	READ_BOOL_FIELD(is_grant);
	READ_BOOL_FIELD(admin_opt);
	READ_NODE_FIELD(grantor);
	READ_ENUM_FIELD(behavior, DropBehavior);

	READ_DONE();
}

/* SPANGRES END */

/*
 *	Stuff from plannodes.h.
 */

/*
 * _readPlannedStmt
 */
static PlannedStmt *
_readPlannedStmt(void)
{
	READ_LOCALS(PlannedStmt);

	READ_ENUM_FIELD(commandType, CmdType);
	READ_UINT64_FIELD(queryId);
	READ_BOOL_FIELD(hasReturning);
	READ_BOOL_FIELD(hasModifyingCTE);
	READ_BOOL_FIELD(canSetTag);
	READ_BOOL_FIELD(transientPlan);
	READ_BOOL_FIELD(dependsOnRole);
	READ_BOOL_FIELD(parallelModeNeeded);
	READ_INT_FIELD(jitFlags);
	READ_NODE_FIELD(planTree);
	READ_NODE_FIELD(rtable);
	READ_NODE_FIELD(resultRelations);
	READ_NODE_FIELD(appendRelations);
	READ_NODE_FIELD(subplans);
	READ_BITMAPSET_FIELD(rewindPlanIDs);
	READ_NODE_FIELD(rowMarks);
	READ_NODE_FIELD(relationOids);
	READ_NODE_FIELD(invalItems);
	READ_NODE_FIELD(paramExecTypes);
	READ_NODE_FIELD(utilityStmt);
	READ_LOCATION_FIELD(stmt_location);
	READ_INT_FIELD(stmt_len);

	READ_DONE();
}

/*
 * ReadCommonPlan
 *	Assign the basic stuff of all nodes that inherit from Plan
 */
static void
ReadCommonPlan(Plan *local_node)
{
	READ_TEMP_LOCALS();

	READ_FLOAT_FIELD(startup_cost);
	READ_FLOAT_FIELD(total_cost);
	READ_FLOAT_FIELD(plan_rows);
	READ_INT_FIELD(plan_width);
	READ_BOOL_FIELD(parallel_aware);
	READ_BOOL_FIELD(parallel_safe);
	READ_BOOL_FIELD(async_capable);
	READ_INT_FIELD(plan_node_id);
	READ_NODE_FIELD(targetlist);
	READ_NODE_FIELD(qual);
	READ_NODE_FIELD(lefttree);
	READ_NODE_FIELD(righttree);
	READ_NODE_FIELD(initPlan);
	READ_BITMAPSET_FIELD(extParam);
	READ_BITMAPSET_FIELD(allParam);
}

/*
 * _readPlan
 */
static Plan *
_readPlan(void)
{
	READ_LOCALS_NO_FIELDS(Plan);

	ReadCommonPlan(local_node);

	READ_DONE();
}

/*
 * _readResult
 */
static Result *
_readResult(void)
{
	READ_LOCALS(Result);

	ReadCommonPlan(&local_node->plan);

	READ_NODE_FIELD(resconstantqual);

	READ_DONE();
}

/*
 * _readProjectSet
 */
static ProjectSet *
_readProjectSet(void)
{
	READ_LOCALS_NO_FIELDS(ProjectSet);

	ReadCommonPlan(&local_node->plan);

	READ_DONE();
}

/*
 * _readModifyTable
 */
static ModifyTable *
_readModifyTable(void)
{
	READ_LOCALS(ModifyTable);

	ReadCommonPlan(&local_node->plan);

	READ_ENUM_FIELD(operation, CmdType);
	READ_BOOL_FIELD(canSetTag);
	READ_UINT_FIELD(nominalRelation);
	READ_UINT_FIELD(rootRelation);
	READ_BOOL_FIELD(partColsUpdated);
	READ_NODE_FIELD(resultRelations);
	READ_NODE_FIELD(updateColnosLists);
	READ_NODE_FIELD(withCheckOptionLists);
	READ_NODE_FIELD(returningLists);
	READ_NODE_FIELD(fdwPrivLists);
	READ_BITMAPSET_FIELD(fdwDirectModifyPlans);
	READ_NODE_FIELD(rowMarks);
	READ_INT_FIELD(epqParam);
	READ_ENUM_FIELD(onConflictAction, OnConflictAction);
	READ_NODE_FIELD(arbiterIndexes);
	READ_NODE_FIELD(onConflictSet);
	READ_NODE_FIELD(onConflictCols);
	READ_NODE_FIELD(onConflictWhere);
	READ_UINT_FIELD(exclRelRTI);
	READ_NODE_FIELD(exclRelTlist);
	READ_NODE_FIELD(mergeActionLists);

	READ_DONE();
}

/*
 * _readAppend
 */
static Append *
_readAppend(void)
{
	READ_LOCALS(Append);

	ReadCommonPlan(&local_node->plan);

	READ_BITMAPSET_FIELD(apprelids);
	READ_NODE_FIELD(appendplans);
	READ_INT_FIELD(nasyncplans);
	READ_INT_FIELD(first_partial_plan);
	READ_NODE_FIELD(part_prune_info);

	READ_DONE();
}

/*
 * _readMergeAppend
 */
static MergeAppend *
_readMergeAppend(void)
{
	READ_LOCALS(MergeAppend);

	ReadCommonPlan(&local_node->plan);

	READ_BITMAPSET_FIELD(apprelids);
	READ_NODE_FIELD(mergeplans);
	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(sortColIdx, local_node->numCols);
	READ_OID_ARRAY(sortOperators, local_node->numCols);
	READ_OID_ARRAY(collations, local_node->numCols);
	READ_BOOL_ARRAY(nullsFirst, local_node->numCols);
	READ_NODE_FIELD(part_prune_info);

	READ_DONE();
}

/*
 * _readRecursiveUnion
 */
static RecursiveUnion *
_readRecursiveUnion(void)
{
	READ_LOCALS(RecursiveUnion);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(wtParam);
	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(dupColIdx, local_node->numCols);
	READ_OID_ARRAY(dupOperators, local_node->numCols);
	READ_OID_ARRAY(dupCollations, local_node->numCols);
	READ_LONG_FIELD(numGroups);

	READ_DONE();
}

/*
 * _readBitmapAnd
 */
static BitmapAnd *
_readBitmapAnd(void)
{
	READ_LOCALS(BitmapAnd);

	ReadCommonPlan(&local_node->plan);

	READ_NODE_FIELD(bitmapplans);

	READ_DONE();
}

/*
 * _readBitmapOr
 */
static BitmapOr *
_readBitmapOr(void)
{
	READ_LOCALS(BitmapOr);

	ReadCommonPlan(&local_node->plan);

	READ_BOOL_FIELD(isshared);
	READ_NODE_FIELD(bitmapplans);

	READ_DONE();
}

/*
 * ReadCommonScan
 *	Assign the basic stuff of all nodes that inherit from Scan
 */
static void
ReadCommonScan(Scan *local_node)
{
	READ_TEMP_LOCALS();

	ReadCommonPlan(&local_node->plan);

	READ_UINT_FIELD(scanrelid);
}

/*
 * _readScan
 */
static Scan *
_readScan(void)
{
	READ_LOCALS_NO_FIELDS(Scan);

	ReadCommonScan(local_node);

	READ_DONE();
}

/*
 * _readSeqScan
 */
static SeqScan *
_readSeqScan(void)
{
	READ_LOCALS_NO_FIELDS(SeqScan);

	ReadCommonScan(&local_node->scan);

	READ_DONE();
}

/*
 * _readSampleScan
 */
static SampleScan *
_readSampleScan(void)
{
	READ_LOCALS(SampleScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(tablesample);

	READ_DONE();
}

/*
 * _readIndexScan
 */
static IndexScan *
_readIndexScan(void)
{
	READ_LOCALS(IndexScan);

	ReadCommonScan(&local_node->scan);

	READ_OID_FIELD(indexid);
	READ_NODE_FIELD(indexqual);
	READ_NODE_FIELD(indexqualorig);
	READ_NODE_FIELD(indexorderby);
	READ_NODE_FIELD(indexorderbyorig);
	READ_NODE_FIELD(indexorderbyops);
	READ_ENUM_FIELD(indexorderdir, ScanDirection);

	READ_DONE();
}

/*
 * _readIndexOnlyScan
 */
static IndexOnlyScan *
_readIndexOnlyScan(void)
{
	READ_LOCALS(IndexOnlyScan);

	ReadCommonScan(&local_node->scan);

	READ_OID_FIELD(indexid);
	READ_NODE_FIELD(indexqual);
	READ_NODE_FIELD(recheckqual);
	READ_NODE_FIELD(indexorderby);
	READ_NODE_FIELD(indextlist);
	READ_ENUM_FIELD(indexorderdir, ScanDirection);

	READ_DONE();
}

/*
 * _readBitmapIndexScan
 */
static BitmapIndexScan *
_readBitmapIndexScan(void)
{
	READ_LOCALS(BitmapIndexScan);

	ReadCommonScan(&local_node->scan);

	READ_OID_FIELD(indexid);
	READ_BOOL_FIELD(isshared);
	READ_NODE_FIELD(indexqual);
	READ_NODE_FIELD(indexqualorig);

	READ_DONE();
}

/*
 * _readBitmapHeapScan
 */
static BitmapHeapScan *
_readBitmapHeapScan(void)
{
	READ_LOCALS(BitmapHeapScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(bitmapqualorig);

	READ_DONE();
}

/*
 * _readTidScan
 */
static TidScan *
_readTidScan(void)
{
	READ_LOCALS(TidScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(tidquals);

	READ_DONE();
}

/*
 * _readTidRangeScan
 */
static TidRangeScan *
_readTidRangeScan(void)
{
	READ_LOCALS(TidRangeScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(tidrangequals);

	READ_DONE();
}

/*
 * _readSubqueryScan
 */
static SubqueryScan *
_readSubqueryScan(void)
{
	READ_LOCALS(SubqueryScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(subplan);
	READ_ENUM_FIELD(scanstatus, SubqueryScanStatus);

	READ_DONE();
}

/*
 * _readFunctionScan
 */
static FunctionScan *
_readFunctionScan(void)
{
	READ_LOCALS(FunctionScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(functions);
	READ_BOOL_FIELD(funcordinality);

	READ_DONE();
}

/*
 * _readValuesScan
 */
static ValuesScan *
_readValuesScan(void)
{
	READ_LOCALS(ValuesScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(values_lists);

	READ_DONE();
}

/*
 * _readTableFuncScan
 */
static TableFuncScan *
_readTableFuncScan(void)
{
	READ_LOCALS(TableFuncScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(tablefunc);

	READ_DONE();
}

/*
 * _readCteScan
 */
static CteScan *
_readCteScan(void)
{
	READ_LOCALS(CteScan);

	ReadCommonScan(&local_node->scan);

	READ_INT_FIELD(ctePlanId);
	READ_INT_FIELD(cteParam);

	READ_DONE();
}

/*
 * _readNamedTuplestoreScan
 */
static NamedTuplestoreScan *
_readNamedTuplestoreScan(void)
{
	READ_LOCALS(NamedTuplestoreScan);

	ReadCommonScan(&local_node->scan);

	READ_STRING_FIELD(enrname);

	READ_DONE();
}

/*
 * _readWorkTableScan
 */
static WorkTableScan *
_readWorkTableScan(void)
{
	READ_LOCALS(WorkTableScan);

	ReadCommonScan(&local_node->scan);

	READ_INT_FIELD(wtParam);

	READ_DONE();
}

/*
 * _readForeignScan
 */
static ForeignScan *
_readForeignScan(void)
{
	READ_LOCALS(ForeignScan);

	ReadCommonScan(&local_node->scan);

	READ_ENUM_FIELD(operation, CmdType);
	READ_UINT_FIELD(resultRelation);
	READ_OID_FIELD(fs_server);
	READ_NODE_FIELD(fdw_exprs);
	READ_NODE_FIELD(fdw_private);
	READ_NODE_FIELD(fdw_scan_tlist);
	READ_NODE_FIELD(fdw_recheck_quals);
	READ_BITMAPSET_FIELD(fs_relids);
	READ_BOOL_FIELD(fsSystemCol);

	READ_DONE();
}

/*
 * _readCustomScan
 */
static CustomScan *
_readCustomScan(void)
{
	READ_LOCALS(CustomScan);
	char	   *custom_name;
	const CustomScanMethods *methods;

	ReadCommonScan(&local_node->scan);

	READ_UINT_FIELD(flags);
	READ_NODE_FIELD(custom_plans);
	READ_NODE_FIELD(custom_exprs);
	READ_NODE_FIELD(custom_private);
	READ_NODE_FIELD(custom_scan_tlist);
	READ_BITMAPSET_FIELD(custom_relids);

	/* Lookup CustomScanMethods by CustomName */
	token = pg_strtok(&length); /* skip methods: */
	token = pg_strtok(&length); /* CustomName */
	custom_name = nullable_string(token, length);
	methods = GetCustomScanMethods(custom_name, false);
	local_node->methods = methods;

	READ_DONE();
}

/*
 * ReadCommonJoin
 *	Assign the basic stuff of all nodes that inherit from Join
 */
static void
ReadCommonJoin(Join *local_node)
{
	READ_TEMP_LOCALS();

	ReadCommonPlan(&local_node->plan);

	READ_ENUM_FIELD(jointype, JoinType);
	READ_BOOL_FIELD(inner_unique);
	READ_NODE_FIELD(joinqual);
}

/*
 * _readJoin
 */
static Join *
_readJoin(void)
{
	READ_LOCALS_NO_FIELDS(Join);

	ReadCommonJoin(local_node);

	READ_DONE();
}

/*
 * _readNestLoop
 */
static NestLoop *
_readNestLoop(void)
{
	READ_LOCALS(NestLoop);

	ReadCommonJoin(&local_node->join);

	READ_NODE_FIELD(nestParams);

	READ_DONE();
}

/*
 * _readMergeJoin
 */
static MergeJoin *
_readMergeJoin(void)
{
	int			numCols;

	READ_LOCALS(MergeJoin);

	ReadCommonJoin(&local_node->join);

	READ_BOOL_FIELD(skip_mark_restore);
	READ_NODE_FIELD(mergeclauses);

	numCols = list_length(local_node->mergeclauses);

	READ_OID_ARRAY(mergeFamilies, numCols);
	READ_OID_ARRAY(mergeCollations, numCols);
	READ_INT_ARRAY(mergeStrategies, numCols);
	READ_BOOL_ARRAY(mergeNullsFirst, numCols);

	READ_DONE();
}

/*
 * _readHashJoin
 */
static HashJoin *
_readHashJoin(void)
{
	READ_LOCALS(HashJoin);

	ReadCommonJoin(&local_node->join);

	READ_NODE_FIELD(hashclauses);
	READ_NODE_FIELD(hashoperators);
	READ_NODE_FIELD(hashcollations);
	READ_NODE_FIELD(hashkeys);

	READ_DONE();
}

/*
 * _readMaterial
 */
static Material *
_readMaterial(void)
{
	READ_LOCALS_NO_FIELDS(Material);

	ReadCommonPlan(&local_node->plan);

	READ_DONE();
}

/*
 * _readMemoize
 */
static Memoize *
_readMemoize(void)
{
	READ_LOCALS(Memoize);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(numKeys);
	READ_OID_ARRAY(hashOperators, local_node->numKeys);
	READ_OID_ARRAY(collations, local_node->numKeys);
	READ_NODE_FIELD(param_exprs);
	READ_BOOL_FIELD(singlerow);
	READ_BOOL_FIELD(binary_mode);
	READ_UINT_FIELD(est_entries);
	READ_BITMAPSET_FIELD(keyparamids);

	READ_DONE();
}

/*
 * ReadCommonSort
 *	Assign the basic stuff of all nodes that inherit from Sort
 */
static void
ReadCommonSort(Sort *local_node)
{
	READ_TEMP_LOCALS();

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(sortColIdx, local_node->numCols);
	READ_OID_ARRAY(sortOperators, local_node->numCols);
	READ_OID_ARRAY(collations, local_node->numCols);
	READ_BOOL_ARRAY(nullsFirst, local_node->numCols);
}

/*
 * _readSort
 */
static Sort *
_readSort(void)
{
	READ_LOCALS_NO_FIELDS(Sort);

	ReadCommonSort(local_node);

	READ_DONE();
}

/*
 * _readIncrementalSort
 */
static IncrementalSort *
_readIncrementalSort(void)
{
	READ_LOCALS(IncrementalSort);

	ReadCommonSort(&local_node->sort);

	READ_INT_FIELD(nPresortedCols);

	READ_DONE();
}

/*
 * _readGroup
 */
static Group *
_readGroup(void)
{
	READ_LOCALS(Group);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(grpColIdx, local_node->numCols);
	READ_OID_ARRAY(grpOperators, local_node->numCols);
	READ_OID_ARRAY(grpCollations, local_node->numCols);

	READ_DONE();
}

/*
 * _readAgg
 */
static Agg *
_readAgg(void)
{
	READ_LOCALS(Agg);

	ReadCommonPlan(&local_node->plan);

	READ_ENUM_FIELD(aggstrategy, AggStrategy);
	READ_ENUM_FIELD(aggsplit, AggSplit);
	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(grpColIdx, local_node->numCols);
	READ_OID_ARRAY(grpOperators, local_node->numCols);
	READ_OID_ARRAY(grpCollations, local_node->numCols);
	READ_LONG_FIELD(numGroups);
	READ_UINT64_FIELD(transitionSpace);
	READ_BITMAPSET_FIELD(aggParams);
	READ_NODE_FIELD(groupingSets);
	READ_NODE_FIELD(chain);

	READ_DONE();
}

/*
 * _readWindowAgg
 */
static WindowAgg *
_readWindowAgg(void)
{
	READ_LOCALS(WindowAgg);

	ReadCommonPlan(&local_node->plan);

	READ_UINT_FIELD(winref);
	READ_INT_FIELD(partNumCols);
	READ_ATTRNUMBER_ARRAY(partColIdx, local_node->partNumCols);
	READ_OID_ARRAY(partOperators, local_node->partNumCols);
	READ_OID_ARRAY(partCollations, local_node->partNumCols);
	READ_INT_FIELD(ordNumCols);
	READ_ATTRNUMBER_ARRAY(ordColIdx, local_node->ordNumCols);
	READ_OID_ARRAY(ordOperators, local_node->ordNumCols);
	READ_OID_ARRAY(ordCollations, local_node->ordNumCols);
	READ_INT_FIELD(frameOptions);
	READ_NODE_FIELD(startOffset);
	READ_NODE_FIELD(endOffset);
	READ_NODE_FIELD(runCondition);
	READ_NODE_FIELD(runConditionOrig);
	READ_OID_FIELD(startInRangeFunc);
	READ_OID_FIELD(endInRangeFunc);
	READ_OID_FIELD(inRangeColl);
	READ_BOOL_FIELD(inRangeAsc);
	READ_BOOL_FIELD(inRangeNullsFirst);
	READ_BOOL_FIELD(topWindow);

	READ_DONE();
}

/*
 * _readUnique
 */
static Unique *
_readUnique(void)
{
	READ_LOCALS(Unique);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(uniqColIdx, local_node->numCols);
	READ_OID_ARRAY(uniqOperators, local_node->numCols);
	READ_OID_ARRAY(uniqCollations, local_node->numCols);

	READ_DONE();
}

/*
 * _readGather
 */
static Gather *
_readGather(void)
{
	READ_LOCALS(Gather);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(num_workers);
	READ_INT_FIELD(rescan_param);
	READ_BOOL_FIELD(single_copy);
	READ_BOOL_FIELD(invisible);
	READ_BITMAPSET_FIELD(initParam);

	READ_DONE();
}

/*
 * _readGatherMerge
 */
static GatherMerge *
_readGatherMerge(void)
{
	READ_LOCALS(GatherMerge);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(num_workers);
	READ_INT_FIELD(rescan_param);
	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(sortColIdx, local_node->numCols);
	READ_OID_ARRAY(sortOperators, local_node->numCols);
	READ_OID_ARRAY(collations, local_node->numCols);
	READ_BOOL_ARRAY(nullsFirst, local_node->numCols);
	READ_BITMAPSET_FIELD(initParam);

	READ_DONE();
}

/*
 * _readHash
 */
static Hash *
_readHash(void)
{
	READ_LOCALS(Hash);

	ReadCommonPlan(&local_node->plan);

	READ_NODE_FIELD(hashkeys);
	READ_OID_FIELD(skewTable);
	READ_INT_FIELD(skewColumn);
	READ_BOOL_FIELD(skewInherit);
	READ_FLOAT_FIELD(rows_total);

	READ_DONE();
}

/*
 * _readSetOp
 */
static SetOp *
_readSetOp(void)
{
	READ_LOCALS(SetOp);

	ReadCommonPlan(&local_node->plan);

	READ_ENUM_FIELD(cmd, SetOpCmd);
	READ_ENUM_FIELD(strategy, SetOpStrategy);
	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(dupColIdx, local_node->numCols);
	READ_OID_ARRAY(dupOperators, local_node->numCols);
	READ_OID_ARRAY(dupCollations, local_node->numCols);
	READ_INT_FIELD(flagColIdx);
	READ_INT_FIELD(firstFlag);
	READ_LONG_FIELD(numGroups);

	READ_DONE();
}

/*
 * _readLockRows
 */
static LockRows *
_readLockRows(void)
{
	READ_LOCALS(LockRows);

	ReadCommonPlan(&local_node->plan);

	READ_NODE_FIELD(rowMarks);
	READ_INT_FIELD(epqParam);

	READ_DONE();
}

/*
 * _readLimit
 */
static Limit *
_readLimit(void)
{
	READ_LOCALS(Limit);

	ReadCommonPlan(&local_node->plan);

	READ_NODE_FIELD(limitOffset);
	READ_NODE_FIELD(limitCount);
	READ_ENUM_FIELD(limitOption, LimitOption);
	READ_INT_FIELD(uniqNumCols);
	READ_ATTRNUMBER_ARRAY(uniqColIdx, local_node->uniqNumCols);
	READ_OID_ARRAY(uniqOperators, local_node->uniqNumCols);
	READ_OID_ARRAY(uniqCollations, local_node->uniqNumCols);

	READ_DONE();
}

/*
 * _readNestLoopParam
 */
static NestLoopParam *
_readNestLoopParam(void)
{
	READ_LOCALS(NestLoopParam);

	READ_INT_FIELD(paramno);
	READ_NODE_FIELD(paramval);

	READ_DONE();
}

/*
 * _readPlanRowMark
 */
static PlanRowMark *
_readPlanRowMark(void)
{
	READ_LOCALS(PlanRowMark);

	READ_UINT_FIELD(rti);
	READ_UINT_FIELD(prti);
	READ_UINT_FIELD(rowmarkId);
	READ_ENUM_FIELD(markType, RowMarkType);
	READ_INT_FIELD(allMarkTypes);
	READ_ENUM_FIELD(strength, LockClauseStrength);
	READ_ENUM_FIELD(waitPolicy, LockWaitPolicy);
	READ_BOOL_FIELD(isParent);

	READ_DONE();
}

static PartitionPruneInfo *
_readPartitionPruneInfo(void)
{
	READ_LOCALS(PartitionPruneInfo);

	READ_NODE_FIELD(prune_infos);
	READ_BITMAPSET_FIELD(other_subplans);

	READ_DONE();
}

static PartitionedRelPruneInfo *
_readPartitionedRelPruneInfo(void)
{
	READ_LOCALS(PartitionedRelPruneInfo);

	READ_UINT_FIELD(rtindex);
	READ_BITMAPSET_FIELD(present_parts);
	READ_INT_FIELD(nparts);
	READ_INT_ARRAY(subplan_map, local_node->nparts);
	READ_INT_ARRAY(subpart_map, local_node->nparts);
	READ_OID_ARRAY(relid_map, local_node->nparts);
	READ_NODE_FIELD(initial_pruning_steps);
	READ_NODE_FIELD(exec_pruning_steps);
	READ_BITMAPSET_FIELD(execparamids);

	READ_DONE();
}

static PartitionPruneStepOp *
_readPartitionPruneStepOp(void)
{
	READ_LOCALS(PartitionPruneStepOp);

	READ_INT_FIELD(step.step_id);
	READ_INT_FIELD(opstrategy);
	READ_NODE_FIELD(exprs);
	READ_NODE_FIELD(cmpfns);
	READ_BITMAPSET_FIELD(nullkeys);

	READ_DONE();
}

static PartitionPruneStepCombine *
_readPartitionPruneStepCombine(void)
{
	READ_LOCALS(PartitionPruneStepCombine);

	READ_INT_FIELD(step.step_id);
	READ_ENUM_FIELD(combineOp, PartitionPruneCombineOp);
	READ_NODE_FIELD(source_stepids);

	READ_DONE();
}

/*
 * _readPlanInvalItem
 */
static PlanInvalItem *
_readPlanInvalItem(void)
{
	READ_LOCALS(PlanInvalItem);

	READ_INT_FIELD(cacheId);
	READ_UINT_FIELD(hashValue);

	READ_DONE();
}

/*
 * _readSubPlan
 */
static SubPlan *
_readSubPlan(void)
{
	READ_LOCALS(SubPlan);

	READ_ENUM_FIELD(subLinkType, SubLinkType);
	READ_NODE_FIELD(testexpr);
	READ_NODE_FIELD(paramIds);
	READ_INT_FIELD(plan_id);
	READ_STRING_FIELD(plan_name);
	READ_OID_FIELD(firstColType);
	READ_INT_FIELD(firstColTypmod);
	READ_OID_FIELD(firstColCollation);
	READ_BOOL_FIELD(useHashTable);
	READ_BOOL_FIELD(unknownEqFalse);
	READ_BOOL_FIELD(parallel_safe);
	READ_NODE_FIELD(setParam);
	READ_NODE_FIELD(parParam);
	READ_NODE_FIELD(args);
	READ_FLOAT_FIELD(startup_cost);
	READ_FLOAT_FIELD(per_call_cost);

	READ_DONE();
}

/*
 * _readAlternativeSubPlan
 */
static AlternativeSubPlan *
_readAlternativeSubPlan(void)
{
	READ_LOCALS(AlternativeSubPlan);

	READ_NODE_FIELD(subplans);

	READ_DONE();
}

/*
 * _readExtensibleNode
 */
static ExtensibleNode *
_readExtensibleNode(void)
{
	const ExtensibleNodeMethods *methods;
	ExtensibleNode *local_node;
	const char *extnodename;

	READ_TEMP_LOCALS();

	token = pg_strtok(&length); /* skip :extnodename */
	token = pg_strtok(&length); /* get extnodename */

	extnodename = nullable_string(token, length);
	if (!extnodename)
		elog(ERROR, "extnodename has to be supplied");
	methods = GetExtensibleNodeMethods(extnodename, false);

	local_node = (ExtensibleNode *) newNode(methods->node_size,
											T_ExtensibleNode);
	local_node->extnodename = extnodename;

	/* deserialize the private fields */
	methods->nodeRead(local_node);

	READ_DONE();
}

/*
 * _readPartitionBoundSpec
 */
static PartitionBoundSpec *
_readPartitionBoundSpec(void)
{
	READ_LOCALS(PartitionBoundSpec);

	READ_CHAR_FIELD(strategy);
	READ_BOOL_FIELD(is_default);
	READ_INT_FIELD(modulus);
	READ_INT_FIELD(remainder);
	READ_NODE_FIELD(listdatums);
	READ_NODE_FIELD(lowerdatums);
	READ_NODE_FIELD(upperdatums);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readPartitionRangeDatum
 */
static PartitionRangeDatum *
_readPartitionRangeDatum(void)
{
	READ_LOCALS(PartitionRangeDatum);

	READ_ENUM_FIELD(kind, PartitionRangeDatumKind);
	READ_NODE_FIELD(value);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readCreateDbStmt
 */
static CreatedbStmt*
_readCreateDbStmt(void)
{
	READ_LOCALS(CreatedbStmt);

	READ_STRING_FIELD(dbname);
	READ_NODE_FIELD(options);

	READ_DONE();
}

/*
 * _readDropStmt
 */
static DropStmt*
_readDropStmt(void)
{
	READ_LOCALS(DropStmt);

	READ_NODE_FIELD(objects);
	READ_ENUM_FIELD(removeType, ObjectType);
	READ_ENUM_FIELD(behavior, DropBehavior);
	READ_BOOL_FIELD(missing_ok);
	READ_BOOL_FIELD(concurrent);

	READ_DONE();
}

/*
 * _readInsertStmt
 */
static InsertStmt*
_readInsertStmt(void)
{
	READ_LOCALS(InsertStmt);

	READ_NODE_FIELD(relation);
	READ_NODE_FIELD(cols);
	READ_NODE_FIELD(selectStmt);
	READ_NODE_FIELD(onConflictClause);
	READ_NODE_FIELD(returningList);
	READ_NODE_FIELD(withClause);
	READ_ENUM_FIELD(override, OverridingKind);

	READ_DONE();
}

/*
 * _readOnConflictClause
 */
static OnConflictClause*
_readOnConflictClause(void)
{
	READ_LOCALS(OnConflictClause);

	READ_ENUM_FIELD(action, OnConflictAction);
	READ_NODE_FIELD(infer);
	READ_NODE_FIELD(targetList);
	READ_NODE_FIELD(whereClause);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readInferClause
 */
static InferClause*
_readInferClause(void)
{
	READ_LOCALS(InferClause);

	READ_NODE_FIELD(indexElems);
	READ_NODE_FIELD(whereClause);
	READ_STRING_FIELD(conname);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

static UpdateStmt*
_readUpdateStmt(void)
{
	READ_LOCALS(UpdateStmt);

	READ_NODE_FIELD(relation);
	READ_NODE_FIELD(targetList);
	READ_NODE_FIELD(whereClause);
	READ_NODE_FIELD(fromClause);
	READ_NODE_FIELD(returningList);
	READ_NODE_FIELD(withClause);

	READ_DONE();
}

static DeleteStmt*
_readDeleteStmt(void)
{
	READ_LOCALS(DeleteStmt);

	READ_NODE_FIELD(relation);
	READ_NODE_FIELD(usingClause);
	READ_NODE_FIELD(whereClause);
	READ_NODE_FIELD(returningList);
	READ_NODE_FIELD(withClause);

	READ_DONE();
}

/*
 * _readWithClause
 */
static WithClause*
_readWithClause(void)
{
	READ_LOCALS(WithClause);

	READ_NODE_FIELD(ctes);
	READ_BOOL_FIELD(recursive);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readIndexElem
 */
static IndexElem*
_readIndexElem(void)
{
	READ_LOCALS(IndexElem);

	READ_STRING_FIELD(name);
	READ_NODE_FIELD(expr);
	READ_STRING_FIELD(expr_string);
	READ_STRING_FIELD(indexcolname);
	READ_NODE_FIELD(collation);
	READ_NODE_FIELD(opclass);
	READ_NODE_FIELD(opclassopts);
	READ_ENUM_FIELD(ordering, SortByDir);
	READ_ENUM_FIELD(nulls_ordering, SortByNulls);

	READ_DONE();
}

/*
 * _readPartitionSpec
 */
static PartitionSpec*
_readPartitionSpec(void)
{
	READ_LOCALS(PartitionSpec);

	READ_STRING_FIELD(strategy);
	READ_NODE_FIELD(partParams);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readPartitionElem
 */
static PartitionElem*
_readPartitionElem(void)
{
	READ_LOCALS(PartitionElem);

	READ_STRING_FIELD(name);
	READ_NODE_FIELD(expr);
	READ_NODE_FIELD(collation);
	READ_NODE_FIELD(opclass);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readTableLikeClause
 */
static TableLikeClause*
_readTableLikeClause(void)
{
	READ_LOCALS(TableLikeClause);

	READ_NODE_FIELD(relation);
	READ_UINT_FIELD(options);
	READ_OID_FIELD(relationOid);

	READ_DONE();
}

/*
 * _readCollateClause
 */
static CollateClause*
_readCollateClause(void)
{
	READ_LOCALS(CollateClause);

	READ_NODE_FIELD(arg);
	READ_NODE_FIELD(collname);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readConstraint
 */
static Constraint *
_readConstraint(void)
{
	READ_LOCALS(Constraint);

	READ_STRING_FIELD(access_method);
	READ_STRING_FIELD(conname);
	READ_ENUM_FIELD(contype, ConstrType);
	READ_STRING_FIELD(cooked_expr);
	READ_BOOL_FIELD(deferrable);
	READ_NODE_FIELD(exclusions);
	READ_NODE_FIELD(fk_attrs);
	READ_CHAR_FIELD(fk_del_action);
	READ_CHAR_FIELD(fk_matchtype);
	READ_CHAR_FIELD(fk_upd_action);
	READ_CHAR_FIELD(generated_when);
	READ_STRING_FIELD(indexname);
	READ_STRING_FIELD(indexspace);
	READ_BOOL_FIELD(reset_default_tblspc);
	READ_BOOL_FIELD(initdeferred);
	READ_BOOL_FIELD(initially_valid);
	READ_BOOL_FIELD(is_no_inherit);
	READ_NODE_FIELD(keys);
	READ_NODE_FIELD(including);
	READ_LOCATION_FIELD(location);
	READ_NODE_FIELD(old_conpfeqop);
	READ_OID_FIELD(old_pktable_oid);
	READ_NODE_FIELD(options);
	READ_NODE_FIELD(pk_attrs);
	READ_NODE_FIELD(pktable);
	READ_NODE_FIELD(raw_expr);
	READ_BOOL_FIELD(skip_validation);
	READ_ENUM_FIELD(stored_kind, GeneratedColStoreOpt);
	READ_INT_FIELD(vector_length);
	READ_NODE_FIELD(where_clause);
  READ_STRING_FIELD(constraint_expr_string);

	READ_DONE();
}

static AlterDatabaseSetStmt*
_readAlterDatabaseSetStmt(void)
{
	READ_LOCALS(AlterDatabaseSetStmt);

	READ_STRING_FIELD(dbname);
	READ_NODE_FIELD(setstmt);

	READ_DONE();
}

static VariableSetStmt*
_readVariableSetStmt(void)
{
	READ_LOCALS(VariableSetStmt);

	READ_ENUM_FIELD(kind, VariableSetKind);
	READ_STRING_FIELD(name);
	READ_NODE_FIELD(args);
	READ_BOOL_FIELD(is_local);

	READ_DONE();
}

static IndexStmt*
_readIndexStmt(void)
{
	READ_LOCALS(IndexStmt);

	READ_STRING_FIELD(idxname);
	READ_NODE_FIELD(relation);
	READ_STRING_FIELD(accessMethod);
	READ_STRING_FIELD(tableSpace);
	READ_NODE_FIELD(indexParams);
	READ_NODE_FIELD(indexIncludingParams);
	READ_NODE_FIELD(options);
	READ_NODE_FIELD(locality_group_name);
  READ_NODE_FIELD(interleavespec);
	READ_NODE_FIELD(whereClause);
	READ_NODE_FIELD(excludeOpNames);
	READ_STRING_FIELD(idxcomment);
	READ_OID_FIELD(indexOid);
	READ_OID_FIELD(oldNode);
	READ_UINT_FIELD(oldCreateSubid);
	READ_UINT_FIELD(oldFirstRelfilenodeSubid);
	READ_BOOL_FIELD(unique);
	READ_BOOL_FIELD(nulls_not_distinct);
	READ_BOOL_FIELD(primary);
	READ_BOOL_FIELD(isconstraint);
	READ_BOOL_FIELD(deferrable);
	READ_BOOL_FIELD(initdeferred);
	READ_BOOL_FIELD(transformed);
	READ_BOOL_FIELD(concurrent);
	READ_BOOL_FIELD(if_not_exists);
	READ_BOOL_FIELD(reset_default_tblspc);

	READ_DONE();
}

static AlterTableStmt*
_readAlterTableStmt(void)
{
	READ_LOCALS(AlterTableStmt);

	READ_NODE_FIELD(relation);
	READ_NODE_FIELD(cmds);
	READ_ENUM_FIELD(objtype, ObjectType);
	READ_BOOL_FIELD(missing_ok);

	READ_DONE();
}

static AlterTableCmd*
_readAlterTableCmd(void)
{
	READ_LOCALS(AlterTableCmd);

	READ_ENUM_FIELD(subtype, AlterTableType);
	READ_STRING_FIELD(name);
	READ_INT_FIELD(num);
	READ_NODE_FIELD(newowner);
	READ_NODE_FIELD(def);
	READ_ENUM_FIELD(behavior, DropBehavior);
	READ_BOOL_FIELD(missing_ok);
	READ_NODE_FIELD(locality_group_name);
	READ_STRING_FIELD(raw_expr_string);

	READ_DONE();
}

static RoleSpec*
_readRoleSpec(void)
{
	READ_LOCALS(RoleSpec);

	READ_ENUM_FIELD(roletype, RoleSpecType);
	READ_STRING_FIELD(rolename);
	READ_INT_FIELD(location);

	READ_DONE();
}

static WindowDef*
_readWindowDef(void)
{
	READ_LOCALS(WindowDef);

	READ_STRING_FIELD(name);
	READ_STRING_FIELD(refname);
	READ_NODE_FIELD(partitionClause);
	READ_NODE_FIELD(orderClause);
	READ_INT_FIELD(frameOptions);
	READ_NODE_FIELD(startOffset);
	READ_NODE_FIELD(endOffset);
	READ_INT_FIELD(location);

	READ_DONE();
}

static SortBy*
_readSortBy(void)
{
	READ_LOCALS(SortBy);

	READ_NODE_FIELD(node);
	READ_ENUM_FIELD(sortby_dir, SortByDir);
	READ_ENUM_FIELD(sortby_nulls, SortByNulls);
	READ_NODE_FIELD(useOp);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

static PrepareStmt*
_readPrepareStmt(void)
{
	READ_LOCALS(PrepareStmt);

	READ_STRING_FIELD(name);
	READ_NODE_FIELD(argtypes);
	READ_NODE_FIELD(query);

	READ_DONE();
}

static ExecuteStmt*
_readExecuteStmt(void)
{
	READ_LOCALS(ExecuteStmt);

	READ_STRING_FIELD(name);
	READ_NODE_FIELD(params);

	READ_DONE();
}

static InterleaveSpec*
_readInterleaveSpec(void)
{
	READ_LOCALS(InterleaveSpec);

	READ_ENUM_FIELD(interleavetype, InterleaveInType);
	READ_NODE_FIELD(parent);
	READ_CHAR_FIELD(on_delete_action);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

static ExplainStmt*
_readExplainStmt(void)
{
	READ_LOCALS(ExplainStmt);

	READ_NODE_FIELD(query);
	READ_NODE_FIELD(options);

	READ_DONE();
}

static VacuumStmt*
_readVacuumStmt(void)
{
	READ_LOCALS(VacuumStmt);

	READ_NODE_FIELD(options);
	READ_NODE_FIELD(rels);
	READ_BOOL_FIELD(is_vacuumcmd);

	READ_DONE();
}

static VacuumRelation*
_readVacuumRelation(void)
{
	READ_LOCALS(VacuumRelation);

	READ_NODE_FIELD(relation);
	READ_OID_FIELD(oid);
	READ_NODE_FIELD(va_cols);

	READ_DONE();
}

static TruncateStmt*
_readTruncateStmt(void)
{
	READ_LOCALS(TruncateStmt);

	READ_NODE_FIELD(relations);
	READ_BOOL_FIELD(restart_seqs);
	READ_ENUM_FIELD(behavior, DropBehavior);

	READ_DONE();
}

static TransactionStmt*
_readTransactionStmt(void)
{
	READ_LOCALS(TransactionStmt);

	READ_ENUM_FIELD(kind, TransactionStmtKind);
	READ_NODE_FIELD(options);
	READ_STRING_FIELD(savepoint_name);
	READ_STRING_FIELD(gid);
	READ_BOOL_FIELD(chain);

	READ_DONE();
}

static CreateSeqStmt*
_readCreateSeqStmt(void)
{
	READ_LOCALS(CreateSeqStmt);

	READ_NODE_FIELD(sequence);
	READ_NODE_FIELD(options);
	READ_OID_FIELD(ownerId);
	READ_BOOL_FIELD(for_identity);
	READ_BOOL_FIELD(if_not_exists);

	READ_DONE();
}

static AlterSeqStmt*
_readAlterSeqStmt(void)
{
	READ_LOCALS(AlterSeqStmt);

	READ_NODE_FIELD(sequence);
	READ_NODE_FIELD(options);
	READ_BOOL_FIELD(for_identity);
	READ_BOOL_FIELD(missing_ok);

	READ_DONE();
}

static RenameStmt*
_readRenameStmt(void)
{
	READ_LOCALS(RenameStmt);

	READ_ENUM_FIELD(renameType, ObjectType);
	READ_ENUM_FIELD(relationType, ObjectType);
	READ_NODE_FIELD(relation);
	READ_NODE_FIELD(object);
	READ_STRING_FIELD(subname);
	READ_STRING_FIELD(newname);
	READ_ENUM_FIELD(behavior, DropBehavior);
	READ_BOOL_FIELD(missing_ok);
	READ_BOOL_FIELD(addSynonym);

	READ_DONE();
}

static TableRenameOp*
_readTableRenameOp(void)
{
	READ_LOCALS(TableRenameOp);

	READ_NODE_FIELD(fromName);
	READ_STRING_FIELD(toName);

	READ_DONE();
}

static TableChainedRenameStmt*
_readTableChainedRenameStmt(void)
{
	READ_LOCALS(TableChainedRenameStmt);

	READ_NODE_FIELD(ops);

	READ_DONE();
}

static SynonymClause*
_readSynonymClause(void)
{
	READ_LOCALS(SynonymClause);

	READ_STRING_FIELD(name);

	READ_DONE();
}

static CallStmt*
_readCallStmt(void)
{
	READ_LOCALS(CallStmt);

	READ_NODE_FIELD(funccall);
	READ_NODE_FIELD(funcexpr);
	READ_NODE_FIELD(outargs);

	READ_DONE();
}

static AlterOwnerStmt*
_readAlterOwnerStmt(void)
{
	READ_LOCALS(AlterOwnerStmt);

	READ_ENUM_FIELD(objectType, ObjectType);
	READ_NODE_FIELD(relation);
	READ_NODE_FIELD(object);
	READ_NODE_FIELD(newowner);

	READ_DONE();
}

static AlterStatsStmt*
_readAlterStatsStmt(void)
{
	READ_LOCALS(AlterStatsStmt);

	READ_NODE_FIELD(defnames);
	READ_INT_FIELD(stxstattarget);
	READ_BOOL_FIELD(missing_ok);

	READ_DONE();
}

static AlterObjectSchemaStmt*
_readAlterObjectSchemaStmt(void)
{
	READ_LOCALS(AlterObjectSchemaStmt);

	READ_ENUM_FIELD(objectType, ObjectType);
	READ_NODE_FIELD(relation);
	READ_NODE_FIELD(object);
	READ_STRING_FIELD(newschema);
	READ_BOOL_FIELD(missing_ok);

	READ_DONE();
}
/* SPANGRES BEGIN */
/*
 * _readLockingClause
 */
static LockingClause *
_readLockingClause(void)
{
       READ_LOCALS(LockingClause);

       READ_NODE_FIELD(lockedRels);
       READ_ENUM_FIELD(strength, LockClauseStrength);
       READ_ENUM_FIELD(waitPolicy, LockWaitPolicy);

       READ_DONE();
}
/* SPANGRES END */
/*
 * parseNodeString
 *
 * Given a character string representing a node tree, parseNodeString creates
 * the internal node structure.
 *
 * The string to be read must already have been loaded into pg_strtok().
 */
Node *
parseNodeString(void)
{
	void	   *return_value;

	READ_TEMP_LOCALS();

	/* Guard against stack overflow due to overly complex expressions */
	check_stack_depth();

	token = pg_strtok(&length);

#define MATCH(tokname, namelen) \
	(length == namelen && memcmp(token, tokname, namelen) == 0)

	if (MATCH("QUERY", 5))
		return_value = _readQuery();
	else if (MATCH("WITHCHECKOPTION", 15))
		return_value = _readWithCheckOption();
	else if (MATCH("SORTGROUPCLAUSE", 15))
		return_value = _readSortGroupClause();
	else if (MATCH("GROUPINGSET", 11))
		return_value = _readGroupingSet();
	else if (MATCH("WINDOWCLAUSE", 12))
		return_value = _readWindowClause();
	else if (MATCH("ROWMARKCLAUSE", 13))
		return_value = _readRowMarkClause();
	else if (MATCH("CTESEARCHCLAUSE", 15))
		return_value = _readCTESearchClause();
	else if (MATCH("CTECYCLECLAUSE", 14))
		return_value = _readCTECycleClause();
	else if (MATCH("COMMONTABLEEXPR", 15))
		return_value = _readCommonTableExpr();
	else if (MATCH("MERGEWHENCLAUSE", 15))
		return_value = _readMergeWhenClause();
	else if (MATCH("MERGEACTION", 11))
		return_value = _readMergeAction();
	else if (MATCH("SETOPERATIONSTMT", 16))
		return_value = _readSetOperationStmt();
	else if (MATCH("ALIAS", 5))
		return_value = _readAlias();
	else if (MATCH("RANGEVAR", 8))
		return_value = _readRangeVar();
	else if (MATCH("INTOCLAUSE", 10))
		return_value = _readIntoClause();
	else if (MATCH("TABLEFUNC", 9))
		return_value = _readTableFunc();
	else if (MATCH("VAR", 3))
		return_value = _readVar();
	else if (MATCH("CONST", 5))
		return_value = _readConst();
	else if (MATCH("PARAM", 5))
		return_value = _readParam();
	else if (MATCH("AGGREF", 6))
		return_value = _readAggref();
	else if (MATCH("GROUPINGFUNC", 12))
		return_value = _readGroupingFunc();
	else if (MATCH("WINDOWFUNC", 10))
		return_value = _readWindowFunc();
	else if (MATCH("SUBSCRIPTINGREF", 15))
		return_value = _readSubscriptingRef();
	else if (MATCH("FUNCEXPR", 8))
		return_value = _readFuncExpr();
	else if (MATCH("NAMEDARGEXPR", 12))
		return_value = _readNamedArgExpr();
	else if (MATCH("OPEXPR", 6))
		return_value = _readOpExpr();
	else if (MATCH("DISTINCTEXPR", 12))
		return_value = _readDistinctExpr();
	else if (MATCH("NULLIFEXPR", 10))
		return_value = _readNullIfExpr();
	else if (MATCH("SCALARARRAYOPEXPR", 17))
		return_value = _readScalarArrayOpExpr();
	else if (MATCH("BOOLEXPR", 8))
		return_value = _readBoolExpr();
	else if (MATCH("SUBLINK", 7))
		return_value = _readSubLink();
	else if (MATCH("FIELDSELECT", 11))
		return_value = _readFieldSelect();
	else if (MATCH("FIELDSTORE", 10))
		return_value = _readFieldStore();
	else if (MATCH("RELABELTYPE", 11))
		return_value = _readRelabelType();
	else if (MATCH("COERCEVIAIO", 11))
		return_value = _readCoerceViaIO();
	else if (MATCH("ARRAYCOERCEEXPR", 15))
		return_value = _readArrayCoerceExpr();
	else if (MATCH("CONVERTROWTYPEEXPR", 18))
		return_value = _readConvertRowtypeExpr();
	else if (MATCH("COLLATEEXPR", 11))
		return_value = _readCollateExpr();
	else if (MATCH("CASEEXPR", 8))
		return_value = _readCaseExpr();
	else if (MATCH("CASEWHEN", 8))
		return_value = _readCaseWhen();
	else if (MATCH("CASETESTEXPR", 12))
		return_value = _readCaseTestExpr();
	else if (MATCH("ARRAYEXPR", 9))
		return_value = _readArrayExpr();
	else if (MATCH("ROWEXPR", 7))
		return_value = _readRowExpr();
	else if (MATCH("ROWCOMPAREEXPR", 14))
		return_value = _readRowCompareExpr();
	else if (MATCH("COALESCEEXPR", 12))
		return_value = _readCoalesceExpr();
	else if (MATCH("MINMAXEXPR", 10))
		return_value = _readMinMaxExpr();
	else if (MATCH("SQLVALUEFUNCTION", 16))
		return_value = _readSQLValueFunction();
	else if (MATCH("XMLEXPR", 7))
		return_value = _readXmlExpr();
	else if (MATCH("NULLTEST", 8))
		return_value = _readNullTest();
	else if (MATCH("BOOLEANTEST", 11))
		return_value = _readBooleanTest();
	else if (MATCH("COERCETODOMAIN", 14))
		return_value = _readCoerceToDomain();
	else if (MATCH("COERCETODOMAINVALUE", 19))
		return_value = _readCoerceToDomainValue();
	else if (MATCH("SETTODEFAULT", 12))
		return_value = _readSetToDefault();
	else if (MATCH("CURRENTOFEXPR", 13))
		return_value = _readCurrentOfExpr();
	else if (MATCH("NEXTVALUEEXPR", 13))
		return_value = _readNextValueExpr();
	else if (MATCH("INFERENCEELEM", 13))
		return_value = _readInferenceElem();
	else if (MATCH("TARGETENTRY", 11))
		return_value = _readTargetEntry();
	else if (MATCH("RANGETBLREF", 11))
		return_value = _readRangeTblRef();
	else if (MATCH("JOINEXPR", 8))
		return_value = _readJoinExpr();
	else if (MATCH("FROMEXPR", 8))
		return_value = _readFromExpr();
	else if (MATCH("ONCONFLICTEXPR", 14))
		return_value = _readOnConflictExpr();
	else if (MATCH("APPENDRELINFO", 13))
		return_value = _readAppendRelInfo();
	else if (MATCH("RANGETBLENTRY", 13))
		return_value = _readRangeTblEntry();
	else if (MATCH("RANGETBLFUNCTION", 16))
		return_value = _readRangeTblFunction();
	else if (MATCH("TABLESAMPLECLAUSE", 17))
		return_value = _readTableSampleClause();
	else if (MATCH("NOTIFYSTMT", 10))
		return_value = _readNotifyStmt();
	else if (MATCH("DEFELEM", 7))
		return_value = _readDefElem();
	else if (MATCH("DECLARECURSORSTMT", 17))
		return_value = _readDeclareCursorStmt();
	else if (MATCH("PLANNEDSTMT", 11))
		return_value = _readPlannedStmt();
	else if (MATCH("PLAN", 4))
		return_value = _readPlan();
	else if (MATCH("RESULT", 6))
		return_value = _readResult();
	else if (MATCH("PROJECTSET", 10))
		return_value = _readProjectSet();
	else if (MATCH("MODIFYTABLE", 11))
		return_value = _readModifyTable();
	else if (MATCH("APPEND", 6))
		return_value = _readAppend();
	else if (MATCH("MERGEAPPEND", 11))
		return_value = _readMergeAppend();
	else if (MATCH("RECURSIVEUNION", 14))
		return_value = _readRecursiveUnion();
	else if (MATCH("BITMAPAND", 9))
		return_value = _readBitmapAnd();
	else if (MATCH("BITMAPOR", 8))
		return_value = _readBitmapOr();
	else if (MATCH("SCAN", 4))
		return_value = _readScan();
	else if (MATCH("SEQSCAN", 7))
		return_value = _readSeqScan();
	else if (MATCH("SAMPLESCAN", 10))
		return_value = _readSampleScan();
	else if (MATCH("INDEXSCAN", 9))
		return_value = _readIndexScan();
	else if (MATCH("INDEXONLYSCAN", 13))
		return_value = _readIndexOnlyScan();
	else if (MATCH("BITMAPINDEXSCAN", 15))
		return_value = _readBitmapIndexScan();
	else if (MATCH("BITMAPHEAPSCAN", 14))
		return_value = _readBitmapHeapScan();
	else if (MATCH("TIDSCAN", 7))
		return_value = _readTidScan();
	else if (MATCH("TIDRANGESCAN", 12))
		return_value = _readTidRangeScan();
	else if (MATCH("SUBQUERYSCAN", 12))
		return_value = _readSubqueryScan();
	else if (MATCH("FUNCTIONSCAN", 12))
		return_value = _readFunctionScan();
	else if (MATCH("VALUESSCAN", 10))
		return_value = _readValuesScan();
	else if (MATCH("TABLEFUNCSCAN", 13))
		return_value = _readTableFuncScan();
	else if (MATCH("CTESCAN", 7))
		return_value = _readCteScan();
	else if (MATCH("NAMEDTUPLESTORESCAN", 19))
		return_value = _readNamedTuplestoreScan();
	else if (MATCH("WORKTABLESCAN", 13))
		return_value = _readWorkTableScan();
	else if (MATCH("FOREIGNSCAN", 11))
		return_value = _readForeignScan();
	else if (MATCH("CUSTOMSCAN", 10))
		return_value = _readCustomScan();
	else if (MATCH("JOIN", 4))
		return_value = _readJoin();
	else if (MATCH("NESTLOOP", 8))
		return_value = _readNestLoop();
	else if (MATCH("MERGEJOIN", 9))
		return_value = _readMergeJoin();
	else if (MATCH("HASHJOIN", 8))
		return_value = _readHashJoin();
	else if (MATCH("MATERIAL", 8))
		return_value = _readMaterial();
	else if (MATCH("MEMOIZE", 7))
		return_value = _readMemoize();
	else if (MATCH("SORT", 4))
		return_value = _readSort();
	else if (MATCH("INCREMENTALSORT", 15))
		return_value = _readIncrementalSort();
	else if (MATCH("GROUP", 5))
		return_value = _readGroup();
	else if (MATCH("AGG", 3))
		return_value = _readAgg();
	else if (MATCH("WINDOWAGG", 9))
		return_value = _readWindowAgg();
	else if (MATCH("UNIQUE", 6))
		return_value = _readUnique();
	else if (MATCH("GATHER", 6))
		return_value = _readGather();
	else if (MATCH("GATHERMERGE", 11))
		return_value = _readGatherMerge();
	else if (MATCH("HASH", 4))
		return_value = _readHash();
	else if (MATCH("SETOP", 5))
		return_value = _readSetOp();
	else if (MATCH("LOCKROWS", 8))
		return_value = _readLockRows();
	else if (MATCH("LIMIT", 5))
		return_value = _readLimit();
	else if (MATCH("NESTLOOPPARAM", 13))
		return_value = _readNestLoopParam();
	else if (MATCH("PLANROWMARK", 11))
		return_value = _readPlanRowMark();
	else if (MATCH("PARTITIONPRUNEINFO", 18))
		return_value = _readPartitionPruneInfo();
	else if (MATCH("PARTITIONEDRELPRUNEINFO", 23))
		return_value = _readPartitionedRelPruneInfo();
	else if (MATCH("PARTITIONPRUNESTEPOP", 20))
		return_value = _readPartitionPruneStepOp();
	else if (MATCH("PARTITIONPRUNESTEPCOMBINE", 25))
		return_value = _readPartitionPruneStepCombine();
	else if (MATCH("PLANINVALITEM", 13))
		return_value = _readPlanInvalItem();
	else if (MATCH("SUBPLAN", 7))
		return_value = _readSubPlan();
	else if (MATCH("ALTERNATIVESUBPLAN", 18))
		return_value = _readAlternativeSubPlan();
	else if (MATCH("EXTENSIBLENODE", 14))
		return_value = _readExtensibleNode();
	else if (MATCH("PARTITIONBOUNDSPEC", 18))
		return_value = _readPartitionBoundSpec();
	else if (MATCH("PARTITIONRANGEDATUM", 19))
		return_value = _readPartitionRangeDatum();
  else if (MATCH("CREATECHANGESTREAMSTMT", 22))
		return_value = _readCreateChangeStreamStmt();
  else if (MATCH("CHANGESTREAMTRACKEDTABLE", 24))
		return_value = _readChangeStreamTrackedTable();
  else if (MATCH("ALTERCHANGESTREAMSTMT", 21))
    return_value = _readAlterChangeStreamStmt();
	// SPANGRES BEGIN
	else if (MATCH("CREATESEARCHINDEXSTMT", 21))
		return_value = _readCreateSearchIndexStmt();
	else if (MATCH("ALTERSEARCHINDEXSTMT", 20))
		return_value = _readAlterSearchIndexStmt();
	else if (MATCH("ALTERSEARCHINDEXCMD", 19))
		return_value = _readAlterSearchIndexCmd();
	// SPANGRES END
	else if (MATCH("LOCALITYGROUPOPTION", 19))
		return_value = _readLocalityGroupOption();
	else if (MATCH("CREATELOCALITYGROUPSTMT", 23))
		return_value = _readCreateLocalityGroupStmt();
	else if (MATCH("ALTERLOCALITYGROUPSTMT", 22))
		return_value = _readAlterLocalityGroupStmt();
	else if (MATCH("ALTERCOLUMNLOCALITYGROUPSTMT", 28))
		return_value = _readAlterColumnLocalityGroupStmt();
	else if (MATCH("CREATEROLESTMT", 14))
		return_value = _readCreateRoleStmt();
	else if (MATCH("DROPROLESTMT", 12))
		return_value = _readDropRoleStmt();
	else if (MATCH("GRANTSTMT", 9))
		return_value = _readGrantStmt();
	else if (MATCH("ACCESSPRIV", 10))
		return_value = _readAccessPriv();
	else if (MATCH("OBJECTWITHARGS", 14))
		return_value = _readObjectWithArgs();
	else if (MATCH("GRANTROLESTMT", 13))
		return_value = _readGrantRoleStmt();
	/* SPANGRES BEGIN */
	else if (MATCH("RAWSTMT", 7))
		return_value = _readRawStmt();
	else if (MATCH("A_CONST", 7))
		return_value = _readAConst();
	else if (MATCH("RESTARGET", 9))
		return_value = _readResTarget();
	else if (MATCH("SELECT", 6))
		return_value = _readSelectStmt();
	else if (MATCH("TYPECAST", 8))
		return_value = _readTypeCast();
	else if (MATCH("TYPENAME", 8))
		return_value = _readTypeName();
	else if (MATCH("FUNCCALL", 8))
		return_value = _readFuncCall();
	else if (MATCH("COLUMNREF", 9))
		return_value = _readColumnRef();
	else if (MATCH("AEXPR", 5))
		return_value = _readAExpr();
	else if (MATCH("A_STAR", 6))
		return_value = _readA_Star();
	else if (MATCH("A_ARRAYEXPR", 11))
		return_value = _readA_ArrayExpr();
	else if (MATCH("RANGESUBSELECT", 14))
		return_value = _readRangeSubselect();
	else if (MATCH("CREATESTMT", 10))
		return_value = _readCreateStmt();
	else if (MATCH("CREATEDBSTMT", 12))
		return_value = _readCreateDbStmt();
	else if (MATCH("COLUMNDEF", 9))
		return_value = _readColumnDef();
	else if (MATCH("PARTITIONSPEC", 13))
		return_value = _readPartitionSpec();
	else if (MATCH("PARTITIONELEM", 13))
		return_value = _readPartitionElem();
	else if (MATCH("TABLELIKECLAUSE", 15))
		return_value = _readTableLikeClause();
	else if (MATCH("COLLATECLAUSE", 13))
		return_value = _readCollateClause();
	else if (MATCH("CONSTRAINT", 10))
		return_value = _readConstraint();
	else if (MATCH("DROPSTMT", 8))
		return_value = _readDropStmt();
	else if (MATCH("ALTERDATABASESETSTMT", 20))
		return_value = _readAlterDatabaseSetStmt();
	else if (MATCH("VARIABLESETSTMT", 15))
		return_value = _readVariableSetStmt();
	else if (MATCH("INSERTSTMT", 10))
		return_value = _readInsertStmt();
	else if (MATCH("ONCONFLICTCLAUSE", 16))
		return_value = _readOnConflictClause();
	else if (MATCH("INFERCLAUSE", 11))
		return_value = _readInferClause();
	else if (MATCH("WITHCLAUSE", 10))
		return_value = _readWithClause();
	else if (MATCH("INDEXELEM", 9))
		return_value = _readIndexElem();
	else if (MATCH("UPDATESTMT", 10))
		return_value = _readUpdateStmt();
	else if (MATCH("MULTIASSIGNREF", 14))
		return_value = _readMultiAssignRef();
	else if (MATCH("DELETESTMT", 10))
		return_value = _readDeleteStmt();
	else if (MATCH("INDEXSTMT", 9))
		return_value = _readIndexStmt();
	else if (MATCH("ALTERTABLESTMT", 14))
		return_value = _readAlterTableStmt();
	else if (MATCH("ALTERTABLECMD", 13))
		return_value = _readAlterTableCmd();
	else if (MATCH("ROLESPEC", 8))
		return_value = _readRoleSpec();
	else if (MATCH("WINDOWDEF", 9))
		return_value = _readWindowDef();
	else if (MATCH("SORTBY", 6))
		return_value = _readSortBy();
	else if (MATCH("PREPARESTMT", 11))
		return_value = _readPrepareStmt();
	else if (MATCH("EXECUTESTMT", 11))
		return_value = _readExecuteStmt();
	else if (MATCH("PARAMREF", 8))
		return_value = _readParamRef();
	else if (MATCH("INTERLEAVESPEC", 14))
		return_value = _readInterleaveSpec();
	else if (MATCH("EXPLAINSTMT", 11))
		return_value = _readExplainStmt();
	else if (MATCH("VACUUMSTMT", 10))
		return_value = _readVacuumStmt();
	else if (MATCH("VACUUMRELATION", 14))
		return_value = _readVacuumRelation();
	else if (MATCH("TRUNCATESTMT", 12))
		return_value = _readTruncateStmt();
	else if (MATCH("TRANSACTIONSTMT",15))
		return_value = _readTransactionStmt();
	else if (MATCH("CREATETABLEASSTMT",17))
		return_value = _readCreateTableAsStmt();
	else if (MATCH("CREATESCHEMASTMT", 16))
		return_value = _readCreateSchemaStmt();
	else if (MATCH("VIEWSTMT", 8))
		return_value = _readViewStmt();
	else if (MATCH("ALTERSPANGRESSTATSSTMT", 22))
		return_value = _readAlterSpangresStatsStmt();
	else if (MATCH("A_INDIRECTION", 13))
		return_value = _readAIndirection();
	else if (MATCH("A_INDICES", 9))
		return_value = _readAIndices();
	else if (MATCH("TABLETTL", 8))
		return_value = _readTtlSpangres();
	else if (MATCH("RANGEFUNCTION", 13))
		return_value = _readRangeFunction();
	else if (MATCH("CREATESEQSTMT", 13))
		return_value = _readCreateSeqStmt();
	else if (MATCH("ALTERSEQSTMT", 12))
		return_value = _readAlterSeqStmt();
	else if (MATCH("RENAMESTMT", 10))
		return_value = _readRenameStmt();
	else if (MATCH("TABLERENAMEOP", 13))
		return_value = _readTableRenameOp();
	else if (MATCH("TABLECHAINEDRENAMESTMT", 22))
		return_value = _readTableChainedRenameStmt();
	else if (MATCH("SYNONYMCLAUSE", 13))
		return_value = _readSynonymClause();
	else if (MATCH("CALLSTMT", 8))
		return_value = _readCallStmt();
	else if (MATCH("ALTEROWNERSTMT", 14))
		return_value = _readAlterOwnerStmt();
	else if (MATCH("ALTERSTATSSTMT", 14))
		return_value = _readAlterStatsStmt();
	else if (MATCH("ALTEROBJECTSCHEMASTMT", 21))
		return_value = _readAlterObjectSchemaStmt();
	else if (MATCH("LOCKINGCLAUSE", 13))
		return_value = _readLockingClause();
	/* SPANGRES END */
	else
	{
		/* SPANGRES BEGIN */
		elog(WARNING, "badly formatted node string \"%.32s\"...", token);

		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			errmsg("statement type is not supported")));
		/* SPANGRES END */

		return_value = NULL;	/* keep compiler quiet */
	}

	return (Node *) return_value;
}


/*
 * readDatum
 *
 * Given a string representation of a constant, recreate the appropriate
 * Datum.  The string representation embeds length info, but not byValue,
 * so we must be told that.
 */
Datum
readDatum(bool typbyval)
{
	Size		length,
				i;
	int			tokenLength;
	const char *token;
	Datum		res;
	char	   *s;

	/*
	 * read the actual length of the value
	 */
	token = pg_strtok(&tokenLength);
	length = atoui(token);

	token = pg_strtok(&tokenLength);	/* read the '[' */
	if (token == NULL || token[0] != '[')
		elog(ERROR, "expected \"[\" to start datum, but got \"%s\"; length = %zu",
			 token ? token : "[NULL]", length);

	if (typbyval)
	{
		if (length > (Size) sizeof(Datum))
			elog(ERROR, "byval datum but length = %zu", length);
		res = (Datum) 0;
		s = (char *) (&res);
		for (i = 0; i < (Size) sizeof(Datum); i++)
		{
			token = pg_strtok(&tokenLength);
			/* SPANGRES BEGIN */
			// Safely handle if `token` is NULL due to reaching end of input
			s[i] = token ? (char) atoi(token) : 0;
			/* SPANGRES END */
		}
	}
	else if (length <= 0)
		res = (Datum) NULL;
	else
	{
		s = (char *) palloc(length);
		for (i = 0; i < length; i++)
		{
			token = pg_strtok(&tokenLength);
			/* SPANGRES BEGIN */
			// Safely handle if `token` is NULL due to reaching end of input
			s[i] = token ? (char) atoi(token) : 0;
			/* SPANGRES END */
		}
		res = PointerGetDatum(s);
	}

	token = pg_strtok(&tokenLength);	/* read the ']' */
	if (token == NULL || token[0] != ']')
		elog(ERROR, "expected \"]\" to end datum, but got \"%s\"; length = %zu",
			 token ? token : "[NULL]", length);

	return res;
}

/*
 * readAttrNumberCols
 */
AttrNumber *
readAttrNumberCols(int numCols)
{
	int			tokenLength,
				i;
	const char *token;
	AttrNumber *attr_vals;

	if (numCols <= 0)
		return NULL;

	attr_vals = (AttrNumber *) palloc(numCols * sizeof(AttrNumber));
	for (i = 0; i < numCols; i++)
	{
		token = pg_strtok(&tokenLength);
		attr_vals[i] = atoi(token);
	}

	return attr_vals;
}

/*
 * readOidCols
 */
Oid *
readOidCols(int numCols)
{
	int			tokenLength,
				i;
	const char *token;
	Oid		   *oid_vals;

	if (numCols <= 0)
		return NULL;

	oid_vals = (Oid *) palloc(numCols * sizeof(Oid));
	for (i = 0; i < numCols; i++)
	{
		token = pg_strtok(&tokenLength);
		oid_vals[i] = atooid(token);
	}

	return oid_vals;
}

/*
 * readIntCols
 */
int *
readIntCols(int numCols)
{
	int			tokenLength,
				i;
	const char *token;
	int		   *int_vals;

	if (numCols <= 0)
		return NULL;

	int_vals = (int *) palloc(numCols * sizeof(int));
	for (i = 0; i < numCols; i++)
	{
		token = pg_strtok(&tokenLength);
		int_vals[i] = atoi(token);
	}

	return int_vals;
}

/*
 * readBoolCols
 */
bool *
readBoolCols(int numCols)
{
	int			tokenLength,
				i;
	const char *token;
	bool	   *bool_vals;

	if (numCols <= 0)
		return NULL;

	bool_vals = (bool *) palloc(numCols * sizeof(bool));
	for (i = 0; i < numCols; i++)
	{
		token = pg_strtok(&tokenLength);
		bool_vals[i] = strtobool(token);
	}

	return bool_vals;
}
