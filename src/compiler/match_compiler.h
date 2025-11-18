#ifndef MATCH_COMPILER_H
#define MATCH_COMPILER_H

#include "core/ast.h"

typedef enum {
    DT_LEAF,
    DT_FAIL,
    DT_SWITCH,
    DT_GUARD,
} DTNodeKind;

typedef enum {
    CTOR_INT,
    CTOR_FLOAT,
    CTOR_STRING,
    CTOR_BOOL,
    CTOR_NULL,
    CTOR_ENUM,
    CTOR_WILD,
    CTOR_TUPLE,
    CTOR_RANGE,
} CtorKind;

typedef struct Constructor {
    CtorKind kind;
    int64_t  ival;
    double   fval;
    char    *sval;
    int      bval;
    char    *enum_path;
    int      arity;
    int      range_inclusive;
    Node    *range_start;
    Node    *range_end;
} Constructor;

typedef struct DTNode DTNode;

typedef struct {
    Constructor ctor;
    char      **bindings;
    int         nbindings;
    DTNode     *child;
} DTBranch;

struct DTNode {
    DTNodeKind kind;
    union {
        struct {
            int    arm_index;
            char **bound_names;
            int   *bound_cols;
            int    nbounds;
            Node  *body;
        } leaf;
        struct {
            int        col;
            DTBranch  *branches;
            int        nbranches;
            int        cap_branches;
            DTNode    *fallback;
        } sw;
        struct {
            Node   *cond;
            DTNode *then_dt;
            DTNode *else_dt;
        } guard;
    };
};

typedef struct {
    Node **patterns;
    Node  *guard;
    Node  *body;
    int    arm_index;
} PatRow;

typedef struct {
    PatRow *rows;
    int     nrows;
    int     cap_rows;
    int     ncols;
} PatMatrix;

DTNode *match_compile(Node *match_node);
void    dt_free(DTNode *dt);
Node   *dt_to_ast(DTNode *dt, Node *subject, Span span);

void match_compiler_optimize(Node *program);

#endif
