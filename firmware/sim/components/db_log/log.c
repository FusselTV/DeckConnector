#include <stdio.h>

#include "db_log.h"
#include "esp_http_server.h"
#include "nvs.h"

size_t db_log_read(char *buf, size_t cap)
{
    return (size_t)snprintf(buf, cap, "simulator\n");
}

size_t db_log_since(uint64_t *pos, char *buf, size_t cap)
{
    size_t n = *pos ? 0 : db_log_read(buf, cap);
    *pos += n;
    return n;
}

size_t db_log_prev(char *buf, size_t cap)
{
    return 0;
}

/* metrics.c reads task and image statistics the host does not have: same keys, no samples, no image. */
void metrics_start(void)
{
}

esp_err_t get_metrics(httpd_req_t *req)
{
    nvs_stats_t nvs = {0};
    nvs_get_stats(NULL, &nvs);
    char body[192];
    snprintf(
        body, sizeof(body),
        "{\"every_s\":5,\"cpu\":[],\"heap\":[],\"psram\":[],\"flash\":{\"firmware\":{\"used\":0,\"size\":%d},"
        "\"settings\":{\"used\":%u,\"size\":%u}}}",
        0x400000, (unsigned)nvs.used_entries * 32, (unsigned)nvs.total_entries * 32);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

esp_err_t get_profile(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"routes\":[]}");
}

void db_log_start(void)
{
}

void db_log_mark(db_mark_t mark)
{
}

int64_t db_log_mark_ms(db_mark_t mark)
{
    return 0;
}

const char *db_log_mark_name(db_mark_t mark)
{
    return "";
}
