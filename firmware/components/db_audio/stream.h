#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_http_server.h"

/* db_stream_io_t.recv on an async copy of the request. */
int stream_recv(void *ctx, char *buf, size_t len, bool wait);

/* Answers (503 with `error`, else whether `all` played), hands the request back and closes its connection:
 * the rest of a stream that ended early is not read. */
void stream_end(httpd_req_t *req, const char *error, bool all);
