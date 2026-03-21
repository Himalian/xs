#define _POSIX_C_SOURCE 200809L
#include "semantic/scoped_check.h"
#include "core/xs.h"
#include <stdlib.h>
#include <string.h>

/* Stack of scoped binding names active in the current function.
 * Lookup is linear; the stacks are small in practice (most blocks
 * carry zero scoped bindings, and a single function rarely has
 * more than a handful). */
typedef struct {
    char **names;
    int    len, cap;
} NameStack;

static void ns_init(NameStack *s) { s->names = NULL; s->len = 0; s->cap = 0; }

static void ns_push(NameStack *s, const char *name) {
    if (!name) return;
    if (s->len >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->names = realloc(s->names, sizeof(char *) * s->cap);
    }
    s->names[s->len++] = xs_strdup(name);
}

static int ns_contains(NameStack *s, const char *name) {
    if (!name) return 0;
    for (int i = 0; i < s->len; i++)
        if (s->names[i] && strcmp(s->names[i], name) == 0) return 1;
    return 0;
}

static void ns_truncate(NameStack *s, int to) {
    while (s->len > to) free(s->names[--s->len]);
}

static void ns_free(NameStack *s) {
    for (int i = 0; i < s->len; i++) free(s->names[i]);
    free(s->names);
    s->names = NULL; s->len = 0; s->cap = 0;
}

static void emit_escape_error(SemaCtx *ctx, Node *site, const char *name,
                              const char *via) {
    Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "S0042",
        "scoped binding '%s' escapes its block", name ? name : "?");
    diag_annotate(d, site->span, 1, "%s here", via);
    diag_note(d, "@scoped values must not outlive their enclosing block");
    diag_hint(d, "drop @scoped or copy the value out before returning");
    diag_emit(ctx->diag, d);
    ctx->n_errors++;
}

static int is_scoped_ident(Node *n, NameStack *scoped) {
    if (!n) return 0;
    if (VAL_TAG(n) != NODE_IDENT) return 0;
    return ns_contains(scoped, n->ident.name);
}

static void check(SemaCtx *ctx, Node *n, NameStack *scoped) {
    if (!n) return;
    switch (VAL_TAG(n)) {

    case NODE_LET:
    case NODE_VAR:
        if (n->let.value) check(ctx, n->let.value, scoped);
        if (n->let.is_scoped && n->let.name) {
            ns_push(scoped, n->let.name);
        } else if (n->let.value && is_scoped_ident(n->let.value, scoped)) {
            /* `let outer = scoped_var` -- moves a scoped binding into a
             * non-scoped slot, escaping. */
            emit_escape_error(ctx, n, n->let.value->ident.name,
                              "stored in non-scoped binding");
        }
        break;

    case NODE_RETURN:
        if (n->ret.value) check(ctx, n->ret.value, scoped);
        if (is_scoped_ident(n->ret.value, scoped)) {
            emit_escape_error(ctx, n, n->ret.value->ident.name, "returned");
        }
        break;

    case NODE_ASSIGN:
        if (n->assign.value) check(ctx, n->assign.value, scoped);
        if (n->assign.target) check(ctx, n->assign.target, scoped);
        /* x = scoped: only OK if x itself is a scoped binding in this
         * frame. Sema doesn't track that yet, so be conservative: any
         * assignment of a scoped ident to a non-scoped target is an
         * escape. */
        if (is_scoped_ident(n->assign.value, scoped) &&
            VAL_TAG(n->assign.target) == NODE_IDENT &&
            !ns_contains(scoped, n->assign.target->ident.name)) {
            emit_escape_error(ctx, n, n->assign.value->ident.name,
                              "stored in non-scoped binding");
        }
        break;

    case NODE_CALL:
        if (n->call.callee) check(ctx, n->call.callee, scoped);
        for (int i = 0; i < n->call.args.len; i++) {
            Node *a = n->call.args.items[i];
            check(ctx, a, scoped);
            /* A scoped value passed to a function may escape if the
             * function stores it. Conservative: forbid passing scoped
             * idents as args unless the call is to a builtin we know
             * doesn't retain (println, str, len, etc). */
            if (is_scoped_ident(a, scoped)) {
                int safe = 0;
                if (n->call.callee && VAL_TAG(n->call.callee) == NODE_IDENT) {
                    const char *cn = n->call.callee->ident.name;
                    if (cn && (strcmp(cn, "println") == 0 ||
                               strcmp(cn, "print")   == 0 ||
                               strcmp(cn, "str")     == 0 ||
                               strcmp(cn, "len")     == 0 ||
                               strcmp(cn, "type_of") == 0 ||
                               strcmp(cn, "assert")  == 0)) safe = 1;
                }
                if (!safe) {
                    emit_escape_error(ctx, a, a->ident.name,
                                      "passed to a non-pure callee");
                }
            }
        }
        break;

    case NODE_METHOD_CALL:
        if (n->method_call.obj) check(ctx, n->method_call.obj, scoped);
        for (int i = 0; i < n->method_call.args.len; i++) {
            Node *a = n->method_call.args.items[i];
            check(ctx, a, scoped);
            if (is_scoped_ident(a, scoped)) {
                /* arr.push(scoped) / map.set(_, scoped) etc -- always
                 * stores into the receiver, so the value escapes. */
                emit_escape_error(ctx, a, a->ident.name,
                                  "stored via method call");
            }
        }
        break;

    case NODE_LAMBDA: {
        /* a closure that captures a scoped ident is itself an escape
         * vector (the closure may outlive the scope). Conservative:
         * forbid scoped idents from appearing inside lambda bodies. */
        int saved = scoped->len;
        check(ctx, n->lambda.body, scoped);
        ns_truncate(scoped, saved);
        break;
    }

    case NODE_FN_DECL: {
        /* nested fn body gets its own scoped frame */
        int saved = scoped->len;
        check(ctx, n->fn_decl.body, scoped);
        ns_truncate(scoped, saved);
        break;
    }

    case NODE_BLOCK: {
        int saved = scoped->len;
        for (int i = 0; i < n->block.stmts.len; i++)
            check(ctx, n->block.stmts.items[i], scoped);
        if (n->block.expr) check(ctx, n->block.expr, scoped);
        /* a block-as-expression that yields a scoped ident lets the
         * value flow outward as the block's value -- treat that as
         * an escape too. */
        if (n->block.expr && is_scoped_ident(n->block.expr, scoped)) {
            emit_escape_error(ctx, n->block.expr,
                              n->block.expr->ident.name,
                              "is the value of an enclosing block");
        }
        ns_truncate(scoped, saved);
        break;
    }

    case NODE_IF:
        if (n->if_expr.cond) check(ctx, n->if_expr.cond, scoped);
        if (n->if_expr.then) check(ctx, n->if_expr.then, scoped);
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            check(ctx, n->if_expr.elif_conds.items[i], scoped);
            check(ctx, n->if_expr.elif_thens.items[i], scoped);
        }
        if (n->if_expr.else_branch) check(ctx, n->if_expr.else_branch, scoped);
        break;

    case NODE_FOR:
        if (n->for_loop.iter) check(ctx, n->for_loop.iter, scoped);
        if (n->for_loop.body) check(ctx, n->for_loop.body, scoped);
        break;

    case NODE_WHILE:
        if (n->while_loop.cond) check(ctx, n->while_loop.cond, scoped);
        if (n->while_loop.body) check(ctx, n->while_loop.body, scoped);
        break;

    case NODE_PROGRAM:
        for (int i = 0; i < n->program.stmts.len; i++)
            check(ctx, n->program.stmts.items[i], scoped);
        break;

    default:
        /* fall through: most other node kinds don't introduce escape
         * sites. binops, indices, fields, etc. are read-only on
         * their operands. */
        break;
    }
}

void scoped_check_program(SemaCtx *ctx, Node *program) {
    if (!ctx || !program) return;
    NameStack scoped; ns_init(&scoped);
    check(ctx, program, &scoped);
    ns_free(&scoped);
}
