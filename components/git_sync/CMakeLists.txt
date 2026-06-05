## cJSON is provided by the espressif/cjson managed component
## (ESP-IDF 6.0 moved it out of the core tree -- see the ESP-IDF 6.0
## protocols migration guide). The matching dependency declaration
## lives in idf_component.yml.
idf_component_register(
    SRCS "git_sync.cpp"
    INCLUDE_DIRS "include"
    ## mbedtls is needed for mbedtls_base64_* and for the PSA Crypto
    ## API used by the git blob SHA-1 helper (Mbed TLS 4.x, shipped
    ## with ESP-IDF 6.x, removed the legacy mbedtls_sha1_* API; see
    ## the ESP-IDF 6.0 security migration guide).
    PRIV_REQUIRES esp_http_client esp-tls mbedtls cjson sd_card wifi_manager nvs_flash
)
