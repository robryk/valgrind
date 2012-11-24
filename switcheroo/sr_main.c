/*----------------------------------------------------------------------*/
/*--- Thread Switcheroo: bruteforce concurrency bug finder sr_main.c ---*/
/*----------------------------------------------------------------------*/

#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_tooliface.h"
#include "sr_instrument.h"

static void sr_post_clo_init(void)
{
}

static
IRSB* sr_instrument ( VgCallbackClosure* closure,
                      IRSB* sbIn,
                      VexGuestLayout* layout, 
                      VexGuestExtents* vge,
                      VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
	IRSB* sbOut = deepCopyIRSBExceptStmts(sbIn);
	int i;
	for(i=0; i<sbIn->stmts_used; i++) {
		IRStmt* st = sbIn->stmts[i];
		Bool isMemoryAccess = False;
		switch (st->tag) {
			case Ist_Store:
			case Ist_CAS:
			case Ist_LLSC:
				isMemoryAccess = True;
				break;
			case Ist_WrTmp:
				if (st->Ist.WrTmp.data->tag == Iex_Load)
					isMemoryAccess = True;
				break;
			case Ist_Dirty:
				if (st->Ist.Dirty.details->mFx != Ifx_None)
					isMemoryAccess = True;
				break;
			default:
				break;
		}
		addStmtToIRSB(sbOut, st);
	}
    IRSB* sbFinal = ML_(instrument)(sbOut);
    ppIRSB(sbFinal);
    return sbFinal;
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
