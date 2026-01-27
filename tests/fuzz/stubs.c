/* Stubs for symbols that value.c / env.c / parser.c / render.c reach
   for through cross-module function pointers but which live in parts
   of the runtime we intentionally don't link into the parser fuzzer
   (VM bytecode loader, full interpreter, plugin DSL, CLI argv state).
   Keeping these out of the fuzz binary keeps the target small enough
   for libFuzzer to explore the parser, not the whole runtime. */

#include "core/ast.h"
#include "core/value.h"

int g_no_color = 0;

void proto_free(void *p) { (void)p; }

Value *interp_eval(void *i, Node *n) { (void)i; (void)n; return NULL; }

Node *parse_plugin_decl(void *p) { (void)p; return NULL; }
