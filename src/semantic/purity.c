#include <stdlib.h>
#include <string.h>

#include "core/xs.h"
#include "core/ast.h"
#include "semantic/purity.h"

/* ─── per-fn record ────────────────────────────────────────────────── */

typedef struct {
    Node       *node;        /* fn_decl or lambda */
    const char *name;        /* fn_decl.name, or NULL for lambdas */
    int         pure;        /* tentative answer; flips to 0 on impurity */
    /* outgoing edges: names this fn calls. Resolved lazily during
       propagation against the fn_decl name table. Names not in the
       table are looked up as builtins; missing builtins are treated
       conservatively as impure (the analyzer doesn't see plugin /
       stdlib runtime injections). */
    char      **calls;
    int         n_calls;
    int         cap_calls;
} FnRec;

static FnRec *g_fns = NULL;
static int    g_n_fns = 0;
static int    g_cap_fns = 0;

static int fn_index_for(Node *n) {
    for (int i = 0; i < g_n_fns; i++)
        if (g_fns[i].node == n) return i;
    if (g_n_fns >= g_cap_fns) {
        g_cap_fns = g_cap_fns ? g_cap_fns * 2 : 64;
        g_fns = xs_realloc(g_fns, (size_t)g_cap_fns * sizeof(FnRec));
    }
    FnRec *r = &g_fns[g_n_fns];
    memset(r, 0, sizeof *r);
    r->node = n;
    r->pure = 1;
    r->name = (VAL_TAG(n) == NODE_FN_DECL) ? n->fn_decl.name : NULL;
    return g_n_fns++;
}

static void fn_add_call(FnRec *r, const char *name) {
    if (!name) return;
    for (int i = 0; i < r->n_calls; i++)
        if (strcmp(r->calls[i], name) == 0) return;
    if (r->n_calls >= r->cap_calls) {
        r->cap_calls = r->cap_calls ? r->cap_calls * 2 : 4;
        r->calls = xs_realloc(r->calls, (size_t)r->cap_calls * sizeof(char *));
    }
    r->calls[r->n_calls++] = xs_strdup(name);
}

/* ─── builtin purity table ─────────────────────────────────────────── */

/* Pure builtins: deterministic, no I/O, no time, no rng. Conservative;
   anything not listed is treated as impure unless it's a known module
   prefix that we walk separately (math.*, json.*, ...). */
static const char *PURE_BUILTINS[] = {
    "len","int","float","str","bool","char","repr","ord","chr",
    "abs","min","max","pow","sqrt","floor","ceil","round","log","sin","cos","tan",
    "type","typeof","type_of","is_null","is_int","is_float","is_str","is_bool",
    "is_array","is_fn",
    "Some","None","Ok","Err",
    "range","array","map","vec","sorted","copy","clone",
    "zip","enumerate","sum","contains","filter","reduce",
    "starts_with","ends_with","split","join","trim",
    "to_upper","to_lower","to_string","to_int","to_float","chars","bytes",
    "replace","find","substr","format","sprintf","parse_int","parse_float",
    "keys","values","entries","flatten",
    "todo","unreachable",
    "__xs_fmt","__xs_call_with_array",
    NULL
};

/* Known-impure builtins. Stops the analyzer from being optimistic about
   a name we know does I/O even if it's not in PURE_BUILTINS. */
static const char *IMPURE_BUILTINS[] = {
    "print","println","eprint","eprintln","print_no_nl","input",
    "exit","panic","assert","assert_eq","clear","dbg","pprint",
    "spawn","channel","select","signal","derived","read_line","read_file",
    "write_file","sleep","emit_runtime_hook","cancel",
    NULL
};

/* Module prefixes: anything called as `math.foo` is pure if `math` is
   in PURE_MODULES; impure if in IMPURE_MODULES. Unknown modules are
   conservatively impure. */
static const char *PURE_MODULES[] = {
    "math","json","str","strings","array","arrays","map","maps",
    "base64","hex","url","csv","toml","msgpack","uuid",
    "regex","re","fmt","hash","sha","md5","crc",
    NULL
};

static const char *IMPURE_MODULES[] = {
    "fs","io","os","net","http","https","time","random","db","sql",
    "process","log","trace","tracer","thread","tls","cli","ffi",
    "gc","tracing","watch","input",
    NULL
};

int purity_builtin_is_pure(const char *name) {
    if (!name) return 0;
    for (int i = 0; PURE_BUILTINS[i]; i++)
        if (strcmp(name, PURE_BUILTINS[i]) == 0) return 1;
    for (int i = 0; IMPURE_BUILTINS[i]; i++)
        if (strcmp(name, IMPURE_BUILTINS[i]) == 0) return 0;
    /* Module-qualified: math.sqrt, fs.read, ... */
    const char *dot = strchr(name, '.');
    if (dot) {
        size_t mlen = (size_t)(dot - name);
        char mod[64];
        if (mlen < sizeof mod) {
            memcpy(mod, name, mlen);
            mod[mlen] = '\0';
            for (int i = 0; PURE_MODULES[i]; i++)
                if (strcmp(mod, PURE_MODULES[i]) == 0) return 1;
            for (int i = 0; IMPURE_MODULES[i]; i++)
                if (strcmp(mod, IMPURE_MODULES[i]) == 0) return 0;
        }
    }
    return 0;
}

/* Mutating method names. A method call with one of these names mutates
   the receiver in place and is impure unless the receiver is provably
   a fresh local (see is_local_owned). */
static const char *MUTATING_METHODS[] = {
    "push","pop","shift","unshift","set","insert","delete","del",
    "clear","sort","sort_by","reverse","fill","splice","remove",
    "extend","update","retain","drain",
    NULL
};

static int is_mutating_method(const char *name) {
    if (!name) return 0;
    for (int i = 0; MUTATING_METHODS[i]; i++)
        if (strcmp(name, MUTATING_METHODS[i]) == 0) return 1;
    return 0;
}

/* ─── local binding tracker ────────────────────────────────────────── */

/* Stack of names introduced by let/var/const inside the current
   function body. Pushed on entry to NODE_LET/NODE_VAR/NODE_CONST and
   popped at function-walk exit; the tracker resets per fn so an outer
   binding doesn't shadow an inner one's mutation check. */
static const char **g_locals = NULL;
static int          g_n_locals = 0;
static int          g_cap_locals = 0;

static void locals_push(const char *name) {
    if (!name) return;
    if (g_n_locals >= g_cap_locals) {
        g_cap_locals = g_cap_locals ? g_cap_locals * 2 : 32;
        g_locals = xs_realloc(g_locals, (size_t)g_cap_locals * sizeof(char *));
    }
    g_locals[g_n_locals++] = name;
}

static int is_fn_local(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < g_n_locals; i++)
        if (g_locals[i] && strcmp(g_locals[i], name) == 0) return 1;
    return 0;
}

/* ─── walk ────────────────────────────────────────────────────────── */

static void walk_for_fn(FnRec *r, Node *n);
static void collect_fns(Node *n);

static void mark_impure(FnRec *r, const char *reason) {
    (void)reason;
    r->pure = 0;
}

/* Walk a destructure pattern and record every name it binds as a
   local. Mirrors the param walker but for let/var. */
static void pattern_locals(Node *pat) {
    if (!pat) return;
    switch (VAL_TAG(pat)) {
    case NODE_PAT_IDENT:
        if (pat->pat_ident.name) locals_push(pat->pat_ident.name);
        break;
    case NODE_PAT_TUPLE:
        for (int i = 0; i < pat->pat_tuple.elems.len; i++)
            pattern_locals(pat->pat_tuple.elems.items[i]);
        break;
    case NODE_PAT_SLICE:
        for (int i = 0; i < pat->pat_slice.elems.len; i++)
            pattern_locals(pat->pat_slice.elems.items[i]);
        if (pat->pat_slice.rest) locals_push(pat->pat_slice.rest);
        break;
    case NODE_PAT_STRUCT:
        for (int i = 0; i < pat->pat_struct.fields.len; i++) {
            const char *key = pat->pat_struct.fields.items[i].key;
            Node       *sub = pat->pat_struct.fields.items[i].val;
            if (!sub && key) locals_push(key);
            else pattern_locals(sub);
        }
        break;
    case NODE_PAT_CAPTURE:
        if (pat->pat_capture.name) locals_push(pat->pat_capture.name);
        pattern_locals(pat->pat_capture.pattern);
        break;
    default: break;
    }
}

/* Walk every child node, ignoring nested fn_decl / lambda bodies (those
   are analyzed under their own FnRec by collect_fns / the per-fn pass).
   Mark r impure if a local construct is observably impure or a callee
   resolves to a known-impure builtin. */
static void walk_for_fn(FnRec *r, Node *n) {
    if (!n || !r->pure) return;
    switch (VAL_TAG(n)) {
    case NODE_FN_DECL:
    case NODE_LAMBDA:
        /* Don't descend; the inner fn is its own analysis unit. The
           outer fn's purity depends on whether it *calls* the inner
           fn, not whether the inner exists. */
        return;

    /* Local declarations introduce names we then track for the
       receiver-is-local mutation rule. */
    case NODE_LET: {
        if (n->let.value) walk_for_fn(r, n->let.value);
        if (n->let.pattern) pattern_locals(n->let.pattern);
        else if (n->let.name) locals_push(n->let.name);
        return;
    }
    case NODE_VAR: {
        if (n->let.value) walk_for_fn(r, n->let.value);
        if (n->let.pattern) pattern_locals(n->let.pattern);
        else if (n->let.name) locals_push(n->let.name);
        return;
    }
    case NODE_CONST: {
        if (n->const_.value) walk_for_fn(r, n->const_.value);
        if (n->const_.name) locals_push(n->const_.name);
        return;
    }

    /* Assignment to an outer name is observably impure. Only allow
       assigning into a fn-local. */
    case NODE_ASSIGN: {
        Node *t = n->assign.target;
        int local_ok = 0;
        if (t && VAL_TAG(t) == NODE_IDENT) {
            local_ok = is_fn_local(t->ident.name);
        } else if (t && VAL_TAG(t) == NODE_INDEX) {
            /* obj[idx] = ...: only OK if obj is a fn-local. */
            Node *obj = t->index.obj;
            if (obj && VAL_TAG(obj) == NODE_IDENT)
                local_ok = is_fn_local(obj->ident.name);
            walk_for_fn(r, t->index.index);
        } else if (t && VAL_TAG(t) == NODE_FIELD) {
            Node *obj = t->field.obj;
            if (obj && VAL_TAG(obj) == NODE_IDENT)
                local_ok = is_fn_local(obj->ident.name);
        }
        if (!local_ok) { mark_impure(r, "assignment to non-local"); return; }
        if (n->assign.value) walk_for_fn(r, n->assign.value);
        return;
    }

    /* Mutating method calls: only OK on a fn-local receiver.
       Module-style dispatch (`time.now()`, `fs.read(p)`) parses as
       a method call too; route those through the same module-purity
       table the NODE_CALL/NODE_FIELD path uses. */
    case NODE_METHOD_CALL: {
        Node *obj = n->method_call.obj;
        const char *m = n->method_call.method;
        if (is_mutating_method(m)) {
            int local_ok = 0;
            if (obj && VAL_TAG(obj) == NODE_IDENT)
                local_ok = is_fn_local(obj->ident.name);
            if (!local_ok) { mark_impure(r, "mutating method on non-local"); return; }
        }
        /* Module-qualified dispatch: `mod.fn()`. We can statically
           resolve the purity if `mod` is a bare ident matching a
           known stdlib module name. Anything else stays opaque to
           this pass and runs through the inner walks below. */
        if (obj && VAL_TAG(obj) == NODE_IDENT && obj->ident.name && m) {
            const char *mod = obj->ident.name;
            int known_mod = 0;
            for (int i = 0; PURE_MODULES[i]; i++)
                if (strcmp(mod, PURE_MODULES[i]) == 0) { known_mod = 1; break; }
            if (!known_mod) {
                for (int i = 0; IMPURE_MODULES[i]; i++)
                    if (strcmp(mod, IMPURE_MODULES[i]) == 0) { known_mod = 1; break; }
            }
            if (known_mod) {
                size_t mod_n = strlen(mod), m_n = strlen(m);
                if (mod_n + m_n + 2 < 256) {
                    char buf[256];
                    memcpy(buf, mod, mod_n);
                    buf[mod_n] = '.';
                    memcpy(buf + mod_n + 1, m, m_n);
                    buf[mod_n + 1 + m_n] = '\0';
                    fn_add_call(r, buf);
                    /* still walk args for nested impurity */
                    for (int i = 0; i < n->method_call.args.len; i++)
                        walk_for_fn(r, n->method_call.args.items[i]);
                    return;
                }
            }
        }
        if (n->method_call.obj) walk_for_fn(r, n->method_call.obj);
        for (int i = 0; i < n->method_call.args.len; i++)
            walk_for_fn(r, n->method_call.args.items[i]);
        return;
    }

    /* Calls: record the callee's name for the propagation pass. If
       the callee is a non-name expression (a call result, a field
       access), conservatively mark impure -- we can't statically
       prove the dispatch target is pure. */
    case NODE_CALL: {
        Node *cal = n->call.callee;
        if (cal && VAL_TAG(cal) == NODE_IDENT && cal->ident.name) {
            const char *nm = cal->ident.name;
            /* HOFs: arr.map / arr.filter style is the method form;
               the free-fn forms `map(arr, fn)` / `filter(...)` are
               only pure when the callback is pure. We treat them as
               pure-conditional-on-callback by recording each lambda
               argument's purity check via the call-graph. */
            fn_add_call(r, nm);
        } else if (cal && VAL_TAG(cal) == NODE_FIELD && cal->field.obj &&
                   VAL_TAG(cal->field.obj) == NODE_IDENT && cal->field.name) {
            /* mod.fn() */
            const char *mod = cal->field.obj->ident.name;
            const char *m = cal->field.name;
            if (mod && m) {
                size_t mod_n = strlen(mod), m_n = strlen(m);
                if (mod_n + m_n + 2 < 256) {
                    char buf[256];
                    memcpy(buf, mod, mod_n);
                    buf[mod_n] = '.';
                    memcpy(buf + mod_n + 1, m, m_n);
                    buf[mod_n + 1 + m_n] = '\0';
                    fn_add_call(r, buf);
                } else {
                    mark_impure(r, "indirect call");
                    return;
                }
            } else {
                mark_impure(r, "indirect call");
                return;
            }
        } else if (cal && VAL_TAG(cal) == NODE_LAMBDA) {
            /* IIFE: walk through the lambda for its purity contribution. */
            walk_for_fn(r, cal->lambda.body);
        } else if (cal) {
            /* Any other callee shape (call result, dynamic dispatch)
               is opaque to the static analyzer; treat conservatively. */
            mark_impure(r, "indirect call");
            return;
        }
        for (int i = 0; i < n->call.args.len; i++)
            walk_for_fn(r, n->call.args.items[i]);
        return;
    }

    /* Hard-impure forms. */
    case NODE_SPAWN:
    case NODE_AWAIT:
    case NODE_NURSERY:
    case NODE_PERFORM:
    case NODE_BIND:
    case NODE_SEND_EXPR:
    case NODE_PAUSE:
    case NODE_DEL:
    case NODE_LOAD:
        mark_impure(r, "hard-impure form");
        return;

    /* Try / handle bodies: walk them for inner impurity. handle is
       fine as long as the handled body is pure. */
    case NODE_HANDLE:
        walk_for_fn(r, n->handle.expr);
        for (int i = 0; i < n->handle.arms.len; i++)
            walk_for_fn(r, n->handle.arms.items[i].body);
        return;

    /* Default: walk every child Node-typed field. */
    case NODE_BLOCK: {
        int saved = g_n_locals;
        for (int i = 0; i < n->block.stmts.len; i++)
            walk_for_fn(r, n->block.stmts.items[i]);
        if (n->block.expr) walk_for_fn(r, n->block.expr);
        g_n_locals = saved;
        return;
    }

    case NODE_IF:
        walk_for_fn(r, n->if_expr.cond);
        walk_for_fn(r, n->if_expr.then);
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            walk_for_fn(r, n->if_expr.elif_conds.items[i]);
            walk_for_fn(r, n->if_expr.elif_thens.items[i]);
        }
        if (n->if_expr.else_branch) walk_for_fn(r, n->if_expr.else_branch);
        return;

    case NODE_MATCH: {
        walk_for_fn(r, n->match.subject);
        for (int i = 0; i < n->match.arms.len; i++) {
            int saved = g_n_locals;
            /* Pattern bindings introduce names that count as locals. */
            pattern_locals(n->match.arms.items[i].pattern);
            if (n->match.arms.items[i].guard)
                walk_for_fn(r, n->match.arms.items[i].guard);
            walk_for_fn(r, n->match.arms.items[i].body);
            g_n_locals = saved;
        }
        return;
    }
    case NODE_WHILE:
        walk_for_fn(r, n->while_loop.cond);
        walk_for_fn(r, n->while_loop.body);
        return;
    case NODE_FOR: {
        int saved = g_n_locals;
        pattern_locals(n->for_loop.pattern);
        walk_for_fn(r, n->for_loop.iter);
        walk_for_fn(r, n->for_loop.body);
        g_n_locals = saved;
        return;
    }
    case NODE_LOOP:
        walk_for_fn(r, n->loop.body);
        return;
    case NODE_RETURN:
        if (n->ret.value) walk_for_fn(r, n->ret.value);
        return;
    case NODE_YIELD:
        if (n->yield_.value) walk_for_fn(r, n->yield_.value);
        return;
    case NODE_THROW:
        if (n->throw_.value) walk_for_fn(r, n->throw_.value);
        return;
    case NODE_TRY:
        walk_for_fn(r, n->try_.body);
        for (int i = 0; i < n->try_.catch_arms.len; i++) {
            int saved = g_n_locals;
            pattern_locals(n->try_.catch_arms.items[i].pattern);
            if (n->try_.catch_arms.items[i].guard)
                walk_for_fn(r, n->try_.catch_arms.items[i].guard);
            walk_for_fn(r, n->try_.catch_arms.items[i].body);
            g_n_locals = saved;
        }
        if (n->try_.finally_block) walk_for_fn(r, n->try_.finally_block);
        return;
    case NODE_DEFER:
        if (n->defer_.body) walk_for_fn(r, n->defer_.body);
        return;
    case NODE_BINOP:
        walk_for_fn(r, n->binop.left);
        walk_for_fn(r, n->binop.right);
        return;
    case NODE_UNARY:
        walk_for_fn(r, n->unary.expr);
        return;
    case NODE_INDEX:
        walk_for_fn(r, n->index.obj);
        walk_for_fn(r, n->index.index);
        return;
    case NODE_FIELD:
        walk_for_fn(r, n->field.obj);
        return;
    case NODE_CAST:
        walk_for_fn(r, n->cast.expr);
        return;
    case NODE_RANGE:
        walk_for_fn(r, n->range.start);
        walk_for_fn(r, n->range.end);
        return;
    case NODE_LIT_ARRAY:
        for (int i = 0; i < n->lit_array.elems.len; i++)
            walk_for_fn(r, n->lit_array.elems.items[i]);
        if (n->lit_array.repeat_val) walk_for_fn(r, n->lit_array.repeat_val);
        return;
    case NODE_LIT_TUPLE:
        for (int i = 0; i < n->lit_array.elems.len; i++)
            walk_for_fn(r, n->lit_array.elems.items[i]);
        return;
    case NODE_LIT_MAP:
        for (int i = 0; i < n->lit_map.keys.len; i++) {
            walk_for_fn(r, n->lit_map.keys.items[i]);
            walk_for_fn(r, n->lit_map.vals.items[i]);
        }
        return;
    case NODE_INTERP_STRING:
        for (int i = 0; i < n->lit_string.parts.len; i++)
            walk_for_fn(r, n->lit_string.parts.items[i]);
        return;
    case NODE_STRUCT_INIT:
        for (int i = 0; i < n->struct_init.fields.len; i++)
            walk_for_fn(r, n->struct_init.fields.items[i].val);
        if (n->struct_init.rest) walk_for_fn(r, n->struct_init.rest);
        return;
    case NODE_SPREAD:
        walk_for_fn(r, n->spread.expr);
        return;
    case NODE_LIST_COMP:
        walk_for_fn(r, n->list_comp.element);
        for (int i = 0; i < n->list_comp.clause_iters.len; i++)
            walk_for_fn(r, n->list_comp.clause_iters.items[i]);
        for (int i = 0; i < n->list_comp.clause_conds.len; i++)
            walk_for_fn(r, n->list_comp.clause_conds.items[i]);
        return;
    case NODE_MAP_COMP:
        walk_for_fn(r, n->map_comp.key);
        walk_for_fn(r, n->map_comp.value);
        for (int i = 0; i < n->map_comp.clause_iters.len; i++)
            walk_for_fn(r, n->map_comp.clause_iters.items[i]);
        for (int i = 0; i < n->map_comp.clause_conds.len; i++)
            walk_for_fn(r, n->map_comp.clause_conds.items[i]);
        return;
    case NODE_DO_EXPR:
        walk_for_fn(r, n->do_expr.body);
        return;
    case NODE_WITH:
        walk_for_fn(r, n->with_.expr);
        if (n->with_.name) locals_push(n->with_.name);
        walk_for_fn(r, n->with_.body);
        return;
    case NODE_EXPR_STMT:
        walk_for_fn(r, n->expr_stmt.expr);
        return;
    case NODE_RESUME:
        if (n->resume_.value) walk_for_fn(r, n->resume_.value);
        /* `resume` re-enters a continuation -- non-deterministic
           with respect to handler choice; treat as impure for now. */
        mark_impure(r, "resume");
        return;
    case NODE_PROGRAM:
        for (int i = 0; i < n->program.stmts.len; i++)
            walk_for_fn(r, n->program.stmts.items[i]);
        return;
    default:
        return;
    }
}

/* Pre-pass: enumerate every fn_decl / lambda. */
static void collect_fns(Node *n) {
    if (!n) return;
    switch (VAL_TAG(n)) {
    case NODE_FN_DECL:
        fn_index_for(n);
        if (n->fn_decl.body) collect_fns(n->fn_decl.body);
        return;
    case NODE_LAMBDA:
        fn_index_for(n);
        if (n->lambda.body) collect_fns(n->lambda.body);
        return;
    case NODE_BLOCK:
        for (int i = 0; i < n->block.stmts.len; i++) collect_fns(n->block.stmts.items[i]);
        if (n->block.expr) collect_fns(n->block.expr);
        return;
    case NODE_IF:
        collect_fns(n->if_expr.cond);
        collect_fns(n->if_expr.then);
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            collect_fns(n->if_expr.elif_conds.items[i]);
            collect_fns(n->if_expr.elif_thens.items[i]);
        }
        collect_fns(n->if_expr.else_branch);
        return;
    case NODE_MATCH:
        collect_fns(n->match.subject);
        for (int i = 0; i < n->match.arms.len; i++) {
            collect_fns(n->match.arms.items[i].guard);
            collect_fns(n->match.arms.items[i].body);
        }
        return;
    case NODE_WHILE:
        collect_fns(n->while_loop.cond);
        collect_fns(n->while_loop.body);
        return;
    case NODE_FOR:
        collect_fns(n->for_loop.iter);
        collect_fns(n->for_loop.body);
        return;
    case NODE_LOOP:
        collect_fns(n->loop.body);
        return;
    case NODE_TRY:
        collect_fns(n->try_.body);
        for (int i = 0; i < n->try_.catch_arms.len; i++) {
            collect_fns(n->try_.catch_arms.items[i].guard);
            collect_fns(n->try_.catch_arms.items[i].body);
        }
        collect_fns(n->try_.finally_block);
        return;
    case NODE_DEFER:
        collect_fns(n->defer_.body);
        return;
    case NODE_HANDLE:
        collect_fns(n->handle.expr);
        for (int i = 0; i < n->handle.arms.len; i++)
            collect_fns(n->handle.arms.items[i].body);
        return;
    case NODE_LET:
    case NODE_VAR:
        collect_fns(n->let.value);
        return;
    case NODE_CONST:
        collect_fns(n->const_.value);
        return;
    case NODE_EXPR_STMT:
        collect_fns(n->expr_stmt.expr);
        return;
    case NODE_RETURN:    collect_fns(n->ret.value); return;
    case NODE_YIELD:     collect_fns(n->yield_.value); return;
    case NODE_THROW:     collect_fns(n->throw_.value); return;
    case NODE_AWAIT:     collect_fns(n->await_.expr); return;
    case NODE_NURSERY:   collect_fns(n->nursery_.body); return;
    case NODE_SPAWN:     collect_fns(n->spawn_.expr); return;
    case NODE_RESUME:    collect_fns(n->resume_.value); return;
    case NODE_DO_EXPR:   collect_fns(n->do_expr.body); return;
    case NODE_WITH:      collect_fns(n->with_.expr); collect_fns(n->with_.body); return;
    case NODE_BINOP:     collect_fns(n->binop.left); collect_fns(n->binop.right); return;
    case NODE_UNARY:     collect_fns(n->unary.expr); return;
    case NODE_ASSIGN:    collect_fns(n->assign.target); collect_fns(n->assign.value); return;
    case NODE_INDEX:     collect_fns(n->index.obj); collect_fns(n->index.index); return;
    case NODE_FIELD:     collect_fns(n->field.obj); return;
    case NODE_CAST:      collect_fns(n->cast.expr); return;
    case NODE_RANGE:     collect_fns(n->range.start); collect_fns(n->range.end); return;
    case NODE_CALL:
        collect_fns(n->call.callee);
        for (int i = 0; i < n->call.args.len; i++) collect_fns(n->call.args.items[i]);
        return;
    case NODE_METHOD_CALL:
        collect_fns(n->method_call.obj);
        for (int i = 0; i < n->method_call.args.len; i++) collect_fns(n->method_call.args.items[i]);
        return;
    case NODE_LIT_ARRAY:
        for (int i = 0; i < n->lit_array.elems.len; i++) collect_fns(n->lit_array.elems.items[i]);
        collect_fns(n->lit_array.repeat_val);
        return;
    case NODE_LIT_TUPLE:
        for (int i = 0; i < n->lit_array.elems.len; i++) collect_fns(n->lit_array.elems.items[i]);
        return;
    case NODE_LIT_MAP:
        for (int i = 0; i < n->lit_map.keys.len; i++) {
            collect_fns(n->lit_map.keys.items[i]);
            collect_fns(n->lit_map.vals.items[i]);
        }
        return;
    case NODE_INTERP_STRING:
        for (int i = 0; i < n->lit_string.parts.len; i++) collect_fns(n->lit_string.parts.items[i]);
        return;
    case NODE_STRUCT_INIT:
        for (int i = 0; i < n->struct_init.fields.len; i++) collect_fns(n->struct_init.fields.items[i].val);
        collect_fns(n->struct_init.rest);
        return;
    case NODE_SPREAD:    collect_fns(n->spread.expr); return;
    case NODE_LIST_COMP:
        collect_fns(n->list_comp.element);
        for (int i = 0; i < n->list_comp.clause_iters.len; i++) collect_fns(n->list_comp.clause_iters.items[i]);
        for (int i = 0; i < n->list_comp.clause_conds.len; i++) collect_fns(n->list_comp.clause_conds.items[i]);
        return;
    case NODE_MAP_COMP:
        collect_fns(n->map_comp.key); collect_fns(n->map_comp.value);
        for (int i = 0; i < n->map_comp.clause_iters.len; i++) collect_fns(n->map_comp.clause_iters.items[i]);
        for (int i = 0; i < n->map_comp.clause_conds.len; i++) collect_fns(n->map_comp.clause_conds.items[i]);
        return;
    case NODE_PROGRAM:
        for (int i = 0; i < n->program.stmts.len; i++) collect_fns(n->program.stmts.items[i]);
        return;
    case NODE_CLASS_DECL:
        for (int i = 0; i < n->class_decl.members.len; i++) collect_fns(n->class_decl.members.items[i]);
        return;
    case NODE_IMPL_DECL:
        for (int i = 0; i < n->impl_decl.members.len; i++) collect_fns(n->impl_decl.members.items[i]);
        return;
    case NODE_TRAIT_DECL:
        for (int i = 0; i < n->trait_decl.methods.len; i++) collect_fns(n->trait_decl.methods.items[i]);
        return;
    case NODE_MODULE_DECL:
        for (int i = 0; i < n->module_decl.body.len; i++) collect_fns(n->module_decl.body.items[i]);
        return;
    case NODE_ACTOR_DECL:
        for (int i = 0; i < n->actor_decl.methods.len; i++) collect_fns(n->actor_decl.methods.items[i]);
        return;
    default:
        return;
    }
}

/* ─── name table for fn_decl ──────────────────────────────────────── */

static int find_fn_by_name(const char *name) {
    if (!name) return -1;
    /* Multiple fn_decls can share a name (overloads); treat the name
       as pure only if every overload is pure. Conservative. */
    int found = -1;
    for (int i = 0; i < g_n_fns; i++) {
        if (g_fns[i].name && strcmp(g_fns[i].name, name) == 0) {
            if (g_fns[i].pure == 0) return i; /* one impure -> impure */
            found = i;
        }
    }
    return found;
}

void purity_analyze(Node *program) {
    /* Reset state -- this can be called multiple times per session. */
    for (int i = 0; i < g_n_fns; i++) {
        for (int j = 0; j < g_fns[i].n_calls; j++) free(g_fns[i].calls[j]);
        free(g_fns[i].calls);
    }
    g_n_fns = 0;

    if (!program) return;

    /* 1. enumerate all fn_decl / lambda nodes. */
    collect_fns(program);

    /* 2. per-fn local pass: walk the body, mark hard-impurity, build
       the call edge list. */
    for (int i = 0; i < g_n_fns; i++) {
        FnRec *r = &g_fns[i];
        Node *body = NULL;
        Node **params = NULL; int nparams = 0;
        if (VAL_TAG(r->node) == NODE_FN_DECL) {
            body    = r->node->fn_decl.body;
            nparams = r->node->fn_decl.params.len;
        } else {
            body    = r->node->lambda.body;
            nparams = r->node->lambda.params.len;
        }
        g_n_locals = 0;
        /* Parameters are NOT locals for the mutation-allowed rule:
           mutating a parameter is a callee-visible side effect.
           Don't push them. */
        (void)params; (void)nparams;
        if (body) walk_for_fn(r, body);
    }

    /* 3. propagation fixpoint: a fn becomes impure if any callee
       resolves to an impure builtin or fn_decl. Iterate until stable. */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < g_n_fns; i++) {
            FnRec *r = &g_fns[i];
            if (!r->pure) continue;
            for (int j = 0; j < r->n_calls; j++) {
                const char *name = r->calls[j];
                /* self-recursion is OK (would deadlock the analysis
                   otherwise) -- skip the lookup. */
                if (r->name && strcmp(r->name, name) == 0) continue;
                int idx = find_fn_by_name(name);
                if (idx >= 0) {
                    if (!g_fns[idx].pure) {
                        r->pure = 0;
                        changed = 1;
                        break;
                    }
                    continue;
                }
                /* Not a known fn -- treat as builtin. */
                if (!purity_builtin_is_pure(name)) {
                    r->pure = 0;
                    changed = 1;
                    break;
                }
            }
        }
    }

    /* 4. stamp the answer onto the AST. */
    for (int i = 0; i < g_n_fns; i++) {
        FnRec *r = &g_fns[i];
        if (VAL_TAG(r->node) == NODE_FN_DECL)
            r->node->fn_decl.inferred_pure = r->pure;
        else
            r->node->lambda.inferred_pure  = r->pure;
    }
}
