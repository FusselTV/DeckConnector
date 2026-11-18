# The Matter image: the default firmware plus db_matter. Build: idf.py -C firmware/matter build
cmake_minimum_required(VERSION 3.22)

# esp_matter asks for esp_hosted 2.7 on the P4, the WLAN chip runs 3.0: the manifest goes without both.
set(em ${CMAKE_CURRENT_LIST_DIR}/esp_matter)
if(NOT EXISTS ${em}/idf_component.yml)
    file(DOWNLOAD https://components-file.espressif.com/components/espressif/esp_matter/1.6.1/espressif__esp_matter-v1.6.1.zip
         ${em}.zip EXPECTED_HASH SHA256=b0b6d86c490be2e90864952e0dede0f51c652db9ceb4766a1f1cfe87b70532e0)
    file(ARCHIVE_EXTRACT INPUT ${em}.zip DESTINATION ${em}.tmp)
    file(READ ${em}.tmp/idf_component.yml manifest)
    string(REGEX REPLACE "  espressif/(esp_hosted|esp_wifi_remote):\n    rules:\n[^\n]*\n    version: [^\n]*\n" ""
                         manifest "${manifest}")
    file(WRITE ${em}.tmp/idf_component.yml "${manifest}")
    # CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64
    set(diag ${em}.tmp/connectedhomeip/connectedhomeip/src/platform/ESP32/DiagnosticDataProviderImpl.cpp)
    file(READ ${diag} src)
    string(REPLACE "uint32_t dummyRunTimeCounter;" "configRUN_TIME_COUNTER_TYPE dummyRunTimeCounter;" src "${src}")
    file(WRITE ${diag} "${src}")
    # db_net owns Ethernet: CHIP's commissioning driver would install a second EMAC on its pins and abort
    set(eth ${em}.tmp/connectedhomeip/connectedhomeip/src/platform/ESP32/NetworkCommissioningDriver_Ethernet.cpp)
    file(READ ${eth} src)
    string(REPLACE "    esp_netif_config_t cfg  = ESP_NETIF_DEFAULT_ETH();" "    return CHIP_NO_ERROR;\n    esp_netif_config_t cfg  = ESP_NETIF_DEFAULT_ETH();" src "${src}")
    file(WRITE ${eth} "${src}")
    file(RENAME ${em}.tmp ${em})
    file(REMOVE ${em}.zip)
endif()

set(SDKCONFIG_DEFAULTS ${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components ${CMAKE_CURRENT_LIST_DIR}/../main
                         ${CMAKE_CURRENT_LIST_DIR}/db_matter)
include(../common.cmake)
project(deckconnector)

idf_component_get_property(lib esp_matter COMPONENT_LIB)
target_compile_options(${lib} PRIVATE -Wno-error=stringop-truncation)
# The board resumes a controller's subscription at boot, before it can reach the controller (CASE fails), and the
# default retry follows after 5 min: until the controller's own timeout (~1.5 min) nothing arrived, and the first
# presses were late or lost. 5 s brings the controller back about 12 s after a restart.
target_compile_definitions(${lib} PRIVATE CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_MIN_RETRY_INTERVAL_SECS=5
                                          CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_WAIT_TIME_MULTIPLIER_SECS=5
                                          CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_MAX_RETRY_INTERVAL_SECS=300)
target_compile_definitions(${lib} PRIVATE CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME="DeckConnector"
                                          CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME="DeckConnector")
