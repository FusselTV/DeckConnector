#pragma once

#include <stddef.h>

/* Keep the newest log output in RAM so the web UI can show it. The serial console keeps working. */
void db_log_start(void);
/* Copy the buffered log, oldest line first. Returns the length. */
size_t db_log_read(char *buf, size_t cap);
