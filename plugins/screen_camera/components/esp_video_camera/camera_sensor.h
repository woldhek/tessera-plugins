#pragma once
// What a camera sensor's driver offers the camera beyond esp_cam_sensor's own calls, whichever sensor a board has: the
// camera knows sensors only through this, and a sensor that offers nothing keeps doing what its format says. A board
// names no sensor (it is found at boot); a driver that finds its sensor adds itself here (tessera_camera_add_sensor):
// the OV02C10 in its detect (ov02c10_sensor.c), the SC202CS from Screen camera's small component once it read its id
// (sc202cs.cpp). Plain C, for a driver in C. A field a driver leaves NULL is a thing it cannot do.
#include <stddef.h>

#include "esp_cam_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tessera_camera_sensor {
  const char *name;
  // From now on the sensor makes `fps` frames a second, or as near to it as it can without going faster than its
  // format's own; 0 is its format's own again. Called on the camera's task between two frames, after the format is
  // set. Returns the frames a second it set, 0 when it cannot change them while it streams.
  int (*set_fps)(int fps);
  // What the sensor holds now, read back (its frame length and exposure), for the camera's log a second after a change
  // of frame rate: a sensor takes a new length at a frame's end, and a driver's own log does not reach a screen's
  // (ESP-IDF's level stays at errors). The length written into `out`.
  int (*frame_report)(char *out, size_t size);
  // The sensor's format for a field of view other than its own format's (esp_video_camera.h, set_field): 1 is the whole
  // field the sensor sees (Screen camera's "Full"). NULL for a field it has no mode for. Called on the camera's task
  // right before the camera hands the format to the sensor; the format outlives the camera.
  const esp_cam_sensor_format_t *(*field_format)(int field);
  // What the sensor says it does in that mode, for the log ten seconds after a change or when it fails: the length
  // written into `out`.
  int (*field_report)(char *out, size_t size);
} tessera_camera_sensor_t;

// A driver that found its sensor adds it (once; the table outlives the call).
void tessera_camera_add_sensor(const tessera_camera_sensor_t *sensor);

#ifdef __cplusplus
}
#endif
