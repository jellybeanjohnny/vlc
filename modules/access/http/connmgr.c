/*****************************************************************************
 * connmgr.c: HTTP/TLS VLC connection manager
 *****************************************************************************
 * Copyright © 2015 Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <vlc_common.h>
#include <vlc_network.h>
#include <vlc_tls.h>
#include <vlc_url.h>
#include "transport.h"
#include "conn.h"
#include "connmgr.h"
#include "message.h"

#pragma GCC visibility push(default)

void vlc_http_err(void *ctx, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    msg_GenericVa((vlc_object_t *)ctx, VLC_MSG_ERR, fmt, ap);
    va_end(ap);
}

void vlc_http_dbg(void *ctx, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    msg_GenericVa((vlc_object_t *)ctx, VLC_MSG_DBG, fmt, ap);
    va_end(ap);
}

vlc_tls_t *vlc_https_connect(vlc_tls_creds_t *creds, const char *name,
                             unsigned port, bool *restrict two)
{
    if (port == 0)
        port = 443;

    /* TLS with ALPN */
    const char *alpn[] = { "h2", "http/1.1", NULL };
    char *alp;

    vlc_tls_t *tls = vlc_tls_SocketOpenTLS(creds, name, port, "https",
                                           alpn + !*two, &alp);
    if (tls != NULL)
    {
        *two = (alp != NULL) && !strcmp(alp, "h2");
        free(alp);
    }
    return tls;
}

static char *vlc_http_proxy_find(const char *hostname, unsigned port,
                                 bool secure)
{
    const char *fmt;
    char *url, *proxy = NULL;

    if (strchr(hostname, ':') != NULL)
        fmt = port ? "http%s://[%s]:%u" : "http%s://[%s]";
    else
        fmt = port ? "http%s://%s:%u" : "http%s://%s";

    if (likely(asprintf(&url, fmt, secure ? "s" : "", hostname, port) >= 0))
    {
        proxy = vlc_getProxyUrl(url);
        free(url);
    }
    return proxy;
}

static vlc_tls_t *vlc_https_connect_i11e(vlc_tls_creds_t *creds,
                                         const char *host, unsigned port,
                                         bool *restrict http_two)
{
    vlc_tls_t *tls;

    char *proxy = vlc_http_proxy_find(host, port, true);
    if (proxy != NULL)
    {
        tls = vlc_https_connect_proxy(creds->obj.parent, creds, host, port,
                                      http_two, proxy);
        free(proxy);
    }
    else
        tls = vlc_https_connect(creds, host, port, http_two);

    return tls;
}

static vlc_tls_t *vlc_http_connect_i11e(vlc_object_t *obj,
                                        const char *host, unsigned port,
                                        bool *restrict proxied)
{
    vlc_tls_t *tls;

    char *proxy = vlc_http_proxy_find(host, port, false);
    if (proxy != NULL)
    {
        vlc_url_t url;

        vlc_UrlParse(&url, proxy);
        free(proxy);

        if (url.psz_host != NULL)
            tls =vlc_tls_SocketOpenTCP(obj, url.psz_host,
                                       url.i_port ? url.i_port : 80);
        else
            tls = NULL;

        vlc_UrlClean(&url);
    }
    else
        tls = vlc_tls_SocketOpenTCP(obj, host, port ? port : 80);

    *proxied = proxy != NULL;
    return tls;
}


struct vlc_http_mgr
{
    vlc_object_t *obj;
    vlc_tls_creds_t *creds;
    struct vlc_http_cookie_jar_t *jar;
    struct vlc_http_conn *conn;
    bool use_h2c;
};

static struct vlc_http_conn *vlc_http_mgr_find(struct vlc_http_mgr *mgr,
                                               const char *host, unsigned port)
{
    (void) host; (void) port;
    return mgr->conn;
}

static void vlc_http_mgr_release(struct vlc_http_mgr *mgr,
                                 struct vlc_http_conn *conn)
{
    assert(mgr->conn == conn);
    mgr->conn = NULL;

    vlc_http_conn_release(conn);
}

static
struct vlc_http_msg *vlc_http_mgr_reuse(struct vlc_http_mgr *mgr,
                                        const char *host, unsigned port,
                                        const struct vlc_http_msg *req)
{
    struct vlc_http_conn *conn = vlc_http_mgr_find(mgr, host, port);
    if (conn == NULL)
        return NULL;

    struct vlc_http_stream *stream = vlc_http_stream_open(conn, req);
    if (stream != NULL)
    {
        struct vlc_http_msg *m = vlc_http_msg_get_initial(stream);
        if (m != NULL)
            return m;

        /* NOTE: If the request were not idempotent, we would not know if it
         * was processed by the other end. Thus POST is not used/supported so
         * far, and CONNECT is treated as if it were idempotent (which works
         * fine here). */
    }
    /* Get rid of closing or reset connection */
    vlc_http_mgr_release(mgr, conn);
    return NULL;
}

static struct vlc_http_msg *vlc_https_request(struct vlc_http_mgr *mgr,
                                              const char *host, unsigned port,
                                              const struct vlc_http_msg *req)
{
    if (mgr->creds == NULL && mgr->conn != NULL)
        return NULL; /* switch from HTTP to HTTPS not implemented */

    if (mgr->creds == NULL)
    {   /* First TLS connection: load x509 credentials */
        mgr->creds = vlc_tls_ClientCreate(mgr->obj);
        if (mgr->creds == NULL)
            return NULL;
    }

    /* TODO? non-idempotent request support */
    struct vlc_http_msg *resp = vlc_http_mgr_reuse(mgr, host, port, req);
    if (resp != NULL)
        return resp; /* existing connection reused */

    bool http2 = true;
    vlc_tls_t *tls = vlc_https_connect_i11e(mgr->creds, host, port, &http2);
    if (tls == NULL)
        return NULL;

    struct vlc_http_conn *conn;

    /* For HTTPS, TLS-ALPN determines whether HTTP version 2.0 ("h2") or 1.1
     * ("http/1.1") is used.
     * NOTE: If the negotiated protocol is explicitly "http/1.1", HTTP 1.0
     * should not be used. HTTP 1.0 should only be used if ALPN is not
     * supported by the server.
     * NOTE: We do not enforce TLS version 1.2 for HTTP 2.0 explicitly.
     */
    if (http2)
        conn = vlc_h2_conn_create(mgr->obj, tls);
    else
        conn = vlc_h1_conn_create(mgr->obj, tls, false);

    if (unlikely(conn == NULL))
    {
        vlc_tls_Close(tls);
        return NULL;
    }

    mgr->conn = conn;

    return vlc_http_mgr_reuse(mgr, host, port, req);
}

static struct vlc_http_msg *vlc_http_request(struct vlc_http_mgr *mgr,
                                             const char *host, unsigned port,
                                             const struct vlc_http_msg *req)
{
    if (mgr->creds != NULL && mgr->conn != NULL)
        return NULL; /* switch from HTTPS to HTTP not implemented */

    struct vlc_http_msg *resp = vlc_http_mgr_reuse(mgr, host, port, req);
    if (resp != NULL)
        return resp;

    bool proxy;
    vlc_tls_t *tls = vlc_http_connect_i11e(mgr->obj, host, port, &proxy);
    if (tls == NULL)
        return NULL;

    struct vlc_http_conn *conn;

    if (mgr->use_h2c)
        conn = vlc_h2_conn_create(mgr->obj, tls);
    else
        conn = vlc_h1_conn_create(mgr->obj, tls, proxy);

    if (unlikely(conn == NULL))
    {
        vlc_tls_Close(tls);
        return NULL;
    }

    mgr->conn = conn;

    return vlc_http_mgr_reuse(mgr, host, port, req);
}

struct vlc_http_msg *vlc_http_mgr_request(struct vlc_http_mgr *mgr, bool https,
                                          const char *host, unsigned port,
                                          const struct vlc_http_msg *m)
{
    return (https ? vlc_https_request : vlc_http_request)(mgr, host, port, m);
}

struct vlc_http_cookie_jar_t *vlc_http_mgr_get_jar(struct vlc_http_mgr *mgr)
{
    return mgr->jar;
}

struct vlc_http_mgr *vlc_http_mgr_create(vlc_object_t *obj,
                                         struct vlc_http_cookie_jar_t *jar,
                                         bool h2c)
{
    struct vlc_http_mgr *mgr = malloc(sizeof (*mgr));
    if (unlikely(mgr == NULL))
        return NULL;

    mgr->obj = obj;
    mgr->creds = NULL;
    mgr->jar = jar;
    mgr->conn = NULL;
    mgr->use_h2c = h2c;
    return mgr;
}

void vlc_http_mgr_destroy(struct vlc_http_mgr *mgr)
{
    if (mgr->conn != NULL)
        vlc_http_mgr_release(mgr, mgr->conn);
    if (mgr->creds != NULL)
        vlc_tls_Delete(mgr->creds);
    free(mgr);
}
