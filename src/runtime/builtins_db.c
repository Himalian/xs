#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "core/xs_compat.h"
#include "runtime/interp.h"
#include "runtime/builtins.h"
#include "core/value.h"
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>          /* strcasecmp / strncasecmp */
#else
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#endif
#include <stdio.h>
#include <ctype.h>

/* in-memory kv store */

/* Helper: skip whitespace */
static const char *db_skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Helper: case-insensitive prefix match, returns pointer past match or NULL */
static const char *db_match_kw(const char *s, const char *kw) {
    s = db_skip_ws(s);
    size_t klen = strlen(kw);
    if (strncasecmp(s, kw, klen) == 0 && (s[klen] == '\0' || isspace((unsigned char)s[klen]) || s[klen] == '(')) {
        return s + klen;
    }
    return NULL;
}

/* Helper: read an identifier (table name, etc.) */
static const char *db_read_ident(const char *s, char *buf, int bufsz) {
    s = db_skip_ws(s);
    int i = 0;
    while (*s && !isspace((unsigned char)*s) && *s != '(' && *s != ')' && *s != ',' && *s != ';' && i < bufsz - 1) {
        buf[i++] = *s++;
    }
    buf[i] = '\0';
    return s;
}

/* Internal: execute a SQL-like command on the db, returning a result.
   Supports:
     CREATE TABLE name
     INSERT INTO name VALUES (v1, v2, ...)
     SELECT * FROM name
     DELETE FROM name
     DELETE FROM name WHERE key = value
     DROP TABLE name
*/

static const char *xs_strcasestr_fn(const char *h, const char *n) {
    size_t nlen = strlen(n);
    if (!nlen) return h;
    for (; *h; h++) {
        if (strncasecmp(h, n, nlen) == 0) return h;
    }
    return NULL;
}

/* Resolve a user-written column name to the positional 'cN' key that
   rows are stored under. Falls back to the name itself if the column
   was already in positional form or the schema is missing. */
static void db_resolve_col(Value *db_val, const char *tname,
                            const char *user_name, char *out, size_t outsz) {
    snprintf(out, outsz, "%s", user_name);
    if (!user_name[0]) return;
    /* already positional (c0, c1, ...) */
    if (user_name[0] == 'c' && user_name[1] >= '0' && user_name[1] <= '9') {
        int all_digits = 1;
        for (const char *q = user_name + 1; *q; q++)
            if (*q < '0' || *q > '9') { all_digits = 0; break; }
        if (all_digits) return;
    }
    Value *sch = map_get(db_val->map, "_schemas");
    if (!sch || (VAL_TAG(sch) != XS_MAP && VAL_TAG(sch) != XS_MODULE)) return;
    Value *cols = map_get(sch->map, tname);
    if (!cols || VAL_TAG(cols) != XS_ARRAY) return;
    for (int i = 0; i < cols->arr->len; i++) {
        Value *cn = cols->arr->items[i];
        if (cn && VAL_TAG(cn) == XS_STR && strcasecmp(cn->s, user_name) == 0) {
            snprintf(out, outsz, "c%d", i);
            return;
        }
    }
}

static Value *db_execute(Value *db_val, const char *sql, int return_rows) {
    if (!db_val || (VAL_TAG(db_val) != XS_MAP && VAL_TAG(db_val) != XS_MODULE) || !db_val->map)
        return xs_str("error: invalid db handle");
    Value *tables_v = map_get(db_val->map, "_tables");
    if (!tables_v || (VAL_TAG(tables_v) != XS_MAP && VAL_TAG(tables_v) != XS_MODULE) || !tables_v->map)
        return xs_str("error: corrupt db (no _tables)");
    XSMap *tables = tables_v->map;

    const char *p = sql;
    const char *rest;
    char tname[256];

    /* CREATE TABLE name (col1, col2, ...) */
    if ((rest = db_match_kw(p, "CREATE")) != NULL) {
        rest = db_match_kw(rest, "TABLE");
        if (!rest) return xs_str("error: expected TABLE after CREATE");
        rest = db_read_ident(rest, tname, sizeof tname);
        if (tname[0] == '\0') return xs_str("error: missing table name");
        if (map_get(tables, tname)) return xs_str("error: table already exists");
        Value *tbl = xs_array_new();
        map_set(tables, tname, tbl);
        value_decref(tbl);
        /* Parse optional column list, store names under _schemas[tname]. */
        Value *cols = xs_array_new();
        rest = db_skip_ws(rest);
        if (*rest == '(') {
            rest++;
            while (*rest && *rest != ')') {
                rest = db_skip_ws(rest);
                char cbuf[128];
                rest = db_read_ident(rest, cbuf, sizeof cbuf);
                if (cbuf[0]) {
                    Value *cs = xs_str(cbuf);
                    array_push(cols->arr, cs);
                    value_decref(cs);
                }
                /* skip any per-column type/constraint tokens to the next , or ) */
                while (*rest && *rest != ',' && *rest != ')') rest++;
                if (*rest == ',') rest++;
            }
        }
        Value *sch = map_get(db_val->map, "_schemas");
        if (sch && (VAL_TAG(sch) == XS_MAP || VAL_TAG(sch) == XS_MODULE))
            map_set(sch->map, tname, cols);
        value_decref(cols);
        return xs_str("ok");
    }

    /* DROP TABLE name */
    if ((rest = db_match_kw(p, "DROP")) != NULL) {
        rest = db_match_kw(rest, "TABLE");
        if (!rest) return xs_str("error: expected TABLE after DROP");
        rest = db_read_ident(rest, tname, sizeof tname);
        (void)rest;
        if (tname[0] == '\0') return xs_str("error: missing table name");
        if (!map_get(tables, tname)) return xs_str("error: no such table");
        map_set(tables, tname, value_incref(XS_NULL_VAL));
        return xs_str("ok");
    }

    /* INSERT INTO name VALUES (...) */
    if ((rest = db_match_kw(p, "INSERT")) != NULL) {
        rest = db_match_kw(rest, "INTO");
        if (!rest) return xs_str("error: expected INTO after INSERT");
        rest = db_read_ident(rest, tname, sizeof tname);
        if (tname[0] == '\0') return xs_str("error: missing table name");
        Value *tbl = map_get(tables, tname);
        if (!tbl || VAL_TAG(tbl) != XS_ARRAY) return xs_str("error: no such table");

        rest = db_match_kw(rest, "VALUES");
        if (!rest) return xs_str("error: expected VALUES");
        rest = db_skip_ws(rest);
        if (*rest != '(') return xs_str("error: expected ( after VALUES");
        rest++; /* skip '(' */

        /* Parse comma-separated values until ')' */
        XSMap *row = map_new();
        int col = 0;
        while (*rest && *rest != ')') {
            rest = db_skip_ws(rest);
            if (*rest == ')') break;

            /* Read a value: string (quoted) or number or identifier */
            char vbuf[1024];
            int vi = 0;
            if (*rest == '\'' || *rest == '"') {
                char quote = *rest++;
                while (*rest && *rest != quote && vi < (int)sizeof(vbuf) - 1)
                    vbuf[vi++] = *rest++;
                if (*rest == quote) rest++;
                vbuf[vi] = '\0';
                char col_name[32];
                snprintf(col_name, sizeof col_name, "c%d", col);
                Value *sv = xs_str(vbuf);
                map_set(row, col_name, sv);
                value_decref(sv);
            } else {
                while (*rest && *rest != ',' && *rest != ')' && !isspace((unsigned char)*rest) && vi < (int)sizeof(vbuf) - 1)
                    vbuf[vi++] = *rest++;
                vbuf[vi] = '\0';
                char col_name[32];
                snprintf(col_name, sizeof col_name, "c%d", col);
                /* Try parsing as integer */
                char *endp;
                long long ival = strtoll(vbuf, &endp, 10);
                if (*endp == '\0' && vi > 0) {
                    Value *iv = xs_int((int64_t)ival);
                    map_set(row, col_name, iv);
                    value_decref(iv);
                } else {
                    Value *sv = xs_str(vbuf);
                    map_set(row, col_name, sv);
                    value_decref(sv);
                }
            }
            col++;
            rest = db_skip_ws(rest);
            if (*rest == ',') rest++;
        }
        Value *row_v = xs_module(row);
        array_push(tbl->arr, row_v);
        value_decref(row_v);
        return xs_str("ok");
    }

    /* SELECT * FROM name [WHERE key = value] */
    if ((rest = db_match_kw(p, "SELECT")) != NULL) {
        rest = db_skip_ws(rest);
        /* skip column list: we only support * */
        if (*rest == '*') rest++;
        else {
            /* skip until FROM */
            const char *from = xs_strcasestr_fn(rest, "FROM");
            if (!from) return xs_str("error: expected FROM");
            rest = from;
        }
        rest = db_match_kw(rest, "FROM");
        if (!rest) return xs_str("error: expected FROM");
        rest = db_read_ident(rest, tname, sizeof tname);
        if (tname[0] == '\0') return xs_str("error: missing table name");
        Value *tbl = map_get(tables, tname);
        if (!tbl || VAL_TAG(tbl) != XS_ARRAY) return xs_str("error: no such table");

        /* Check for WHERE clause */
        const char *where = db_match_kw(rest, "WHERE");
        char where_key[256] = {0};
        char where_val[1024] = {0};
        if (where) {
            where = db_read_ident(where, where_key, sizeof where_key);
            where = db_skip_ws(where);
            if (*where == '=') where++;
            where = db_skip_ws(where);
            /* Read value */
            int wi = 0;
            if (*where == '\'' || *where == '"') {
                char q = *where++;
                while (*where && *where != q && wi < (int)sizeof(where_val) - 1)
                    where_val[wi++] = *where++;
            } else {
                while (*where && !isspace((unsigned char)*where) && *where != ';' && wi < (int)sizeof(where_val) - 1)
                    where_val[wi++] = *where++;
            }
            where_val[wi] = '\0';
        }

        char resolved_key[256];
        if (where_key[0])
            db_resolve_col(db_val, tname, where_key, resolved_key, sizeof resolved_key);
        else resolved_key[0] = '\0';

        Value *results = xs_array_new();
        for (int i = 0; i < tbl->arr->len; i++) {
            Value *row = tbl->arr->items[i];
            if (!row || (VAL_TAG(row) != XS_MAP && VAL_TAG(row) != XS_MODULE)) continue;
            if (resolved_key[0]) {
                Value *fv = map_get(row->map, resolved_key);
                if (!fv) continue;
                char *fs = value_str(fv);
                int match = (strcmp(fs, where_val) == 0);
                free(fs);
                if (!match) continue;
            }
            array_push(results->arr, row);
        }
        return results;
    }

    /* DELETE FROM name [WHERE key = value] */
    if ((rest = db_match_kw(p, "DELETE")) != NULL) {
        rest = db_match_kw(rest, "FROM");
        if (!rest) return xs_str("error: expected FROM after DELETE");
        rest = db_read_ident(rest, tname, sizeof tname);
        if (tname[0] == '\0') return xs_str("error: missing table name");
        Value *tbl = map_get(tables, tname);
        if (!tbl || VAL_TAG(tbl) != XS_ARRAY) return xs_str("error: no such table");

        /* Check for WHERE clause */
        const char *where = db_match_kw(rest, "WHERE");
        if (!where) {
            /* Delete all rows */
            tbl->arr->len = 0;
            return xs_str("ok");
        }

        char where_key[256] = {0};
        char where_val[1024] = {0};
        where = db_read_ident(where, where_key, sizeof where_key);
        where = db_skip_ws(where);
        if (*where == '=') where++;
        where = db_skip_ws(where);
        int wi = 0;
        if (*where == '\'' || *where == '"') {
            char q = *where++;
            while (*where && *where != q && wi < (int)sizeof(where_val) - 1)
                where_val[wi++] = *where++;
        } else {
            while (*where && !isspace((unsigned char)*where) && *where != ';' && wi < (int)sizeof(where_val) - 1)
                where_val[wi++] = *where++;
        }
        where_val[wi] = '\0';

        char resolved_key[256];
        db_resolve_col(db_val, tname, where_key, resolved_key, sizeof resolved_key);

        /* Remove matching rows (compact in-place) */
        int dst = 0;
        for (int i = 0; i < tbl->arr->len; i++) {
            Value *row = tbl->arr->items[i];
            int keep = 1;
            if (row && (VAL_TAG(row) == XS_MAP || VAL_TAG(row) == XS_MODULE) && row->map) {
                Value *fv = map_get(row->map, resolved_key);
                if (fv) {
                    char *fs = value_str(fv);
                    if (strcmp(fs, where_val) == 0) keep = 0;
                    free(fs);
                }
            }
            if (keep) {
                tbl->arr->items[dst++] = tbl->arr->items[i];
            } else {
                value_decref(tbl->arr->items[i]);
            }
        }
        tbl->arr->len = dst;
        return xs_str("ok");
    }

    return xs_str("error: unrecognized SQL command");
}

static Value *native_db_open(Interp *ig, Value **a, int n) {
    (void)ig;
    XSMap *db = map_new();
    const char *name = (n > 0 && VAL_TAG(a[0]) == XS_STR) ? a[0]->s : "memdb";
    map_set(db, "_name", xs_str(name));
    XSMap *tables = map_new();
    Value *tv = xs_module(tables);
    map_set(db, "_tables", tv);
    value_decref(tv);
    /* _schemas maps table name -> array of column names, so WHERE/SELECT
       can resolve real column identifiers instead of only positional
       c0/c1/... names. */
    XSMap *schemas = map_new();
    Value *sv = xs_module(schemas);
    map_set(db, "_schemas", sv);
    value_decref(sv);
    return xs_module(db);
}

static Value *native_db_exec(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2) return xs_str("error: db.exec requires (db, sql)");
    if ((VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE) || VAL_TAG(a[1]) != XS_STR)
        return xs_str("error: invalid arguments to db.exec");
    return db_execute(a[0], a[1]->s, 0);
}

static Value *native_db_query(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2) return xs_str("error: db.query requires (db, sql)");
    if ((VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE) || VAL_TAG(a[1]) != XS_STR)
        return xs_str("error: invalid arguments to db.query");
    return db_execute(a[0], a[1]->s, 1);
}

static Value *native_db_close(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE))
        return xs_str("error: db.close requires a db handle");
    /* Mark db as closed by removing _tables */
    map_set(a[0]->map, "_tables", value_incref(XS_NULL_VAL));
    map_set(a[0]->map, "_closed", value_incref(XS_TRUE_VAL));
    return xs_str("ok");
}

Value *make_db_module(void) {
    XSMap *m = map_new();
    map_take(m, "open",  xs_native(native_db_open));
    map_take(m, "exec",  xs_native(native_db_exec));
    map_take(m, "query", xs_native(native_db_query));
    map_take(m, "close", xs_native(native_db_close));
    return xs_module(m);
}
