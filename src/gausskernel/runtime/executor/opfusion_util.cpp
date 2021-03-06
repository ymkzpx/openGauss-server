/*
 * Copyright (c) 2020 Huawei Technologies Co.,Ltd.
 *
 * openGauss is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 * http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * ---------------------------------------------------------------------------------------
 *
 * opfusion_util.cpp
 * The main part of the bypass executor. Instead of processing through the origin
 * Portal executor, the bypass executor provides a shortcut when the query is
 * simple.
 *
 * IDENTIFICATION
 * src/gausskernel/runtime/executor/opfusion_util.cpp
 *
 * ---------------------------------------------------------------------------------------
 */
#include "opfusion/opfusion_util.h"

#include "access/printtup.h"
#include "access/transam.h"
#include "catalog/pg_aggregate.h"
#include "commands/copy.h"
#include "executor/nodeIndexscan.h"
#include "libpq/pqformat.h"
#include "mb/pg_wchar.h"
#include "nodes/makefuncs.h"
#include "optimizer/clauses.h"
#include "parser/parsetree.h"
#include "utils/dynahash.h"
#include "utils/lsyscache.h"
#include "utils/snapmgr.h"
#include "parser/parse_coerce.h"

const char *getBypassReason(FusionType result)
{
    switch (result) {
        case NONE_FUSION: {
            return "Bypass not executed";
        }
        case SELECT_FUSION: {
            return "Bypass executed through select fusion";
        }

        case SELECT_FOR_UPDATE_FUSION: {
            return "Bypass executed through select for update fusion";
        }

        case INSERT_FUSION: {
            return "Bypass executed through insert fusion";
        }

        case UPDATE_FUSION: {
            return "Bypass executed through update fusion";
        }

        case DELETE_FUSION: {
            return "Bypass executed through delete fusion";
        }

        case NOBYPASS_NO_SIMPLE_PLAN: {
            return "Bypass not executed because the plan of query is not a simple plan";
        }

        case NOBYPASS_NO_QUERY_TYPE: {
            return "Bypass not executed because query is not an avaliable bypass statement such as select, delete, "
                "update and insert";
        }

        case NOBYPASS_NO_INDEXSCAN: {
            return "Bypass not executed because query\'s scan operator is not index";
        }

        case NOBYPASS_INDEXSCAN_WITH_ORDERBY: {
            return "Bypass not executed because query used indexscan with order by clause method";
        }

        case NOBYPASS_INDEXSCAN_WITH_QUAL: {
            return "Bypass not executed because query used indexscan with qual";
        }

        case NOBYPASS_INDEXSCAN_CONDITION_INVALID: {
            return "Bypass not executed because query used unsupported indexscan condition";
        }

        case NOBYPASS_INDEXONLYSCAN_WITH_ORDERBY: {
            return "Bypass not executed because query used indexonlyscan with order by clause method";
        }

        case NOBYPASS_INDEXONLYSCAN_WITH_QUAL: {
            return "Bypass not executed because query used indexonlyscan with qual";
        }

        case NOBYPASS_INDEXONLYSCAN_CONDITION_INVALID: {
            return "Bypass not executed because query used invalid indexonlyscan condition";
        }

        case NOBYPASS_TARGET_WITH_SYS_COL: {
            return "Bypass not executed because query used the target list with system column";
        }

        case NOBYPASS_TARGET_WITH_NO_TABLE_COL: {
            return "Bypass not executed because query used the target list which only contains table's column";
        }

        case NOBYPASS_NO_TARGETENTRY: {
            return "Bypass not executed because the type of targetlist of query should be targetEntry";
        }

        case NOBYPASS_PARAM_TYPE_INVALID: {
            return "Bypass not executed because query used unsupported param type";
        }

        case NOBYPASS_DML_RELATION_NUM_INVALID: {
            return "Bypass not executed because query\'s relation number is not 1";
        }

        case NOBYPASS_DML_RELATION_NOT_SUPPORT: {
            return "Bypass not executed because query\'s relation is not support";
        }

        case NOBYPASS_DML_TARGET_TYPE_INVALID: {
            return "Bypass not executed because query used unsupported DML target type";
        }

        case NOBYPASS_EXP_NOT_SUPPORT: {
            return "Bypass not executed because the expression of query is not support";
        }

        case NOBYPASS_LIMITOFFSET_CONST_LESS_THAN_ZERO: {
            return "Bypass not executed because query used limit offset grammar with const less than zero";
        }

        case NOBYPASS_LIMITCOUNT_CONST_LESS_THAN_ZERO: {
            return "Bypass not executed because query used limit count grammar with const less than zero";
        }

        case NOBYPASS_LIMIT_NOT_CONST: {
            return "Bypass not executed because query used limit grammar with a non-constant value";
        }

        case NOBYPASS_NO_SIMPLE_INSERT: {
            return "Bypass not executed because query combines insert operator with others";
        }

        case NOBYPASS_INVALID_SELECT_FOR_UPDATE: {
            return "Bypass not executed because query used invalid select for update";
        }

        case NOBYPASS_INVALID_MODIFYTABLE: {
            return "Bypass not executed because query used invalid modifytable";
        }

        case NOBYPASS_STREAM_NOT_SUPPORT: {
            return "Bypass not executed because query used streaming plan";
        }

        case NOBYPASS_NULLTEST_TYPE_INVALID: {
            return "Bypass not executed because query used invalid composite type";
            break;
        }
        
        case NOBYPASS_INVALID_PLAN: {
            return "Bypass not executed because invalid plan node";
            break;
        }

        case NOBYPASS_NOT_PLAIN_AGG: {
            return "Bypass not executed because it's not a plain agg query";
            break;
        }

        case NOBYPASS_ONE_TARGET_ALLOWED: {
            return "Bypass not executed because it's just one target allowed";
            break;
        }

        case NOBYPASS_AGGREF_TARGET_ALLOWED: {
            return "Bypass not executed because it's just aggref allowed";
            break;
        }

        case NOBYPASS_JUST_SUM_ALLOWED: {
            return "Bypass not executed because it's sum() allowed";
            break;
        }

        case NOBYPASS_JUST_VAR_FOR_AGGARGS: {
            return "Bypass not executed because it's Var type allowed for agg argument";
            break;
        }

        case NOBYPASS_JUST_MERGE_UNSUPPORTED: {
            return "Bypass not executed because it's unsupported that sort node just merge results";
            break;
        }

        case NOBYPASS_JUST_VAR_ALLOWED_IN_SORT: {
            return "Bypass not executed because it's Var type allowed for target in sort query";
            break;
        }

        case NOBYPASS_UPSERT_NOT_SUPPORT: {
            return "Bypass not support INSERT INTO ... ON DUPLICATE KEY UPDATE statement";
            break;
        }

        default: {
            Assert(0);
            ereport(ERROR,
                (errcode(ERRCODE_UNRECOGNIZED_NODE_TYPE), errmsg("unrecognized bypass support type: %d", (int)result)));
            return NULL;
        }
    }
}

void BypassUnsupportedReason(FusionType result)
{
    if (result == NONE_FUSION) {
        return;
    }
    if (u_sess->attr.attr_sql.opfusion_debug_mode == BYPASS_OFF) {
        return;
    }

    Assert(result != BYPASS_OK);
    Assert(u_sess->attr.attr_sql.opfusion_debug_mode == BYPASS_LOG);
    const char *bypass_reason = getBypassReason(result);

    int elevel = DEBUG4;
    if (result < BYPASS_OK) {
        ereport(elevel, (errmodule(MOD_OPFUSION), errcode(ERRCODE_LOG), errmsg("%s.", bypass_reason)));
    }
    if (result > BYPASS_OK) {
        ereport(elevel, (errmodule(MOD_OPFUSION), errcode(ERRCODE_LOG),
            errmsg("%s: \"%s\".", bypass_reason, t_thrd.postgres_cxt.debug_query_string)));
    }
}

bool checkFusionParam(Param *param, ParamListInfo boundParams)
{
    if (param->paramkind == PARAM_EXTERN && boundParams != NULL && param->paramid > 0 &&
        param->paramid <= boundParams->numParams) {
        ParamExternData *prm = &boundParams->params[param->paramid - 1];

        if (OidIsValid(prm->ptype) && (prm->pflags & PARAM_FLAG_CONST)) {
            return true;
        }
    }

    return false;
}

static bool checkFlinfo(Node *node)
{
    /* check whether the flinfo satisfy conditon */
    FmgrInfo *flinfo = NULL;
    flinfo = (FmgrInfo *)palloc(sizeof(FmgrInfo));
    fmgr_info(((FuncExpr *)node)->funcid, flinfo);
    if (flinfo->fn_retset == true || (flinfo->fn_strict == false && flinfo->fn_expr == NULL)) {
        pfree(flinfo);
        flinfo = NULL;
        return false;
    }
    pfree(flinfo);
    flinfo = NULL;
    return true;
}

static bool checkExpr(Node *node, bool is_first)
{
    NodeTag tag = nodeTag(node);
    switch (tag) {
        case T_Const:
        case T_Var:
        case T_Param: {
            return true;
        }

        case T_FuncExpr: {
            if (is_first == false) {
                return false;
            }
            if (!checkFlinfo(node)) {
                return false;
            }
            bool found_ptr = true;
            void *ans = NULL;
            ans = hash_search(g_instance.exec_cxt.function_id_hashtbl, (void *)&((FuncExpr *)node)->funcid, HASH_FIND,
                &found_ptr);
            if (found_ptr == false) {
                return false;
            }
            List *args = ((FuncExpr *)node)->args;
            if (list_length(args) == 0 || list_length(args) > 4) {
                return false;
            }

            bool result = true;
            ListCell *lc = NULL;
            foreach (lc, args) {
                result = result && checkExpr((Node *)lfirst(lc), is_first);
                is_first = false;
            }
            return result;
        }

        case T_OpExpr: {
            if (is_first == false) {
                return false;
            }
            /* check whether return set */
            if (((OpExpr *)node)->opretset == true) {
                return false;
            }

            List *args = ((OpExpr *)node)->args;
            if (list_length(args) == 0 || list_length(args) > 4) {
                return false;
            }

            bool result = true;
            ListCell *lc = NULL;
            foreach (lc, args) {
                result = result && checkExpr((Node *)lfirst(lc), is_first);
                is_first = false;
            }
            return result;
        }

        case T_RelabelType: {
            return checkExpr((Node *)((RelabelType *)node)->arg, is_first);
        }

        default: {
            return false;
        }
    }
}

FusionType checkFusionAgg(Agg *node, ParamListInfo params)
{
    if (node->plan.righttree != NULL || node->plan.lefttree == NULL) {
        return NOBYPASS_INVALID_PLAN;
    }

    /* check whether to have order by */
    if (node->aggstrategy != AGG_PLAIN ||
            node->groupingSets > 0) {
        return NOBYPASS_NOT_PLAIN_AGG;
    }

    Assert (node->numCols == 0);

    if (list_length(node->plan.targetlist) != 1 ||
            node->plan.qual != NULL) {
        return NOBYPASS_ONE_TARGET_ALLOWED;
    }

    TargetEntry *res = (TargetEntry *)linitial(node->plan.targetlist);
    if (!IsA(res->expr, Aggref)) {
        return NOBYPASS_AGGREF_TARGET_ALLOWED;
    }

    Aggref *aggref = (Aggref *)res->expr;

    if (list_length(aggref->args) != 1 ||
            aggref->aggorder != NULL ||
            aggref->aggdistinct != NULL ||
            aggref->aggvariadic) {
        return NOBYPASS_AGGREF_TARGET_ALLOWED;
    }

    switch (aggref->aggfnoid) {
        case INT2SUMFUNCOID:
        case INT4SUMFUNCOID:
        case INT8SUMFUNCOID:
        case NUMERICSUMFUNCOID:
            break;
        default:
            return NOBYPASS_JUST_SUM_ALLOWED;
    }

    res = (TargetEntry *)linitial(aggref->args);
    if (!IsA(res->expr, Var)) {
        return NOBYPASS_JUST_VAR_FOR_AGGARGS;
    }

    return BYPASS_OK;
}

FusionType checkFusionSort(Sort *node, ParamListInfo params)
{
    if (node->plan.righttree != NULL || node->plan.lefttree == NULL) {
        return NOBYPASS_INVALID_PLAN;
    }

    if (node->srt_start_merge) {
        return NOBYPASS_JUST_MERGE_UNSUPPORTED;
    }

    ListCell *lc = NULL;
    /* check whether targetlist is simple */
    foreach (lc, node->plan.targetlist) {
        Assert (IsA(lfirst(lc), TargetEntry));

        TargetEntry *res = (TargetEntry *)lfirst(lc);
        if (!IsA(res->expr, Var)) {
            return NOBYPASS_JUST_VAR_ALLOWED_IN_SORT;
        }

        Var *var = (Var *)res->expr;
        /* System columns, such as ctid and xmin, are not supported. */
        if (var->varoattno <= 0) {
            return NOBYPASS_TARGET_WITH_SYS_COL;
        }
    }

    return BYPASS_OK;
 }


template <bool is_dml, bool isonlyindex> FusionType checkFusionIndexScan(Node *node, ParamListInfo params)
{
    List *tarlist = NULL;
    List *indexorderby = NULL;
    List *indexqual = NULL;
    List *qual = NULL;
    if (isonlyindex) {
        tarlist = ((IndexOnlyScan *)node)->scan.plan.targetlist;
        indexorderby = ((IndexOnlyScan *)node)->indexorderby;
        indexqual = ((IndexOnlyScan *)node)->indexqual;
        qual = ((IndexOnlyScan *)node)->scan.plan.qual;

        if (indexorderby != NULL) {
            return NOBYPASS_INDEXONLYSCAN_WITH_ORDERBY;
        }
    } else {
        tarlist = ((IndexScan *)node)->scan.plan.targetlist;
        indexorderby = ((IndexScan *)node)->indexorderby;
        indexqual = ((IndexScan *)node)->indexqual;
        qual = ((IndexScan *)node)->scan.plan.qual;

        if (indexorderby != NULL) {
            return NOBYPASS_INDEXSCAN_WITH_ORDERBY;
        }
    }

    ListCell *lc = NULL;

    if (is_dml == false) {
        /* check whether targetlist is simple */
        foreach (lc, tarlist) {
            if (!IsA(lfirst(lc), TargetEntry)) {
                return NOBYPASS_NO_TARGETENTRY;
            }
            TargetEntry *res = (TargetEntry *)lfirst(lc);

            if (res->resjunk == true) {
                continue;
            }

            if (!IsA(res->expr, Var)) {
                return NOBYPASS_TARGET_WITH_NO_TABLE_COL;
            }

            Var *var = (Var*)res->expr;
            AttrNumber attno = var->varno == INDEX_VAR ? var->varoattno : var->varattno;
            if (attno <= 0) {
                return NOBYPASS_TARGET_WITH_SYS_COL;
            }
        }
    }

    /* check whether index expression is simple */
    foreach (lc, indexqual) {
        if (IsA(lfirst(lc), NullTest)) {
            if (((NullTest *)lfirst(lc))->argisrow == true) {
                return NOBYPASS_NULLTEST_TYPE_INVALID;
            }
            continue;
        }

        if (!IsA(lfirst(lc), OpExpr)) {
            return NOBYPASS_INDEXSCAN_CONDITION_INVALID;
        }

        OpExpr *opexpr = (OpExpr *)lfirst(lc);

        if (list_length(opexpr->args) != 2) {
            if (isonlyindex) {
                return NOBYPASS_INDEXONLYSCAN_CONDITION_INVALID;
            } else {
                return NOBYPASS_INDEXSCAN_CONDITION_INVALID;
            }
        }

        Expr *leftop = NULL;  /* expr on lhs of operator */
        Expr *rightop = NULL; /* expr on rhs ... */

        leftop = (Expr *)linitial(opexpr->args);
        if (leftop != NULL && IsA(leftop, RelabelType)) {
            leftop = ((RelabelType *)leftop)->arg;
        }

        rightop = (Expr *)lsecond(opexpr->args);
        if (rightop != NULL && IsA(rightop, RelabelType)) {
            rightop = ((RelabelType *)rightop)->arg;
        }

        if (leftop == NULL || rightop == NULL) {
            if (isonlyindex) {
                return NOBYPASS_INDEXONLYSCAN_CONDITION_INVALID;
            } else {
                return NOBYPASS_INDEXSCAN_CONDITION_INVALID;
            }
        }

        if (!IsA(leftop, Var) || (!IsA(rightop, Param) && !IsA(rightop, Const))) {
            if (isonlyindex) {
                return NOBYPASS_INDEXONLYSCAN_CONDITION_INVALID;
            } else {
                return NOBYPASS_INDEXSCAN_CONDITION_INVALID;
            }
        }

        if (IsA(rightop, Param) && !checkFusionParam((Param *)rightop, params)) {
            return NOBYPASS_PARAM_TYPE_INVALID;
        }
    }

    /* check whether filter expression is simple */
    if (qual != NULL) {
        if (isonlyindex) {
            return NOBYPASS_INDEXONLYSCAN_WITH_QUAL;
        } else {
            return NOBYPASS_INDEXSCAN_WITH_QUAL;
        }
    }
    return BYPASS_OK;
}

FusionType checkFusionNestLoop(NestLoop *node, ParamListInfo params)
{
    Join     *joinNode = &node->join;
    Plan     *plan = &joinNode->plan;
    ListCell *lc = NULL;

    /* NestLoop */
    if (node->nestParams != NIL || node->materialAll == true) {
        return NONE_FUSION;
    }

    /* join */
    if (joinNode->jointype     != JOIN_INNER ||
        joinNode->joinqual     != NIL        ||
        joinNode->nulleqqual   != NIL        ||
        joinNode->optimizable  == true       ||
        joinNode->skewoptimize != 0) {
        return NONE_FUSION;
    }

    /* check whether targetlist is simple */
    foreach (lc, plan->targetlist) {
        Assert (IsA(lfirst(lc), TargetEntry));
        TargetEntry *res = (TargetEntry *)lfirst(lc);
        if (!IsA(res->expr, Var)) {
            return NONE_FUSION;
        }

        Var *var = (Var *)res->expr;
        /* System columns, such as ctid and xmin, are not supported. */
        if (var->varoattno <= 0) {
            return NONE_FUSION;
        }
    }

    /* Plan */
    if (plan->lefttree == NULL           ||
        !IsA(plan->lefttree, IndexScan)  ||
        plan->lefttree->lefttree != NULL ||
        plan->righttree == NULL          ||
        !IsA(plan->righttree, IndexScan) ||
        plan->righttree->lefttree != NULL) {
        return NONE_FUSION;
    }

    /* IndexScan */
    FusionType ttype;
    ttype = checkFusionIndexScan<false, false>((Node*)plan->lefttree, params);
    if (ttype > BYPASS_OK) {
        return ttype;
    }
    ttype = checkFusionIndexScan<false, false>((Node*)plan->righttree, params);
    if (ttype > BYPASS_OK) {
        return ttype;
    }

    /* OK */
    return NESTLOOP_INDEX_FUSION;
}

FusionType getSelectFusionType(List *stmt_list, ParamListInfo params)
{
    FusionType ftype = SELECT_FUSION;
    bool limitplan = false;

    /* check whether is only one index scan */
    Plan *top_plan = ((PlannedStmt *)linitial(stmt_list))->planTree;

#ifndef ENABLE_MULTIPLE_NODES
        /* check for nestloop */
        if (u_sess->attr.attr_sql.enable_beta_opfusion &&
            u_sess->attr.attr_sql.enable_beta_nestloop_fusion && IsA(top_plan, NestLoop)) {
            return checkFusionNestLoop((NestLoop *)top_plan, params);
        }
#endif

    /* check for limit */
    if (IsA(top_plan, Limit)) {
        Limit *limit = (Limit *)top_plan;
        if (limit->limitOffset != NULL) {
            if (IsA(limit->limitOffset, Const)) {
                Assert(((Const *)limit->limitOffset)->consttype == 20);
                if (DatumGetInt64(((Const *)limit->limitOffset)->constvalue) < 0) {
                    return NOBYPASS_LIMITOFFSET_CONST_LESS_THAN_ZERO;
                }
            } else {
                return NOBYPASS_LIMIT_NOT_CONST;
            }
        }
        if (limit->limitCount != NULL) {
            if (IsA(limit->limitCount, Const)) {
                Assert(((Const *)limit->limitCount)->consttype == 20);
                if (DatumGetInt64(((Const *)limit->limitCount)->constvalue) < 0) {
                    return NOBYPASS_LIMITCOUNT_CONST_LESS_THAN_ZERO;
                }
            } else {
                return NOBYPASS_LIMIT_NOT_CONST;
            }
        }
        top_plan = top_plan->lefttree;
        limitplan = true;
    }

    /* check select for update */
    if (IsA(top_plan, LockRows)) {
        LockRows *lockrows = (LockRows *)top_plan;
        bool is_select_for_update =
            (list_length(lockrows->rowMarks) == 1 && IsA(linitial(lockrows->rowMarks), PlanRowMark) &&
            ((PlanRowMark *)linitial(lockrows->rowMarks))->markType == ROW_MARK_EXCLUSIVE &&
            ((PlanRowMark *)linitial(lockrows->rowMarks))->noWait == false);
        if (is_select_for_update) {
            top_plan = top_plan->lefttree;
            ftype = SELECT_FOR_UPDATE_FUSION;
        } else {
            return NOBYPASS_INVALID_SELECT_FOR_UPDATE;
        }
    }

#ifndef ENABLE_MULTIPLE_NODES
        /* check select for agg */
        if (u_sess->attr.attr_sql.enable_beta_opfusion && !limitplan && IsA(top_plan, Agg) && 
            ftype == SELECT_FUSION) {
            FusionType ttype;
            ttype = checkFusionAgg((Agg *)top_plan, params);
            if (ttype > BYPASS_OK) {
                return ttype;
            }
            ftype = AGG_INDEX_FUSION;
            top_plan = top_plan->lefttree;
        }
    
        /* check select for sort */
        if (u_sess->attr.attr_sql.enable_beta_opfusion && !limitplan && IsA(top_plan, Sort) &&
            ftype == SELECT_FUSION) {
    
            FusionType ttype;
            ttype = checkFusionSort((Sort*)top_plan, params);
            if (ttype > BYPASS_OK) {
                return ttype;
            }
    
            ftype = SORT_INDEX_FUSION;
            top_plan = top_plan->lefttree;
        }
#endif

    /* check for indexscan or indexonlyscan */
    if ((IsA(top_plan, IndexScan) || IsA(top_plan, IndexOnlyScan)) && top_plan->lefttree == NULL) {
        FusionType ttype;
        if (IsA(top_plan, IndexScan)) {
            ttype = checkFusionIndexScan<false, false>((Node *)top_plan, params);
        } else {
            ttype = checkFusionIndexScan<false, true>((Node *)top_plan, params);
        }
        /* check failed */
        if (ttype > BYPASS_OK) {
            return ttype;
        }
    } else {
        return NOBYPASS_NO_INDEXSCAN;
    }

    return ftype;
}

FusionType checkTargetlist(List *targetList, FusionType ftype)
{
    ListCell *lc = NULL;
    TargetEntry *target = NULL;
    foreach (lc, targetList) {
        target = (TargetEntry *)lfirst(lc);
        if (!checkExpr((Node *)target->expr, true)) {
            return NOBYPASS_EXP_NOT_SUPPORT;
        }
    }
    return ftype;
}

bool checkDMLRelation(Relation rel, PlannedStmt *plannedstmt)
{
    if (rel->rd_rel->relkind != RELKIND_RELATION || rel->rd_rel->relhasrules || rel->rd_rel->relhastriggers ||
        rel->rd_rel->relhasoids || rel->rd_rel->relhassubclass || RelationIsPartitioned(rel) ||
        RelationIsColStore(rel) || RelationInRedistribute(rel) || plannedstmt->hasReturning) {
        return true;
    }
    return false;
}
FusionType getInsertFusionType(List *stmt_list, ParamListInfo params)
{
    FusionType ftype = INSERT_FUSION;

    /* check result relaiton */
    PlannedStmt *plannedstmt = (PlannedStmt *)linitial(stmt_list);
    if (plannedstmt->resultRelations == NULL || list_length(plannedstmt->resultRelations) != 1) {
        return NOBYPASS_DML_RELATION_NUM_INVALID;
    }

    Plan *top_plan = plannedstmt->planTree;
    if (!IsA(top_plan, ModifyTable) || top_plan->plan_node_id != 1) {
        return NOBYPASS_INVALID_MODIFYTABLE;
    }

    /* check subquery num */
    ModifyTable *node = (ModifyTable *)top_plan;
    if (list_length(node->plans) != 1) {
        return NOBYPASS_NO_SIMPLE_PLAN;
    }
    if (!IsA(linitial(node->plans), BaseResult)) {
        return NOBYPASS_NO_SIMPLE_INSERT;
    }
    BaseResult *base = (BaseResult *)linitial(node->plans);
    if (base->plan.lefttree != NULL || base->plan.initPlan != NIL || base->resconstantqual != NULL) {
        return NOBYPASS_NO_SIMPLE_INSERT;
    }
    if (node->upsertAction != UPSERT_NONE) {
        return NOBYPASS_UPSERT_NOT_SUPPORT;
    }

    /* check relation */
    Index res_rel_idx = linitial_int(plannedstmt->resultRelations);
    Oid relid = getrelid(res_rel_idx, plannedstmt->rtable);
    Relation rel = heap_open(relid, AccessShareLock);

    for (int i = 0; i < rel->rd_att->natts; i++) {
        if (rel->rd_att->attrs[i]->attisdropped) {
            continue;
        }
        /* check whether the attrs of */
        HeapTuple tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(rel->rd_att->attrs[i]->atttypid));
        if (!HeapTupleIsValid(tuple)) {
            /* should not happen */
            ereport(ERROR, (errcode(ERRCODE_CACHE_LOOKUP_FAILED),
                errmsg("cache lookup failed for type %u", rel->rd_att->attrs[i]->atttypid)));
        }
        Form_pg_type type_form = (Form_pg_type)GETSTRUCT(tuple);
        ReleaseSysCache(tuple);
        if (type_form->typtype != 'b') {
            heap_close(rel, AccessShareLock);
            return NOBYPASS_DML_TARGET_TYPE_INVALID;
        }

        if (TypeCategory(rel->rd_att->attrs[i]->atttypid) == TYPCATEGORY_STRING &&
            rel->rd_att->attrs[i]->atttypid != TEXTOID) {
            heap_close(rel, AccessShareLock);
            return NOBYPASS_DML_TARGET_TYPE_INVALID;
        }
    }
    if (checkDMLRelation(rel, plannedstmt)) {
        heap_close(rel, AccessShareLock);
        return NOBYPASS_DML_RELATION_NOT_SUPPORT;
    }
    heap_close(rel, AccessShareLock);
    /*
     * check targetlist
     * maybe expr type is FuncExpr because of type conversion.
     */
    BaseResult *base_result = (BaseResult *)linitial(node->plans);
    List *targetlist = base_result->plan.targetlist;
    return checkTargetlist(targetlist, ftype);
}

FusionType getUpdateFusionType(List *stmt_list, ParamListInfo params)
{
    FusionType ftype = UPDATE_FUSION;

    /* check result relaiton */
    PlannedStmt *plannedstmt = (PlannedStmt *)linitial(stmt_list);
    if (plannedstmt->resultRelations == NULL || list_length(plannedstmt->resultRelations) != 1) {
        return NOBYPASS_DML_RELATION_NUM_INVALID;
    }

    Plan *top_plan = plannedstmt->planTree;
    if (!IsA(top_plan, ModifyTable) || top_plan->plan_node_id != 1) {
        return NOBYPASS_INVALID_MODIFYTABLE;
    }

    /* check subquery num */
    ModifyTable *node = (ModifyTable *)top_plan;
    if (list_length(node->plans) != 1) {
        return NOBYPASS_NO_SIMPLE_PLAN;
    }

    if (!IsA(linitial(node->plans), IndexScan)) {
        return NOBYPASS_NO_INDEXSCAN;
    }

    /* check index scan */
    FusionType ttype = checkFusionIndexScan<true, false>((Node *)linitial(node->plans), params);
    /* check failed */
    if (ttype > BYPASS_OK) {
        return ttype;
    }

    /* check relation */
    Index res_rel_idx = linitial_int(plannedstmt->resultRelations);
    Oid relid = getrelid(res_rel_idx, plannedstmt->rtable);
    Relation rel = heap_open(relid, AccessShareLock);
    if (checkDMLRelation(rel, plannedstmt)) {
        heap_close(rel, AccessShareLock);
        return NOBYPASS_DML_RELATION_NOT_SUPPORT;
    }
    heap_close(rel, AccessShareLock);

    /* check target list */
    IndexScan *indexscan = (IndexScan *)linitial(node->plans);
    List *targetlist = indexscan->scan.plan.targetlist;
    return checkTargetlist(targetlist, ftype);
}

FusionType getDeleteFusionType(List *stmt_list, ParamListInfo params)
{
    FusionType ftype = DELETE_FUSION;

    /* check result relaiton */
    PlannedStmt *plannedstmt = (PlannedStmt *)linitial(stmt_list);
    if (plannedstmt->resultRelations == NULL || list_length(plannedstmt->resultRelations) != 1) {
        return NOBYPASS_DML_RELATION_NUM_INVALID;
    }

    Plan *top_plan = plannedstmt->planTree;
    if (!IsA(top_plan, ModifyTable) || top_plan->plan_node_id != 1) {
        return NOBYPASS_INVALID_MODIFYTABLE;
    }

    /* check subquery num */
    ModifyTable *node = (ModifyTable *)top_plan;
    if (list_length(node->plans) != 1) {
        return NOBYPASS_NO_SIMPLE_PLAN;
    }

    if (!IsA(linitial(node->plans), IndexScan)) {
        return NOBYPASS_NO_INDEXSCAN;
    }
    /* check index scan */
    FusionType ttype = checkFusionIndexScan<true, false>((Node *)linitial(node->plans), params);
    /* check failed */
    if (ttype > BYPASS_OK) {
        return ttype;
    }

    /* check relation */
    Index res_rel_idx = linitial_int(plannedstmt->resultRelations);
    Oid relid = getrelid(res_rel_idx, plannedstmt->rtable);
    Relation rel = heap_open(relid, AccessShareLock);
    if (checkDMLRelation(rel, plannedstmt)) {
        heap_close(rel, AccessShareLock);
        return NOBYPASS_DML_RELATION_NOT_SUPPORT;
    }
    heap_close(rel, AccessShareLock);

    return ftype;
}

void InitOpfusionFunctionId()
{
    /* init opfusion function_id hash table */
    HASHCTL ctl_func;
    errno_t rc;
    rc = memset_s(&ctl_func, sizeof(ctl_func), 0, sizeof(ctl_func));
    securec_check_c(rc, "\0", "\0");

    ctl_func.keysize = sizeof(Oid);
    ctl_func.entrysize = sizeof(Oid);
    ctl_func.hcxt = g_instance.instance_context;
    g_instance.exec_cxt.function_id_hashtbl =
        hash_create("Opfusion function id white-list", OPFUSION_FUNCTION_ID_MAX_HASH_SIZE, &ctl_func, HASH_ELEM);

    /* enter function id from array to hash table */
    uint32 i = 0;
    bool found_ptr = false;
    uint32 length = sizeof(function_id) / sizeof(Oid);
    void *ans = NULL;
    for (i = 0; i < length; i++) {
        ans = hash_search(g_instance.exec_cxt.function_id_hashtbl, (void *)&function_id[i], HASH_ENTER, &found_ptr);
    }
}
