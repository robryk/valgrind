#include "sr_runtime.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_threadstate.h"

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

IRDirty* ML_(helper_exit_phased)(void)
{
	return unsafeIRDirty_0_N(0, "exit_phased", &rt_exit_phased, mkIRExprVec_0());
}

static VG_REGPARM(1) int rt_init_phased(HWord values_needed)
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

IRDirty* ML_(helper_init_phased)(HWord values_needed, IRTemp phase)
{
	IRExpr* const_values_needed = mkIRExpr_HWord(values_needed);
	return unsafeIRDirty_1_N(phase, 1, "init_phased", &rt_init_phased, mkIRExprVec_1(const_values_needed));
}

static VG_REGPARM(1) void rt_set_phase(HWord phase)
{
	rt_my_state()->phase = phase;
}

IRDirty* ML_(helper_set_phase)(HWord phase)
{
	IRExpr* const_phase = mkIRExpr_HWord(phase);
	return unsafeIRDirty_0_N(1, "set_phase", &rt_set_phase, mkIRExprVec_1(const_phase));
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

accessor(U1, Bool);
accessor(U8, UChar);
accessor(U16, UShort);
accessor(U32, UInt);
accessor(U64, ULong);
accessor(F64, Double);
accessor(F64i, ULong);

#undef accessor

typedef struct _Accessor {
	IRType type;
	char* get_name;
	void* get;
	char* put_name;
	void* put;
} Accessor;
Accessor accessors[] = {
	{ Ity_I1, "get_U1", &rt_get_U1, "put_U1", &rt_put_U1 },
	{ Ity_I8, "get_U8", &rt_get_U8, "put_U8", &rt_put_U8 },
	{ Ity_I16, "get_U16", &rt_get_U16, "put_U16", &rt_put_U16 },
	{ Ity_I32, "get_U32", &rt_get_U32, "put_U32", &rt_put_U32 },
	{ Ity_I64, "get_U64", &rt_get_U64,"put_U64",  &rt_put_U64 },
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

IRDirty* ML_(helper_retrieve_temp)(IRTemp canonical, IRType type, IRTemp destination)
{
	Accessor* accessor = get_accessor(type);
	IRExpr* const_canonical = mkIRExpr_HWord(canonical);
	return unsafeIRDirty_1_N(destination, 1, accessor->get_name, accessor->get, mkIRExprVec_1(const_canonical));
}

IRDirty* ML_(helper_store_temp)(IRTemp canonical, IRType type)
{
	Accessor* accessor = get_accessor(type);
	IRExpr* const_canonical = mkIRExpr_HWord(canonical);
	IRExpr* temp_source = IRExpr_RdTmp(canonical);
	return unsafeIRDirty_0_N(2, accessor->put_name, accessor->put, mkIRExprVec_2(const_canonical, temp_source));
}
