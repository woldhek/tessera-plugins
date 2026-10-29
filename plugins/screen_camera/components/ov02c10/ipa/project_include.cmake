# Runs before any component's CMakeLists.txt, so esp_ipa finds the file when it reads the property. Rev3 silicon
# (eco5) corrects the black level in its ISP, older chips do not: each gets its own tuning, as esp_cam_sensor picks
# one per revision for the SC2336 and the OS02N10.
if(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
    idf_build_set_property(ESP_IPA_JSON_CONFIG_FILE_PATH "${CMAKE_CURRENT_LIST_DIR}/ov02c10_p4_eco4.json" APPEND)
else()
    idf_build_set_property(ESP_IPA_JSON_CONFIG_FILE_PATH "${CMAKE_CURRENT_LIST_DIR}/ov02c10_p4_eco5.json" APPEND)
endif()
