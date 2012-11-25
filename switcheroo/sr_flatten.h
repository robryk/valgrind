#ifndef __GUARD_SR_FLATTEN_H
#define __GUARD_SR_FLATTEN_H

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"

IRExpr* ML_(flatten_expression)(IRSB* sb, IRExpr* expr);

#endif  // __GUARD_SR_FLATTEN_H

