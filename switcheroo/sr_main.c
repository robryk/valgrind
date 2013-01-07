/*----------------------------------------------------------------------*/
/*--- Thread Switcheroo: bruteforce concurrency bug finder sr_main.c ---*/
/*----------------------------------------------------------------------*/

#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_tooliface.h"
#include "sr_instrument.h"

static void sr_post_clo_init(void)
{
	VG_(clo_vex_control).iropt_register_updates = VexRegUpdAllregsAtEachInsn;
}

static Bool is_memory_access(IRStmt* st)
{
	switch (st->tag) {
		case Ist_Store:
		case Ist_CAS:
		case Ist_LLSC:
			return True;
		case Ist_WrTmp:
			if (st->Ist.WrTmp.data->tag == Iex_Load)
				return True;
			break;
		case Ist_Dirty:
			if (st->Ist.Dirty.details->mFx != Ifx_None)
				return True;
			break;
		default:
			break;
	}
	return False;
}

static
IRSB* sr_instrument ( VgCallbackClosure* closure,
                      IRSB* sbIn,
                      VexGuestLayout* layout, 
                      VexGuestExtents* vge,
                      VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
	//ppIRSB(sbIn);
	IRSB* sbOut = deepCopyIRSBExceptStmts(sbIn);
	int mem_access_first = 0;
	tl_assert(sbIn->stmts[0]->tag == Ist_IMark);
	int i = 1;
	for(i = 1; i < sbIn->stmts_used; i++) {
		if (is_memory_access(sbIn->stmts[i]))
			mem_access_first++;
		if (sbIn->stmts[i]->tag == Ist_IMark)
			break;
	}
	if (mem_access_first == 0) {
		i = 0;
		while (i < sbIn->stmts_used) {
			tl_assert(sbIn->stmts[i]->tag == Ist_IMark);
			Addr64 self_address = sbIn->stmts[i]->Ist.IMark.addr + sbIn->stmts[i]->Ist.IMark.delta;
			int j;
			Bool mem_access = False;
			for(j = i + 1; j < sbIn->stmts_used; j++) {
				if (is_memory_access(sbIn->stmts[j]))
					mem_access = True;
				if (sbIn->stmts[j]->tag == Ist_IMark)
					break;
			}
			if (mem_access) {
				tl_assert(i > 0);
				// We stop instrumenting short of an instruction with memory access
				sbOut->next = mkIRExpr_HWord(self_address);
				sbOut->jumpkind = Ijk_Boring;
				//VG_(printf)("Stopped short of mem access.\n");
				//ppIRSB(sbOut);
				return sbOut;
			}
			int k;
			for(k = i; k < j; k++)
				addStmtToIRSB(sbOut, sbIn->stmts[k]);
			i = j;
		}
		//VG_(printf)("No memory accesses in input SB.\n");
		//ppIRSB(sbOut);
		return sbOut;
	} else {
		Addr64 self_address = sbIn->stmts[0]->Ist.IMark.addr + sbIn->stmts[0]->Ist.IMark.delta;
		// Our first instruction has a memory access: split it into phases
		IRJumpKind jumpkind;
		IRExpr* next_instr;
		if (i == sbIn->stmts_used) {
			//VG_(printf)("aaa\n");
			next_instr = sbIn->next;
			jumpkind = sbIn->jumpkind;
		} else {
			next_instr = mkIRExpr_HWord(sbIn->stmts[i]->Ist.IMark.addr + sbIn->stmts[i]->Ist.IMark.delta);
			jumpkind = Ijk_Boring;
		}
		if (mem_access_first == 1 && jumpkind == Ijk_Boring) {
			int k;
			for(k = 0; k < i; k++)
				addStmtToIRSB(sbOut, sbIn->stmts[k]);
			sbOut->next = next_instr;
			sbOut->jumpkind = Ijk_Boring; // Yield
			//VG_(printf)("Finishing early after a single-mem-access instruction.\n");
			//ppIRSB(sbOut);
			return sbOut;
		}
		// We either have >=2 mem accesses or we must do a nonboring jump at the end, so we need phases.
		// This instruction will be the last we instrument from this BB.
		SRState state = ML_(instrument_start)(sbOut, self_address);
		int k;
		Bool was_mem = False;
		for(k = 1; k < i; k++) {
			if (is_memory_access(sbIn->stmts[k])) {
				if (was_mem)
					ML_(instrument_next_phase)(&state, Ijk_Boring); // Yield
				was_mem = True;
			}
			ML_(instrument_statement)(&state, sbIn->stmts[k]);
		}
		if (jumpkind == Ijk_Boring) {
			sbOut->jumpkind = Ijk_Boring; // Yield
			sbOut->next = ML_(instrument_expression)(&state, next_instr);
		} else {
			// We need to yield after every mem access, so we need to do so at the very end.
			// A Boring jump we can transform into a Yield jump, for the rest we just add a
			// mostly empty phase.
			ML_(instrument_next_phase)(&state, Ijk_Boring);
			sbOut->jumpkind = jumpkind;
			sbOut->next = ML_(instrument_expression)(&state, next_instr);
		}
		ML_(instrument_phased_exit)(&state, NULL);
		//VG_(printf)("Finishing early after emitting a phased instruction.\n");
		//ppIRSB(sbOut);
		return sbOut;
	}
}

static void sr_fini(Int exitcode)
{
}

static void sr_pre_clo_init(void)
{
   VG_(details_name)            ("Thread switcheroo");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("obsessive thread switcher (on every memory access)");
   VG_(details_copyright_author)(
      "Copyright (C) 2002-2012, and GNU GPL'd, by Nicholas Nethercote.");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);

   VG_(details_avg_translation_sizeB) ( 275 );

   VG_(basic_tool_funcs)        (sr_post_clo_init,
                                 sr_instrument,
                                 sr_fini);

   /* No needs, no core events to track */
}

VG_DETERMINE_INTERFACE_VERSION(sr_pre_clo_init)

/*----------------------------------------------------------------------*/
/*--- end                                                            ---*/
/*----------------------------------------------------------------------*/
