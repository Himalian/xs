/* purity.h -- static purity inference for fn_decl and lambda nodes.
 *
 * A function is "pure" when it is deterministic with no observable
 * side effects given the same arguments and immutable free vars.
 * The analyzer walks every fn / lambda in the program, builds a call
 * graph from named callees, and propagates impurity to a fixpoint.
 * The result lands on the AST as `inferred_pure` on each fn_decl /
 * lambda; the runtime copies that bit onto XSFunc / XSProto so a
 * closure value carries it for `__pure?(f)` and decorator gating.
 */
#ifndef PURITY_H
#define PURITY_H

#include "core/ast.h"

/* Walk the program and stamp inferred_pure onto every fn_decl and
   lambda node. Safe to call before / without sema; never reports
   diagnostics. Idempotent. */
void purity_analyze(Node *program);

/* Predicate over a stdlib / builtin name. Pure if the name names a
   value-only function with no side effects (math, json, string ops,
   ...); impure otherwise (print, fs.*, time.now, random.*, ...). */
int  purity_builtin_is_pure(const char *name);

#endif
