# An empty esp_h264 for a JPEG-only camera (components/esp_h264_placeholder/__init__.py): esp_video's manifest asks for
# esp_h264 on every ESP32-P4 build although its H.264 code is gated by Kconfig. The video (parts/video.yaml) builds
# without this placeholder and gets the real encoder.
idf_component_register()
