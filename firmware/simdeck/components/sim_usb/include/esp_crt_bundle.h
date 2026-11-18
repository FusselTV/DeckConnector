/* No certificate bundle in the simulator: wss:// to a public host fails, ws:// and tests' own TLS work. */
#pragma once
#include "esp_err.h"

static inline esp_err_t esp_crt_bundle_attach(void *conf)
{
    return ESP_FAIL;
}
