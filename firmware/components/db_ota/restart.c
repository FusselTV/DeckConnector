#include "esp_attr.h"
#include "esp_cpu.h"
#include "riscv/rv_utils.h"
#include "sdkconfig.h"
#include "soc/cache_reg.h"
#include "soc/pmu_struct.h"
#include "soc/soc.h"

#define SYNC_BUSY                                                                                            \
    (CACHE_INVALIDATE_ENA | CACHE_CLEAN_ENA | CACHE_WRITEBACK_ENA | CACHE_WRITEBACK_INVALIDATE_ENA)

void __real_esp_restart_noos(void); /* NOLINT: named by the linker's --wrap */

/* esp-idf #19171, open in 6.0.2: esp_restart_noos() resets the other core before it stalls it. Caught in a
 * cache sync, the engine stays busy and the Cache_WriteBack_All() after it faults on CACHE_SYNC_MAP_REG, so
 * the restart ends as a crash. Stall that core and let the engine finish first; IDF's restart then goes on.
 */
void IRAM_ATTR __wrap_esp_restart_noos(void) /* NOLINT: named by the linker's --wrap */
{
    rv_utils_intr_global_disable();
#if !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
    esp_cpu_stall(!esp_cpu_get_core_id());
    /* the stall is a posted write: let it land before the engine counts as idle */
    (void)PMU.cpu_sw_stall.val;
#endif
    /* The ENA bits clear themselves when the operation is done; DONE stays set from the last one. Bounded:
     * the restart must happen even if the engine never reports. */
    for (int i = 0; i < 1000000 && (REG_READ(CACHE_SYNC_CTRL_REG) & SYNC_BUSY); i++) {
    }
    __real_esp_restart_noos();
}
