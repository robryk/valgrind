#ifndef __GUARD_SR_RUNTIME_H
#define __GUARD_SR_RUNTIME_H

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"

typedef union _Value {
	Bool   U1;
	UChar  U8;
	UShort U16;
	UInt   U32;
	ULong  U64;
	Double F64;
	ULong  F64i;
	struct {
		ULong hi;
		ULong lo;
	} V128;
	// TODO(robryk): Support for Ity_V128 and Ity_I128
} Value;

void ML_(helper_exit_phased)(IRSB* sb, IRExpr* guard);
IRExpr* ML_(helper_init_phased)(IRSB* sb, HWord values_needed);
void ML_(helper_set_phase)(IRSB* sb, HWord phase, IRExpr* guard);

IRExpr* ML_(helper_retrieve_temp)(IRSB* sb, IRTemp canonical, IRType type);
void ML_(helper_store_temp)(IRSB* sb, IRTemp canonical, IRExpr* guard);

#endif  // __GUARD_SR_RUNTIME_H
