/*
 * regex.h - Thompson NFA-based regex engine for XS
 *
 * Supports: literals, char classes, quantifiers ({n}, {n,m}, {n,}),
 * anchors (^, $, \b), groups (capturing + non-capturing), alternation,
 * backreferences (\1-\9), and all standard escapes.
 */
#ifndef XS_REGEX_ENGINE_H
#define XS_REGEX_ENGINE_H

#include <stddef.h>

/* character class: bitmap for 0-255 + negation flag */
typedef struct {
    unsigned char bits[32]; /* 256 bits */
    int negated;
} XSCharClass;

/* NFA node types */
typedef enum {
    RE_LIT,          /* match single char */
    RE_DOT,          /* match any (except newline) */
    RE_CCLASS,       /* match char class */
    RE_SPLIT,        /* fork: try out1 then out2 */
    RE_JMP,          /* unconditional to out1 */
    RE_MATCH,        /* accepting state */
    RE_SAVE,         /* save position for capture group */
    RE_BOL,          /* ^ line start */
    RE_EOL,          /* $ line end */
    RE_WBOUND,       /* \b word boundary */
    RE_NWBOUND,      /* \B non-word boundary */
    RE_BACKREF,      /* \1-\9 backreference */
    RE_LOOKAHEAD,    /* (?=...) positive lookahead */
    RE_NEG_LOOKAHEAD /* (?!...) negative lookahead */
} RENodeType;

typedef struct RENode RENode;
struct RENode {
    RENodeType type;
    int ch;              /* RE_LIT: character to match */
    int sub;             /* RE_SAVE: slot index (group*2 + 0/1) */
    int backref;         /* RE_BACKREF: group number */
    XSCharClass cc;      /* RE_CCLASS */
    RENode *out1;
    RENode *out2;
    RENode *look_start;  /* lookahead sub-NFA */
};

/* compiled regex */
typedef struct {
    RENode *nodes;      /* arena of all nodes */
    int nnodes;
    int cap;
    RENode *start;      /* NFA entry point */
    int ngroups;        /* number of capture groups (including 0) */
    int flags;
} XSRegex;

/* match result for a single match */
#define RE_MAX_GROUPS 20
typedef struct {
    int start;          /* start offset in string */
    int end;            /* end offset */
    int group_starts[RE_MAX_GROUPS];
    int group_ends[RE_MAX_GROUPS];
    int ngroups;        /* number of groups matched */
    int matched;        /* 1 if match found */
} XSMatch;

/* flags */
#define RE_FLAG_ICASE    1
#define RE_FLAG_MULTILINE 2
#define RE_FLAG_DOTALL   4
#define RE_FLAG_GLOBAL   8

/* compile a regex pattern string into an NFA */
int xs_regex_compile(XSRegex *re, const char *pattern, int flags);

/* free compiled regex */
void xs_regex_free(XSRegex *re);

/* match regex against string, return 1 on match */
int xs_regex_match(const XSRegex *re, const char *str, int len, XSMatch *m);

/* search for first match starting at or after 'start' */
int xs_regex_search(const XSRegex *re, const char *str, int len,
                    int start, XSMatch *m);

/* find all non-overlapping matches */
int xs_regex_find_all(const XSRegex *re, const char *str, int len,
                      XSMatch **matches, int *nmatches);

/* replace first match */
char *xs_regex_replace(const XSRegex *re, const char *str, int len,
                       const char *replacement);

/* replace all matches */
char *xs_regex_replace_all(const XSRegex *re, const char *str, int len,
                           const char *replacement);

/* split string by regex */
int xs_regex_split(const XSRegex *re, const char *str, int len,
                   char ***parts, int *nparts);

/* check if entire string matches */
int xs_regex_full_match(const XSRegex *re, const char *str, int len);

/* character class helpers */
void xs_cc_clear(XSCharClass *cc);
void xs_cc_set(XSCharClass *cc, int ch);
int  xs_cc_test(const XSCharClass *cc, int ch);
void xs_cc_add_range(XSCharClass *cc, int lo, int hi);
void xs_cc_negate(XSCharClass *cc);

/* predefined classes */
void xs_cc_digit(XSCharClass *cc);
void xs_cc_word(XSCharClass *cc);
void xs_cc_space(XSCharClass *cc);
void xs_cc_alpha(XSCharClass *cc);
void xs_cc_alnum(XSCharClass *cc);
void xs_cc_xdigit(XSCharClass *cc);
void xs_cc_punct(XSCharClass *cc);

#endif /* XS_REGEX_ENGINE_H */
