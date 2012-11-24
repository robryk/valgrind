/*----------------------------------------------------------------------*/
/*--- Thread Switcheroo: bruteforce concurrency bug finder sr_main.c ---*/
/*----------------------------------------------------------------------*/

#include "sr_instrument.h"

#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_tooliface.h"
#include "sr_flatten.h"
#include "sr_runtime.h"

typedef struct _SRState {
	IRSB* sb;

	IRExpr* phase;
	int current_phase;

	IRExpr* bool_current_phase;
	IRExpr* one_current_phase;

	int temps_count;
	IRExpr** temps_mapping;
	Bool* temps_written;
} SRState;

static char rt_dummy_location[32];

static void start_phase(SRState* state)
{
	int i;
	for(i=0;i<state->temps_count;i++) {
		state->temps_written[i] = False;
		state->temps_mapping[i] = NULL;
	}
	// TODO: decide on type of state
	state->bool_current_phase = ML_(flatten_expression)(state->sb, IRExpr_Binop(Iop_CmpEQ8, state->phase, IRExpr_Const(IRConst_U8(state->current_phase))));
	state->one_current_phase = ML_(flatten_expression)(state->sb, IRExpr_Unop(Iop_1Uto8, state->bool_current_phase));
}

static void next_phase(SRState* state)
{
	// TODO(robryk): Flush only temps that will be used in the future (ie. ones that get used in a phase in whoch they weren't created).
	int i;
	for(i=0;i<state->temps_count;i++)
		if (state->temps_written[i])
			ML_(helper_store_temp)(state->sb, i, state->bool_current_phase);
	ML_(helper_set_phase)(state->sb, state->current_phase+1, state->bool_current_phase);
	state->current_phase++;
	// Emit a jump to self, so that we do jump between phases
	start_phase(state);
}

static IRExpr* instrument_temp(SRState* state, IRTemp orig_temp)
{
	tl_assert(0 <= orig_temp && orig_temp < state->temps_count);
	if (state->temps_mapping[orig_temp] == NULL) {
		IRType type = typeOfIRTemp(state->sb->tyenv, orig_temp);
		// We cannot and do not guard the following; it has no side-effects so does no harm in other phases.
		state->temps_mapping[orig_temp] = ML_(helper_retrieve_temp)(state->sb, orig_temp, type);
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
	ML_(helper_exit_phased)(state->sb, guard);
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
		{
			IRExpr* orig_addr = instrument_expression(state, expr->Iex.Load.addr);
			// TODO: This should be GWord, but for now we support only HWord==GWord
			IRExpr* addr = IRExpr_Mux0X(state->one_current_phase, mkIRExpr_HWord(&rt_dummy_location), orig_addr);
			expr->Iex.Load.addr = ML_(flatten_expression)(state->sb, addr);
			return expr;
		}
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
			stmt->Ist.Put.data = ML_(flatten_expression)(state->sb, IRExpr_Mux0X(state->one_current_phase, IRExpr_Get(stmt->Ist.Put.offset, typeOfIRExpr(state->sb->tyenv, stmt->Ist.Put.data)), stmt->Ist.Put.data));
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_PutI:
			stmt->Ist.PutI.details->ix = instrument_expression(state, stmt->Ist.PutI.details->ix);
			stmt->Ist.PutI.details->data = instrument_expression(state, stmt->Ist.PutI.details->data);
			stmt->Ist.PutI.details->data = ML_(flatten_expression)(state->sb, IRExpr_Mux0X(state->one_current_phase, IRExpr_GetI(stmt->Ist.PutI.details->descr, stmt->Ist.PutI.details->ix, stmt->Ist.PutI.details->bias), stmt->Ist.PutI.details->data));
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_WrTmp:
			mark_temp_write(state, stmt->Ist.WrTmp.tmp);
			stmt->Ist.WrTmp.data = instrument_expression(state, stmt->Ist.WrTmp.data);
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_Store:
			stmt->Ist.Store.addr = instrument_expression(state, stmt->Ist.Store.addr);
			stmt->Ist.Store.addr = ML_(flatten_expression)(state->sb, IRExpr_Mux0X(state->one_current_phase, mkIRExpr_HWord(&rt_dummy_location), stmt->Ist.Store.addr));
			stmt->Ist.Store.data = instrument_expression(state, stmt->Ist.Store.data);
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_CAS:
			stmt->Ist.CAS.details->addr = instrument_expression(state, stmt->Ist.CAS.details->addr);
			stmt->Ist.CAS.details->addr = ML_(flatten_expression)(state->sb, IRExpr_Mux0X(state->one_current_phase, mkIRExpr_HWord(&rt_dummy_location), stmt->Ist.CAS.details->addr));
			stmt->Ist.CAS.details->expdLo = instrument_expression(state, stmt->Ist.CAS.details->expdLo);
			if (stmt->Ist.CAS.details->expdHi != NULL)
				stmt->Ist.CAS.details->expdHi = instrument_expression(state, stmt->Ist.CAS.details->expdHi);
			stmt->Ist.CAS.details->dataLo = instrument_expression(state, stmt->Ist.CAS.details->dataLo);
			if (stmt->Ist.CAS.details->dataHi != NULL)
				stmt->Ist.CAS.details->dataHi = instrument_expression(state, stmt->Ist.CAS.details->dataHi);
			mark_temp_write(state, stmt->Ist.CAS.details->oldLo);
			if (stmt->Ist.CAS.details->oldHi != IRTemp_INVALID)
				mark_temp_write(state, stmt->Ist.CAS.details->oldHi);
			addStmtToIRSB(state->sb, stmt);
			break;
		case Ist_LLSC:
			VG_(tool_panic)("LLSC is unimplemented");
		case Ist_Dirty:
			if (stmt->Ist.Dirty.details->tmp == IRTemp_INVALID) {
				IRExpr* orig_guard = IRExpr_Unop(Iop_1Uto8, instrument_expression(state, stmt->Ist.Dirty.details->guard));
				IRExpr* new_guard = IRExpr_Binop(Iop_And8, orig_guard, state->one_current_phase);
				stmt->Ist.Dirty.details->guard = ML_(flatten_expression)(state->sb, IRExpr_Unop(Iop_32to1, IRExpr_Unop(Iop_8Uto32, new_guard)));
//				stmt->Ist.Dirty.details->guard = ML_(flatten_expression)(state->sb, IRExpr_Mux0X(state->one_current_phase, IRExpr_Const(IRConst_U1(False)), stmt->Ist.Dirty.details->guard));
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
		{
			tl_assert(typeOfIRExpr(state->sb->tyenv, stmt->Ist.Exit.guard) == Ity_I1);
			IRExpr* orig_guard = IRExpr_Unop(Iop_1Uto8, instrument_expression(state, stmt->Ist.Exit.guard));
			IRExpr* new_guard = IRExpr_Binop(Iop_And8, orig_guard, state->one_current_phase);
			stmt->Ist.Exit.guard = ML_(flatten_expression)(state->sb, IRExpr_Unop(Iop_32to1, IRExpr_Unop(Iop_8Uto32, new_guard)));
			instrument_phased_exit(state, stmt->Ist.Exit.guard);
			addStmtToIRSB(state->sb, stmt);
			break;
		}
		default:
			VG_(tool_panic)("Unknown statement tag.");
	}
}

IRSB* ML_(instrument)(IRSB* sbIn)
{
	SRState state;
	state.sb = deepCopyIRSBExceptStmts(sbIn);
	state.temps_count = sbIn->tyenv->types_used;
	state.temps_mapping = VG_(calloc)("SRState_temps_mapping", state.temps_count, sizeof(*state.temps_mapping));
	state.temps_written = VG_(calloc)("SRState_temps_written", state.temps_count, sizeof(*state.temps_written));

	state.phase = ML_(helper_init_phased)(state.sb, state.temps_count);

	int i = 0;
	if (sbIn->stmts_used > 0 && sbIn->stmts[0]->tag == Ist_IMark)
		i++;
	state.current_phase = 0;
	start_phase(&state);
	for(; i < sbIn->stmts_used; i++) {
		IRStmt* stmt = sbIn->stmts[i];
		if (stmt->tag == Ist_IMark) {
			// Phase boundary
			next_phase(&state);
			continue;
		}
		instrument_statement(&state, stmt);		
	}
	instrument_phased_exit(&state, NULL);
	return state.sb;
}
