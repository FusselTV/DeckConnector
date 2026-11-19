#include "stream.h"

#include <stdio.h>
#include <sys/select.h>

int stream_recv(void *ctx, char *buf, size_t len, bool wait)
{
    httpd_req_t *req = ctx;
    if (!wait) {
        int fd = httpd_req_to_sockfd(req);
        fd_set r;
        FD_ZERO(&r);
        FD_SET(fd, &r);
        struct timeval now = {0};
        if (select(fd + 1, &r, NULL, NULL, &now) <= 0) {
            return 0;
        }
    }
    int n = httpd_req_recv(req, buf, len); /* waits at most the server's recv timeout */
    return n > 0 ? n : -1;
}

void stream_end(httpd_req_t *req, const char *error, bool all)
{
    httpd_handle_t hd = req->handle;
    int fd = httpd_req_to_sockfd(req);
    char body[64];
    snprintf(body, sizeof(body), error ? "{\"error\":\"%s\"}" : "{\"ok\":true,\"stopped\":%s}",
             error ? error
             : all ? "false"
                   : "true");
    if (error) {
        httpd_resp_set_status(req, "503 Service Unavailable");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close"); /* else a client keeps it for its next request */
    httpd_resp_sendstr(req, body);
    httpd_req_async_handler_complete(req);
    httpd_sess_trigger_close(hd, fd);
}
