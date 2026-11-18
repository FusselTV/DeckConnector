#include "db_driver.h"

#include "drivers/drivers.h"

static const db_driver_t *const db_drivers[] = {
    &db_driver_elgato_gen1,
    &db_driver_elgato_gen2,
    &db_driver_mirabox,
    NULL,
};

const db_driver_t *db_driver_find(uint16_t vid, uint16_t pid)
{
    for (const db_driver_t *const *d = db_drivers; *d; d++) {
        if ((*d)->match(vid, pid)) {
            return *d;
        }
    }
    return NULL;
}
