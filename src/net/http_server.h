/* http_server.h - HTTP/1.1 server with routing and middleware */
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "runtime/event_loop.h"
#include <stdint.h>

/* ----------------------------------------------------------------
 *  HTTP Request
 * ---------------------------------------------------------------- */

#define HTTP_MAX_HEADERS   64
#define HTTP_MAX_PATH      2048
#define HTTP_MAX_QUERY     2048
#define HTTP_MAX_METHOD    16
#define HTTP_MAX_VERSION   16
#define HTTP_MAX_HDR_NAME  256
#define HTTP_MAX_HDR_VALUE 4096
#define HTTP_MAX_PARAMS    16
#define HTTP_MAX_BODY      (2 * 1024 * 1024)  /* 2 MB */

typedef struct {
    char method[HTTP_MAX_METHOD];
    char path[HTTP_MAX_PATH];
    char query[HTTP_MAX_QUERY];
    char version[HTTP_MAX_VERSION];
    struct {
        char name[HTTP_MAX_HDR_NAME];
        char value[HTTP_MAX_HDR_VALUE];
    } headers[HTTP_MAX_HEADERS];
    int nheaders;
    char *body;
    int body_len;
    int content_length;
    int keep_alive;
    int chunked;

    /* route params filled by router */
    struct {
        char name[64];
        char value[256];
    } params[HTTP_MAX_PARAMS];
    int nparams;

    /* parsed query params */
    struct {
        char name[128];
        char value[512];
    } query_params[32];
    int nquery_params;

    /* raw url (path + query) */
    char url[HTTP_MAX_PATH + HTTP_MAX_QUERY + 2];
} HTTPRequest;

/* ----------------------------------------------------------------
 *  HTTP Response
 * ---------------------------------------------------------------- */

typedef struct {
    int status;
    char status_text[64];
    struct {
        char name[HTTP_MAX_HDR_NAME];
        char value[HTTP_MAX_HDR_VALUE];
    } headers[32];
    int nheaders;
    char *body;
    int body_len;
    int body_cap;
    char content_type[128];
    int headers_sent;
} HTTPResponse;

/* ----------------------------------------------------------------
 *  Router
 * ---------------------------------------------------------------- */

typedef void (*HTTPHandler)(HTTPRequest *req, HTTPResponse *res, void *ctx);

typedef struct {
    char method[HTTP_MAX_METHOD];
    char pattern[256];
    HTTPHandler handler;
    void *handler_ctx;
    char *param_names[HTTP_MAX_PARAMS];
    int nparams;
    /* compiled pattern segments */
    char **segments;
    int nsegments;
    int is_wildcard;   /* pattern ends with * */
} Route;

typedef int (*MiddlewareFunc)(HTTPRequest *req, HTTPResponse *res, void *ctx);

typedef struct {
    Route *routes;
    int nroutes, cap;
    HTTPHandler not_found_handler;
    void *not_found_ctx;

    struct {
        MiddlewareFunc fn;
        void *ctx;
    } middlewares[32];
    int nmiddlewares;

    /* static file serving */
    struct {
        char prefix[256];
        char dir[1024];
    } statics[8];
    int nstatics;
} Router;

/* ----------------------------------------------------------------
 *  Connection
 * ---------------------------------------------------------------- */

typedef struct HTTPServer HTTPServer;

typedef struct {
    int fd;
    char *read_buf;
    int read_len;
    int read_cap;
    int state;       /* 0 = reading headers, 1 = reading body, 2 = done */
    HTTPServer *server;
    int64_t connected_at;
    char remote_addr[64];
    int remote_port;
} HTTPConnection;

/* ----------------------------------------------------------------
 *  Server
 * ---------------------------------------------------------------- */

struct HTTPServer {
    int listen_fd;
    EventLoop *evloop;
    Router *router;
    int port;
    int running;
    int max_connections;
    int64_t request_count;

    /* active connections */
    HTTPConnection **conns;
    int nconns, conn_cap;

    /* XS callback context (for language bindings) */
    void *xs_interp;
    void *xs_callback_ctx;

    /* access log */
    int access_log;
};

/* ----------------------------------------------------------------
 *  HTTP Request/Response API
 * ---------------------------------------------------------------- */

void http_request_init(HTTPRequest *req);
void http_request_free(HTTPRequest *req);
int  http_parse_request(const char *data, int len, HTTPRequest *req);

void http_response_init(HTTPResponse *res);
void http_response_free(HTTPResponse *res);
void http_response_status(HTTPResponse *res, int code);
void http_response_header(HTTPResponse *res, const char *name, const char *value);
void http_response_body(HTTPResponse *res, const char *data, int len);
void http_response_body_str(HTTPResponse *res, const char *str);
void http_response_content_type(HTTPResponse *res, const char *ct);
int  http_format_response(HTTPResponse *res, char *buf, int buf_size);

/* ----------------------------------------------------------------
 *  Router API
 * ---------------------------------------------------------------- */

Router *router_new(void);
void router_free(Router *r);
int  router_add(Router *r, const char *method, const char *pattern,
                HTTPHandler handler, void *ctx);
Route *router_match(Router *r, const char *method, const char *path,
                    HTTPRequest *req);
void router_set_not_found(Router *r, HTTPHandler handler, void *ctx);
void router_add_middleware(Router *r, MiddlewareFunc fn, void *ctx);
void router_add_static(Router *r, const char *prefix, const char *dir);

/* ----------------------------------------------------------------
 *  Server API
 * ---------------------------------------------------------------- */

HTTPServer *http_server_new(int port);
void http_server_free(HTTPServer *s);
void http_server_route(HTTPServer *s, const char *method,
                       const char *pattern, HTTPHandler handler, void *ctx);
void http_server_middleware(HTTPServer *s, MiddlewareFunc fn, void *ctx);
void http_server_static(HTTPServer *s, const char *prefix, const char *dir);
int  http_server_start(HTTPServer *s);
void http_server_stop(HTTPServer *s);

/* ----------------------------------------------------------------
 *  MIME type detection
 * ---------------------------------------------------------------- */

const char *mime_type_for_ext(const char *filename);

/* ----------------------------------------------------------------
 *  URL helpers
 * ---------------------------------------------------------------- */

int url_decode(const char *src, char *dst, int dst_size);
int parse_query_string(const char *qs, HTTPRequest *req);

/* ----------------------------------------------------------------
 *  Status code text
 * ---------------------------------------------------------------- */

const char *http_status_text(int code);

#endif /* HTTP_SERVER_H */
