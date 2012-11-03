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
	// TODO(robryk): Support for Ity_V128 and Ity_I128
} Value;

IRDirty* ML_(helper_exit_phased)(void);
IRDirty* ML_(helper_init_phased)(HWord values_needed, IRTemp phase);
IRDirty* ML_(helper_set_phase)(HWord phase);

IRDirty* ML_(helper_retrieve_temp)(IRTemp canonical, IRType type, IRTemp destination);
IRDirty* ML_(helper_store_temp)(IRTemp canonical, IRType type);

#endif  // __GUARD_SR_RUNTIME_H
