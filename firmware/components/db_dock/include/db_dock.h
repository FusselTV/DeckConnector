#pragma once

#include <stdbool.h>

#include "esp_err.h"

/*
 * Network Dock emulation. Bitfocus Companion and the Elgato Stream Deck app find the bridge over mDNS
 * (_elg._tcp) and drive the attached Stream Deck as if it hung on an Elgato Network Dock. The protocol
 * ("Cora") is the one of @elgato-stream-deck/tcp; see docs/dock.md.
 */

/* `client_changed(true)` when a host opens the device port, false when it leaves. */
esp_err_t db_dock_start(void (*client_changed)(bool connected));
