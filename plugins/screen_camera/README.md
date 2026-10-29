# Screen camera

See the screen's own camera in Home Assistant. The screen becomes a camera of its device there: a picture on a
dashboard, a stream when you open it, and anything Home Assistant does with a camera (a snapshot in an automation, a
recording).

The picture is made on the screen in hardware, only while Home Assistant asks for one. While the camera is in use,
Tessera shows a dot in the top right corner of the screen, so whoever stands in front of it knows; no setting hides it.

## What you need

- A screen whose board has a camera that Tessera knows how to power and reach: the reTerminal D1001, the M5Stack Tab5,
  or a Guition ESP32-P4 screen in its camera version ("C" in the model name): the JC8012P4A1 (also its V2 and V3),
  the JC1060P470 V2 and the JC4880P443. The Guition ones are new and tried on the JC8012P4A1 V3 only.
- Nothing else in Home Assistant: the camera comes with the screen's device in the ESPHome integration.

## Set up

1. In Tessera, open **Plugins**, choose **Screen camera** and tick your screens. Each screen builds once with it.
2. In Home Assistant, open the screen's device (Settings, Devices): its camera is there as **Camera**. Put it on a
   dashboard with a Picture entity or a Picture glance card.
3. The camera stands upright by itself, also on a screen turned half way round. On a screen mounted the other way
   round, switch on **Camera upside down** in the plugin's settings.
4. Optional: switch on **Light level and motion** in the plugin's settings (the screen's Plugins tab in Tessera, or
   **Camera light level and motion** on the device in Home Assistant). Then **Light level** and **Motion** follow the
   room.

## Video

The part **Video** (on when you add the plugin, in the plugin's details on the screen's Plugins tab) makes the camera
live video with sound in Home Assistant: H.264 from the screen's own video encoder, the camera's own size (1280 x 720 on most boards) at 15 frames a second,
and the microphone as Opus, in sync, about a third of a second behind. It plays in Home Assistant's browser page and
app like any camera.

1. Build the screen with the part on.
2. In Tessera, open **Settings, Video in Home Assistant**, press **Install in Home Assistant**, then **Restart Home
   Assistant**. This is once for your whole home.
3. From then on each screen with the part has a camera **Video** on its device in Home Assistant. Tessera adds it by
   itself, and nobody types a password: it is worked out from the screen's API key on both sides.

With the part, the camera runs from the screen's start, and its pictures come from the video: the camera **Camera**
leaves the screen's device, and **Video** gives the stills. Light level and motion keep working. The dot is solid while
someone watches, and the microphone listens only then. The video needs an ESP32-P4 with a microphone; it was tried on
the reTerminal D1001 and the Guition 10.1-inch.

On the Guition screens the setting **Camera field of view** chooses how much of the room the camera shows: **Full**,
the default, all it sees at half the resolution and 15 frames a second; **Sharp** the middle of it at full resolution.

Tessera's docs/CAMERA.md, "The screen's own camera as video in Home Assistant", says how it works and what it takes.

## Light level and motion

Off until you switch it on. While it is on, the camera keeps running and the screen works out two things from it:

- **Light level**, an estimate in lux from the picture's brightness, the exposure time and the sensor's gain. It is right
  in proportion (twice the light reads about twice the lux), not exact: pick the thresholds of your automations by
  looking at what it reads in your room. It updates every ten seconds, and at once when a light goes on or off.
- **Motion**, from the picture compared in a coarse grid a few times a second; on when part of it changes, and for
  **Motion time-out** after (ten seconds to start with, five seconds to ten minutes). A light switched on or off (most of the picture brighter or darker at once) is not motion: for three
  seconds after it, while the camera adjusts, motion is not judged.

No picture is encoded or sent for them: only the two values leave the screen. The dot in the top right corner is a ring
while it runs, and solid once someone watches. While nobody watches, the camera makes 5 pictures a second, enough for
both, and goes back to full speed at once for the video or a picture. With **Motion wakes the screen** on (off to start
with), someone moving in front of the screen wakes it, as a touch does.

## Privacy

Nothing leaves the screen until Home Assistant asks for a picture or the video, and every picture goes only to your
Home Assistant. The video asks for a password made from the screen's own API key.
With light level and motion on, the camera looks all the time, but what it sees stays on the screen.
Everyone who can open the screen's device or a dashboard with its camera can see through it. To stop that for a while,
disable the camera entity in Home Assistant; to stop it for good, remove the plugin from the screen.

## For makers

The camera is ESPHome's camera with id `ts_camera` (the feature `camera`), so another plugin can build on it. The
camera driver is `esp_video_camera` (github.com/n-IA-hane/esphome-esp-video-camera, from ESPHome pull request
esphome/esphome#16944), copied into `components/esp_video_camera` under the ESPHome License, with its NOTICE; Tessera's few changes are marked
"Tessera" in it. Its empty esp_h264 for a JPEG-only build is a component of its own, `components/esp_h264_placeholder`,
which the part Video removes, so that ESPHome cleans the build folder and the video's build finds the real encoder.
The camera's raw frames go to one consumer of the camera, Screen camera's frame hub (`components/screen_camera/frame_hub.h`),
which hands each to the light level and motion and to the video while they are on; a consumer reads a frame through
`frame_reader.h` (`tests/test_frame_reader.cpp` checks it on a PC). The sensors the camera finds by itself, and the
SC202CS's colour tuning, are the table in `components/screen_camera/sensors.py`: a sensor Espressif's camera library
drives is a row there. What a sensor's driver can do beyond its format, a slower frame rate while the camera only senses
and a mode over its whole field of view, it tells the camera through `components/esp_video_camera/camera_sensor.h`,
whichever board it is on. The video is `components/screen_video`: the frame path and Espressif's esp_h264 in
`video_encoder.cpp`, the RTSP server in `rtsp_server.cpp`, the microphone as Opus in `opus_sound.cpp`.
The OmniVision OV02C10 of the Guition screens, which Espressif's camera library has no driver for, is a component of its
own beside it, `components/ov02c10`: the sensor driver and its image tuning (one for rev3 chips, one for older ones),
with a NOTICE that says where they come from. The SmartSens SC202CS (SC2356) of the Tab5 and the reTerminal has a small
component of its own too, `components/sc202cs`: Espressif's driver starts it with a black level of 16 of 255 for the ISP
to take off, which an ESP32-P4 before rev3 cannot, so the component lowers the sensor's own black level target after the
camera has started it.
