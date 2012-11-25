#include "sr_flatten.h"

IRExpr* ML_(flatten_expression)(IRSB* sb, IRExpr* expr)
{
	switch (expr->tag) {
		case Iex_Get:
			break;
		case Iex_GetI:
			expr->Iex.GetI.ix = ML_(flatten_expression)(sb, expr->Iex.GetI.ix);
			break;
		case Iex_RdTmp:
			// RdTmp is already flat.
			return expr;
		case Iex_Qop:
			expr->Iex.Qop.details->arg1 = ML_(flatten_expression)(sb, expr->Iex.Qop.details->arg1);
			expr->Iex.Qop.details->arg2 = ML_(flatten_expression)(sb, expr->Iex.Qop.details->arg2);
			expr->Iex.Qop.details->arg3 = ML_(flatten_expression)(sb, expr->Iex.Qop.details->arg3);
			expr->Iex.Qop.details->arg4 = ML_(flatten_expression)(sb, expr->Iex.Qop.details->arg4);
			break;
		case Iex_Triop:
			expr->Iex.Triop.details->arg1 = ML_(flatten_expression)(sb, expr->Iex.Triop.details->arg1);
			expr->Iex.Triop.details->arg2 = ML_(flatten_expression)(sb, expr->Iex.Triop.details->arg2);
			expr->Iex.Triop.details->arg3 = ML_(flatten_expression)(sb, expr->Iex.Triop.details->arg3);
			break;
		case Iex_Binop:
			expr->Iex.Binop.arg1 = ML_(flatten_expression)(sb, expr->Iex.Binop.arg1);
			expr->Iex.Binop.arg2 = ML_(flatten_expression)(sb, expr->Iex.Binop.arg2);
			break;
		case Iex_Unop:
			expr->Iex.Unop.arg = ML_(flatten_expression)(sb, expr->Iex.Unop.arg);
			break;
		case Iex_Load:
			expr->Iex.Load.addr = ML_(flatten_expression)(sb, expr->Iex.Load.addr);
			break;
		case Iex_Const:
			// Const is already flat.
			return expr;
		case Iex_Mux0X:
			expr->Iex.Mux0X.cond = ML_(flatten_expression)(sb, expr->Iex.Mux0X.cond);
			expr->Iex.Mux0X.expr0 = ML_(flatten_expression)(sb, expr->Iex.Mux0X.expr0);
			expr->Iex.Mux0X.exprX = ML_(flatten_expression)(sb, expr->Iex.Mux0X.exprX);
			break;
		case Iex_CCall:
			{
				int i;
				for(i=0; expr->Iex.CCall.args[i] != NULL; i++)
					expr->Iex.CCall.args[i] = ML_(flatten_expression)(sb, expr->Iex.CCall.args[i]);
				return expr;
			}
		default:
			VG_(tool_panic)("Unknown expression tag.");
	}
	IRTemp tmp = newIRTemp(sb->tyenv, typeOfIRExpr(sb->tyenv, expr));
	addStmtToIRSB(sb, IRStmt_WrTmp(tmp, expr));
	return IRExpr_RdTmp(tmp);
}

