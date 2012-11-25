#include "sr_runtime.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_threadstate.h"
#include "sr_flatten.h"

typedef struct _SRThreadState {
	int phase;
	int values_size;
	int values_used;
	Value* values;
} SRThreadState;

static SRThreadState threads[VG_N_THREADS];

static SRThreadState* rt_my_state(void)
{
	return &threads[VG_(get_running_tid)()];
}

static void rt_exit_phased(void)
{
	rt_my_state()->phase = 0;
}

void ML_(helper_exit_phased)(IRSB* sb, IRExpr* guard)
{
	IRDirty* call = unsafeIRDirty_0_N(0, "exit_phased", &rt_exit_phased, mkIRExprVec_0());
	if (guard != NULL)
		call->guard = guard;
	addStmtToIRSB(sb, IRStmt_Dirty(call));
}

static VG_REGPARM(1) UChar rt_init_phased(HWord values_needed)
{
	SRThreadState* state = rt_my_state();
	if (values_needed > state->values_size) {
		if (state->values != NULL)
			VG_(free)(state->values);
		state->values = VG_(calloc)("switcheroo values", values_needed, sizeof(*state->values));
		state->values_size = values_needed;
	}
	state->values_used = values_needed;
	return state->phase;
}

IRExpr* ML_(helper_init_phased)(IRSB* sb, HWord values_needed)
{
	IRExpr* const_values_needed = mkIRExpr_HWord(values_needed);
	IRTemp phase = newIRTemp(sb->tyenv, Ity_I64);
	IRDirty* call = unsafeIRDirty_1_N(phase, 1, "init_phased", &rt_init_phased, mkIRExprVec_1(const_values_needed));
	addStmtToIRSB(sb, IRStmt_Dirty(call));
	return ML_(flatten_expression)(sb, IRExpr_Unop(Iop_64to8, IRExpr_RdTmp(phase)));
}

static VG_REGPARM(1) void rt_set_phase(HWord phase)
{
	rt_my_state()->phase = phase;
}

void ML_(helper_set_phase)(IRSB* sb, HWord phase, IRExpr* guard)
{
	IRExpr* const_phase = mkIRExpr_HWord(phase);
	IRDirty* call = unsafeIRDirty_0_N(1, "set_phase", &rt_set_phase, mkIRExprVec_1(const_phase));
	if (guard)
		call->guard = guard;
	addStmtToIRSB(sb, IRStmt_Dirty(call));
}

#define accessor(Type, CType) \
static VG_REGPARM(1) CType rt_get_##Type(HWord idx) \
{ \
	SRThreadState* state = rt_my_state(); \
	tl_assert(0 <= idx && idx < state->values_used); \
	return state->values[idx].Type; \
} \
\
static VG_REGPARM(2) void rt_put_##Type(HWord idx, CType value) \
{ \
	SRThreadState* state = rt_my_state(); \
	tl_assert(0 <= idx && idx < state->values_used); \
	state->values[idx].Type = value; \
}

accessor(U1, HWord);
accessor(U8, HWord);
accessor(U16, HWord);
accessor(U32, HWord);
accessor(U64, HWord);
//accessor(F64, HWord);
//accessor(F64i, HWord);

#undef accessor

typedef struct _Accessor {
	IRType type;
	char* get_name;
	void* get;
	IROp get_cast;
	char* put_name;
	void* put;
	IROp put_cast;
} Accessor;
Accessor accessors[] = {
	{ Ity_I1, "get_U1", &rt_get_U1, Iop_64to1, "put_U1", &rt_put_U1, Iop_1Uto64 },
	{ Ity_I8, "get_U8", &rt_get_U8, Iop_64to8, "put_U8", &rt_put_U8, Iop_8Uto64 },
	{ Ity_I16, "get_U16", &rt_get_U16, Iop_64to16, "put_U16", &rt_put_U16, Iop_16Uto64 },
	{ Ity_I32, "get_U32", &rt_get_U32, Iop_64to32, "put_U32", &rt_put_U32, Iop_32Uto64 },
	{ Ity_I64, "get_U64", &rt_get_U64, Iop_INVALID, "put_U64",  &rt_put_U64, Iop_INVALID },
	//{ Ity_F64, "get_F64", &rt_get_F64, "put_F64", &rt_put_F64 },
	//{ Ity_F64i, "get_F64i", &rt_get_F64i, "put_F64i", &rt_put_F64i },
};

static Accessor* get_accessor(IRType type)
{
	int i;
	for(i=0;i<sizeof(accessors)/sizeof(Accessor);i++) {
		if (accessors[i].type == type)
			return &accessors[i];
	}
	VG_(tool_panic)("No accessor for type.");
}

static ULong rt_retrieve_128_lo(HWord idx)
{
	SRThreadState* state = rt_my_state();
	return state->values[idx].V128.lo;
}

static ULong rt_retrieve_128_hi(HWord idx)
{
	SRThreadState* state = rt_my_state();
	return state->values[idx].V128.hi;
}

static IRExpr* helper_retrieve_128(IRSB* sb, IRTemp canonical, IROp hilo)
{
	IRTemp lo_temp = newIRTemp(sb->tyenv, Ity_I64);
	IRDirty* lo_call = unsafeIRDirty_1_N(lo_temp, 1, "get_128_lo", &rt_retrieve_128_lo, mkIRExprVec_1(mkIRExpr_HWord(canonical)));
	addStmtToIRSB(sb, IRStmt_Dirty(lo_call));
	IRTemp hi_temp = newIRTemp(sb->tyenv, Ity_I64);
	IRDirty* hi_call = unsafeIRDirty_1_N(hi_temp, 1, "get_128_hi", &rt_retrieve_128_hi, mkIRExprVec_1(mkIRExpr_HWord(canonical)));
	addStmtToIRSB(sb, IRStmt_Dirty(hi_call));
	return ML_(flatten_expression)(sb, IRExpr_Binop(hilo, IRExpr_RdTmp(hi_temp), IRExpr_RdTmp(lo_temp)));	
}

static void rt_store_V128(HWord idx, ULong lo, ULong hi)
{
	SRThreadState* state = rt_my_state();
	state->values[idx].V128.lo = lo;
	state->values[idx].V128.hi = hi;
}

static void helper_store_128(IRSB* sb, IRTemp canonical, IRExpr* guard, IROp get_lo, IROp get_hi)
{
	IRExpr* const_canonical = mkIRExpr_HWord(canonical);
	IRExpr* lo = ML_(flatten_expression)(sb, IRExpr_Unop(get_lo, IRExpr_RdTmp(canonical)));
	IRExpr* hi = ML_(flatten_expression)(sb, IRExpr_Unop(get_hi, IRExpr_RdTmp(canonical)));
	IRDirty* call = unsafeIRDirty_0_N(3, "put_V128", &rt_store_V128,
		mkIRExprVec_3(const_canonical, lo, hi));
	if (guard)
		call->guard = guard;
	addStmtToIRSB(sb, IRStmt_Dirty(call));
}

IRExpr* ML_(helper_retrieve_temp)(IRSB* sb, IRTemp canonical, IRType type)
{
	if (type == Ity_V128)
		return helper_retrieve_128(sb, canonical, Iop_64HLtoV128);
	if (type == Ity_I128)
		return helper_retrieve_128(sb, canonical, Iop_64HLto128);
	Accessor* accessor = get_accessor(type);
	IRExpr* const_canonical = mkIRExpr_HWord(canonical);
	IRTemp temp = newIRTemp(sb->tyenv, Ity_I64);
	IRDirty* call = unsafeIRDirty_1_N(temp, 1, accessor->get_name, accessor->get, mkIRExprVec_1(const_canonical));
	addStmtToIRSB(sb, IRStmt_Dirty(call));
	IRExpr* result = IRExpr_RdTmp(temp);
	if (accessor->get_cast != Iop_INVALID)
		result = ML_(flatten_expression)(sb, IRExpr_Unop(accessor->get_cast, IRExpr_RdTmp(temp)));
	return result;
}

void ML_(helper_store_temp)(IRSB* sb, IRTemp canonical, IRExpr* guard)
{
	IRType type = typeOfIRTemp(sb->tyenv, canonical);
	if (type == Ity_V128)
		return helper_store_128(sb, canonical, guard, Iop_V128to64, Iop_V128HIto64);
	if (type == Ity_I128)
		return helper_store_128(sb, canonical, guard, Iop_128to64, Iop_128HIto64);
	Accessor* accessor = get_accessor(type);
	IRExpr* const_canonical = mkIRExpr_HWord(canonical);
	IRExpr* source = IRExpr_RdTmp(canonical);
	if (accessor->put_cast != Iop_INVALID)
		source = ML_(flatten_expression)(sb, IRExpr_Unop(accessor->put_cast, source));
	IRDirty* call = unsafeIRDirty_0_N(2, accessor->put_name, accessor->put, mkIRExprVec_2(const_canonical, source));
	if (guard)
		call->guard = guard;
	addStmtToIRSB(sb, IRStmt_Dirty(call));
}
