#ifndef XS_ERROR_H
#define XS_ERROR_H

#include "core/value.h"
#include "core/ast.h"

/* error values are XS_MAPs with "kind", "message", optional "cause" */
Value *xs_error_new(const char *kind, const char *message, Value *cause);
Value *xs_error_from_str(const char *message);
const char *xs_error_kind(Value *err);
const char *xs_error_message(Value *err);
Value      *xs_error_cause(Value *err);

void xs_runtime_error(Span span, const char *label, const char *hint,
                      const char *fmt, ...);
void xs_error_set_source(const char *source);

/* Number of xs_runtime_error calls made in this process. The interpreter
   sometimes reports an error and continues with a null sentinel rather than
   aborting; main checks this after run so that reported runtime errors
   surface as a non-zero process exit. */
extern int g_xs_runtime_error_count;

/* Number of live try frames across both backends. Bumped by the
   interpreter on NODE_TRY entry and by the VM on OP_TRY_BEGIN. When > 0
   xs_runtime_error knows the throw it raises will be caught and skips
   inline rendering + the exit-code bump. */
extern int g_xs_in_try;

/* Pending throwable installed by xs_runtime_error when called from
   contexts that do not own an interp (currently the VM). The VM's
   dispatch loop checks this at the top of each iteration and unwinds
   like OP_THROW would. The interpreter ignores it (it has its own
   cf.signal/cf.value path). Owned; cleared when consumed. */
extern Value *g_xs_pending_throw;

#endif
