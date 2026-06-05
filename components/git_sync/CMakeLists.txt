## ESP-IDF 6.0 moved the bundled cJSON ("json") component out of the
## core tree into the espressif/cjson managed component (see the
## ESP-IDF 6.0 protocols migration guide). The managed component
## registers under the name "cjson", so the PRIV_REQUIRES entry must
## switch from "json" (IDF <=5.x built-in) to "cjson" (IDF >=6.0
## managed) depending on the running ESP-IDF version. The matching
## dependency declaration lives in idf_component.yml and is gated on
## the same idf_version >=6.0 rule so older IDFs keep using the
## built-in component without downloading it.
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(_git_sync_cjson_component cjson)
else()
    set(_git_sync_cjson_component json)
endif()

idf_component_register(
    SRCS "git_sync.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_client esp-tls ${_git_sync_cjson_component} sd_card wifi_manager nvs_flash
)
