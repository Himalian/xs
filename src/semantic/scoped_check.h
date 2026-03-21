/* scoped_check.h -- escape analysis for @scoped bindings.
 *
 * @scoped marks a binding whose value must not outlive the lexical
 * block that introduced it: it can't be returned, can't be stored
 * in an outer container, can't be captured by a closure that
 * survives the scope. The runtime can elide GC tracking for these
 * since their lifetime is statically known.
 *
 * This pass runs after resolve / typecheck. It walks the AST,
 * tracking which bindings carry @scoped, and emits diagnostics for
 * the well-defined escape patterns. Conservative: anything we can't
 * prove safe is rejected.
 */
#ifndef SCOPED_CHECK_H
#define SCOPED_CHECK_H

#include "core/ast.h"
#include "semantic/sema.h"

void scoped_check_program(SemaCtx *ctx, Node *program);

#endif
