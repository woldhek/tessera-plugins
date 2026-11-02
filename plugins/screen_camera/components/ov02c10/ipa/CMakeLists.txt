# The OV02C10's image tuning for esp_video's ISP pipeline. esp_cam_sensor has no OV02C10, so nothing hands esp_ipa a
# tuning for it and the pipeline would run without auto exposure or white balance. project_include.cmake beside this
# file adds the tuning to the files esp_ipa compiles into its configuration; the component itself builds nothing.
# components/ov02c10/__init__.py copies this folder into the build as components/ov02c10_ipa.
idf_component_register()
