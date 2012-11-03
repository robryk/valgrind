/*----------------------------------------------------------------------*/
/*--- Thread Switcheroo: bruteforce concurrency bug finder sr_main.c ---*/
/*----------------------------------------------------------------------*/

#include "sr_runtime.h"

#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_tooliface.h"

typedef struct _SRState {
	IRSB* sb;

	IRExpr* phase;
	int current_phase;

	IRExpr* bool_current_phase;
	IRExpr* zero_current_phase;

	int temps_count;
	IRExpr** temps_mapping;
	Bool* temps_written;
} SRState;

static IRExpr* wrap_expression(IRSB* sb, IRExpr* expr)
{
	IRTemp tmp = newIRTemp(sb->tyenv, typeOfIRExpr(sb->tyenv, expr));
	addStmtToIRSB(sb, IRStmt_WrTmp(tmp, expr));
	return IRExpr_RdTmp(tmp);
}

static void start_phase(SRState* state)
{
	int i;
	for(i=0;i<state->sb->tyenv->types_used;i++) {
		state->temps_written[i] = False;
		state->temps_mapping[i] = NULL;
	}
	// TODO: decide on type of state
	state->bool_current_phase = wrap_expression(state->sb, IRExpr_Binop(Iop_CmpEQ32, state->phase, IRExpr_Const(IRConst_U32(state->current_phase))));
	// state->zero_current_phase
}

static void next_phase(SRState* state)
{
	// TODO(robryk): Flush only temps that will be used in the future (ie. ones that get used in a phase in whoch they weren't created).
	int i;
	for(i=0;i<state->temps_count;i++)
		if (state->temps_written[i]) {
			IRDirty* store_action = ML_(helper_store_temp)(i, typeOfIRTemp(state->sb->tyenv, i));
			store_action->guard = state->bool_current_phase;
			addStmtToIRSB(state->sb, IRStmt_Dirty(store_action));
		}
	IRDirty* advance_phase_action = ML_(helper_set_phase)(state->current_phase+1);
	advance_phase_action->guard = state->bool_current_phase;
	addStmtToIRSB(state->sb, IRStmt_Dirty(advance_phase_action));
	state->current_phase++;
	// Emit a jump to self, so that we do jump between phases
	start_phase(state);
}

static IRExpr* instrument_temp(SRState* state, IRTemp orig_temp)
{
	tl_assert(orig_temp < state->temps_count);
	if (state->temps_mapping[orig_temp] == NULL) {
		IRType type = typeOfIRTemp(state->sb->tyenv, orig_temp);
		IRTemp current_temp = newIRTemp(state->sb->tyenv, type);
		IRDirty* retrieve_action = ML_(helper_retrieve_temp)(orig_temp, type, current_temp);
		// We cannot and do not guard the following; it has no side-effects so does no harm in other phases.
		addStmtToIRSB(state->sb, IRStmt_Dirty(retrieve_action));
		state->temps_mapping[orig_temp] = IRExpr_RdTmp(current_temp);
	}
	return state->temps_mapping[orig_temp];
}

static void mark_temp_write(SRState* state, IRTemp temp)
{
	tl_assert(0 <= temp && temp < state->temps_count);
	tl_assert(state->temps_mapping[temp] == NULL);
	state->temps_written[temp] = True;
	state->temps_mapping[temp] = IRExpr_RdTmp(temp);
}

static void instrument_phased_exit(SRState* state, IRExpr* guard)
{
	IRDirty* dirty = ML_(helper_exit_phased());
	if (guard != NULL)
		dirty->guard = guard;
	addStmtToIRSB(state->sb, IRStmt_Dirty(dirty));
}

static IRExpr* instrument_expression(SRState* state, IRExpr* expr)
{
	switch (expr->tag) {
		case Iex_Get:
			return expr;
		case Iex_GetI:
			expr->Iex.GetI.ix = instrument_expression(state, expr->Iex.GetI.ix);
			return expr;
		case Iex_RdTmp:
			return instrument_temp(state, expr->Iex.RdTmp.tmp);
		case Iex_Qop:
			expr->Iex.Qop.details->arg1 = instrument_expression(state, expr->Iex.Qop.details->arg1);
			expr->Iex.Qop.details->arg2 = instrument_expression(state, expr->Iex.Qop.details->arg2);
			expr->Iex.Qop.details->arg3 = instrument_expression(state, expr->Iex.Qop.details->arg3);
			expr->Iex.Qop.details->arg4 = instrument_expression(state, expr->Iex.Qop.details->arg4);
			return expr;
		case Iex_Triop:
			expr->Iex.Triop.details->arg1 = instrument_expression(state, expr->Iex.Triop.details->arg1);
			expr->Iex.Triop.details->arg2 = instrument_expression(state, expr->Iex.Triop.details->arg2);
			expr->Iex.Triop.details->arg3 = instrument_expression(state, expr->Iex.Triop.details->arg3);
			return expr;
		case Iex_Binop:
			expr->Iex.Binop.arg1 = instrument_expression(state, expr->Iex.Binop.arg1);
			expr->Iex.Binop.arg2 = instrument_expression(state, expr->Iex.Binop.arg2);
			return expr;
		case Iex_Unop:
			expr->Iex.Unop.arg = instrument_expression(state, expr->Iex.Unop.arg);
			return expr;
		case Iex_Load:
			expr->Iex.Load.addr = instrument_expression(state, expr->Iex.Load.addr);
			return expr;
		case Iex_Const:
			return expr;
		case Iex_Mux0X:
			expr->Iex.Mux0X.cond = instrument_expression(state, expr->Iex.Mux0X.cond);
			expr->Iex.Mux0X.expr0 = instrument_expression(state, expr->Iex.Mux0X.expr0);
			expr->Iex.Mux0X.exprX = instrument_expression(state, expr->Iex.Mux0X.exprX);
			return expr;
		case Iex_CCall:
			{
				int i;
				for(i=0; expr->Iex.CCall.args[i] != NULL; i++)
					expr->Iex.CCall.args[i] = instrument_expression(state, expr->Iex.CCall.args[i]);
				return expr;
			}
		default:
			VG_(tool_panic)("Unknown expression tag.");
	}
}

static void instrument_statement(SRState* state, IRStmt* stmt)
{
	switch (stmt->tag) {
		case Ist_NoOp:
			break;
		case Ist_IMark:
			VG_(tool_panic)("Ist_IMark passed to instrument_statement");
		case Ist_AbiHint:
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_Put:
			stmt->Ist.Put.data = instrument_expression(state, stmt->Ist.Put.data);
			stmt->Ist.Put.data = wrap_expression(state->sb, IRExpr_Mux0X(state->zero_current_phase, stmt->Ist.Put.data, wrap_expression(state->sb, IRExpr_Get(stmt->Ist.Put.offset, typeOfIRExpr(state->sb->tyenv, stmt->Ist.Put.data)))));
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_PutI:
			stmt->Ist.PutI.details->ix = instrument_expression(state, stmt->Ist.PutI.details->ix);
			stmt->Ist.PutI.details->data = instrument_expression(state, stmt->Ist.PutI.details->data);
			stmt->Ist.PutI.details->data = wrap_expression(state->sb, IRExpr_Mux0X(state->zero_current_phase, stmt->Ist.PutI.details->data, IRExpr_GetI(stmt->Ist.PutI.details->descr, stmt->Ist.PutI.details->ix, stmt->Ist.PutI.details->bias)));
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_WrTmp:
			mark_temp_write(state, stmt->Ist.WrTmp.tmp);
			stmt->Ist.WrTmp.data = instrument_expression(state, stmt->Ist.WrTmp.data);
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_Store:
			stmt->Ist.Store.addr = instrument_expression(state, stmt->Ist.Store.addr);
			stmt->Ist.Store.data = instrument_expression(state, stmt->Ist.Store.data);
			stmt->Ist.Store.data = wrap_expression(state->sb, IRExpr_Mux0X(state->zero_current_phase, stmt->Ist.Store.data, wrap_expression(state->sb, IRExpr_Load(stmt->Ist.Store.end, typeOfIRExpr(state->sb->tyenv, stmt->Ist.Store.data), stmt->Ist.Store.addr))));
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_CAS:
		case Ist_LLSC:
			VG_(tool_panic)("CAS and LLSC are unimplemented");
		case Ist_Dirty:
			if (stmt->Ist.Dirty.details->tmp == IRTemp_INVALID) {
				stmt->Ist.Dirty.details->guard = instrument_expression(state, stmt->Ist.Dirty.details->guard);
				stmt->Ist.Dirty.details->guard = wrap_expression(state->sb, IRExpr_Mux0X(state->zero_current_phase, stmt->Ist.Dirty.details->guard, IRExpr_Const(IRConst_U1(False))));
				int i;
				for(i=0;stmt->Ist.Dirty.details->args[i]!=NULL;i++)
					stmt->Ist.Dirty.details->args[i] = instrument_expression(state, stmt->Ist.Dirty.details->args[i]);
			} else {
				// TODO(robryk): This is Wrong. The dirty helper will get called in phases in which it shouldn't be. There is currently no sensible way to do that -- I'd need
				// to wrap it so that conditionally it doesn't get executed and something gets returned.
				mark_temp_write(state, stmt->Ist.Dirty.details->tmp);
			}
		case Ist_MBE:
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_Exit:
			tl_assert(typeOfIRExpr(state->sb->tyenv, stmt->Ist.Exit.guard) == Ity_I1);
			stmt->Ist.Dirty.details->guard = instrument_expression(state, stmt->Ist.Dirty.details->guard);
			stmt->Ist.Dirty.details->guard = wrap_expression(state->sb, IRExpr_Mux0X(state->zero_current_phase, stmt->Ist.Dirty.details->guard, IRExpr_Const(IRConst_U1(False))));
			instrument_phased_exit(state, stmt->Ist.Dirty.details->guard);
			addStmtToIRSB(state->sb, stmt);
			break;
		default:
			VG_(tool_panic)("Unknown statement tag.");
	}
}

static IRSB* instrument(IRSB* sbIn)
{
	SRState state;
	state.sb = deepCopyIRSBExceptStmts(sbIn);
	state.temps_count = sbIn->tyenv->types_used;
	state.temps_mapping = VG_(calloc)("SRState_temps_mapping", state.temps_count, sizeof(*state.temps_mapping));
	state.temps_written = VG_(calloc)("SRState_temps_written", state.temps_count, sizeof(*state.temps_written));

	IRTemp phase_temp = newIRTemp(state.sb->tyenv, Ity_I32); // FIXME: Type
	addStmtToISRB(state.sb, IRStmt_Dirty(ML_(helper_init_phased)(phase_temp, state.temps_count)));
	state.phase = IRExpr_RdTmp(phase_temp);

	state.current_phase = 0;
	start_phase(&state);


}
