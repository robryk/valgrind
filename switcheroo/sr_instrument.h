#ifndef __GUARD_SR_INSTRUMENT_H
#define __GUARD_SR_INSTRUMENT_H

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"

typedef struct _SRState {
	IRConst *self_address;

	IRSB* sb;

	IRExpr* phase;
	int current_phase;

	IRExpr* bool_current_phase;
	IRExpr* one_current_phase;

	int temps_count;
	IRExpr** temps_mapping;
	Bool* temps_written;
} SRState;

void ML_(instrument_next_phase)(SRState* state, IRJumpKind jk);
void ML_(instrument_phased_exit)(SRState* state, IRExpr* guard);
IRExpr* ML_(instrument_expression)(SRState* state, IRExpr* expr);
void ML_(instrument_statement)(SRState* state, IRStmt* stmt);
SRState ML_(instrument_start)(IRSB* sb, Addr64 self_address);


#endif  // __GUARD_SR_INSTRUMENT_H
