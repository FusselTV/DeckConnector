/* What the firmware gets from the chip and has no Linux counterpart. */
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zlib.h>

#include "db_config.h"
#include "esp_private/partition_linux.h"

/* the ROM's miniz; zlib reads the same streams */
size_t tinfl_decompress_mem_to_mem(void *out, size_t out_len, const void *src, size_t src_len, int flags)
{
    uLongf n = out_len;
    return uncompress(out, &n, src, src_len) == Z_OK ? (size_t)n : (size_t)-1;
}

int db_web_temp_c(void)
{
    return 0;
}

esp_err_t __real_db_config_load(void);

/* DB_SIM_FLASH: a flash image that outlives the simulator, as in firmware/sim. Before the first partition opens. */
__attribute__((constructor)) static void flash_file(void)
{
    const char *flash = getenv("DB_SIM_FLASH");
    if (flash) {
        strlcpy(esp_partition_get_file_mmap_ctrl_input()->flash_file_name, flash, PATH_MAX);
    }
}

/* Before anything reads the settings: the password from DB_SIM_PASSWORD, as firmware/sim takes it, no flash
 * file left in /tmp, and a browser that drops a socket mid-write does not end the simulator. */
esp_err_t __wrap_db_config_load(void)
{
    signal(SIGPIPE, SIG_IGN);
    if (!getenv("DB_SIM_FLASH")) {
        unlink(esp_partition_get_file_mmap_ctrl_act()->flash_file_name);
    }
    esp_err_t err = __real_db_config_load();
    db_config_t cfg;
    db_config_copy(&cfg);
    const char *pw = getenv("DB_SIM_PASSWORD");
    strlcpy(cfg.admin_pass, pw ? pw : "simulator", sizeof(cfg.admin_pass));
    db_config_save(&cfg);
    return err;
}
