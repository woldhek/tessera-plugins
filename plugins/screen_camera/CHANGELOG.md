# Changelog

## 0.6.15 - 2026-10-29
- **A sound packet goes whenever the connection has room for it.** 0.6.14 held it back whenever the connection was not
  "writable", which lwIP says only with half of the send buffer free: a fifth of a second of video in flight, normal
  on Wi-Fi, so one to three packets of 20 ms a second were left out and the sound broke into snippets (heard on both
  a reTerminal D1001 and a Guition 10.1 hearing each other). It is now sent without waiting; only a packet the
  connection takes no part of is left out, and a part it did take goes first next time, so the stream stays whole.

## 0.6.14 - 2026-10-29
- **A viewer whose connection is full misses a moment of sound instead of holding everyone up.** The video already left
  such a viewer's pictures behind until a key frame found room (0.6.10); its sound packets still went into send(), which
  waited for the Wi-Fi up to two seconds and held the whole sender with it: every viewer's pictures and sound stopped,
  then came in one burst. Heard as chopped, doubled words on a reTerminal D1001 streaming to a Guition 10.1 while it
  showed the 10.1 full screen (the add-on: sound 0.8 s behind on average, 2.4 s at most). The report counts the packets
  left behind.

## 0.6.13 - 2026-10-29
- **The video's quantiser is capped at 42 again.** 0.6.11 let the encoder use H.264's whole range to stay near its
  1 Mbit/s in a busy picture. Measured, it already does at 42: on a Guition 10.1 with fresh noise over half of every
  frame it sent 983 to 1006 kbit/s at 42 and 1001 to 1002 at 51, and as many pictures were left behind on the
  connection either way. So the setting goes back to what it was.

## 0.6.12 - 2026-10-29
- **The screen slows its own live tiles while someone hears its microphone** (needs Tessera's dev firmware, plugin API
  0.23). Four live tiles on a reTerminal D1001 crowded its Wi-Fi so much that its video's sound reached the viewer in
  bursts a second late; while a viewer has the sound, the screen now keeps its live camera tiles at a quarter of their
  pictures a second, so the conversation goes first.

## 0.6.11 - 2026-10-29
- **The video keeps to its bitrate in a busy picture.** The encoder could make a frame only so coarse (quantiser 42 at
  most), so a picture with much detail or movement came out larger than the 1 Mbit/s the video is meant to send: a
  reTerminal D1001 sent 1.6 to 2.1 Mbit/s, and 30 to 70 frames a minute did not arrive. It may now use H.264's
  whole range (up to 51), so it stays near 1 Mbit/s and a busy moment looks a little softer instead of stalling.

## 0.6.10 - 2026-10-29
- **The sound no longer waits for the picture on a slow connection.** Picture and sound leave the screen over one
  connection, and when the screen's Wi-Fi uplink stalled (a reTerminal D1001 showing four live tiles while its camera
  was watched) a picture waited in the connection for seconds and the sound behind it: it reached Home Assistant in
  bursts a second late, 2.4 s at most, and the screen that played it cut more than half of it away (words gone, bits
  doubled). After 2 s of waiting the viewer was dropped and go2rtc connected anew. Now a picture that finds the
  connection full (more than half of its send buffer waiting) is left behind, the viewer gets a key frame first when
  there is room again, and the sound goes between the packets of a long picture. On the bench, a viewer that stopped
  reading for 3 s of every 6: 0 bytes of sound lost, the viewer kept, 232 pictures left behind in 40 s; before, the
  same viewer was dropped at 2 s. The log's line every minute counts the pictures left behind for a full connection.

## 0.6.9 - 2026-10-28
- **Watching the video no longer trips the wake word.** While someone watched the screen's video in Home Assistant, the
  wake word could lose its place many times an hour (217 an hour on a Guition 10.1 with one viewer). The video's sound
  now steps aside for the wake word: 0 in the same test, the video still at 15 frames a second. The log's Sound line also
  says how long each piece of sound takes to encode.

## 0.6.8 - 2026-10-28
- **The screen's own camera on a tile of its own is drawn on the screen itself** (needs Tessera's dev firmware and app,
  plugin API 0.22): a live camera tile that shows this screen's own camera gets its pictures straight from the camera,
  without the video, Home Assistant or the network in between. The picture is as new as the camera's, and while nobody
  watches the video in Home Assistant the screen's video encoder stays off. Nothing to do; the camera's dot shows while
  the tile is on the glass, as it does while someone watches the video.

## 0.6.7 - 2026-10-28
- **Gives way on a busy screen** (needs Tessera's dev firmware, plugin API 0.21): while a finger is on the glass or
  the screen shows signs of trouble, the video sends every other frame or one in four, and light level and motion are
  sampled half or a quarter as often. Back to full by itself a moment later. Nothing to do.

## 0.6.6 - 2026-10-28
- **Less work while nobody watches** on the Guition screens' camera with Full field of view: the screen halves a
  picture's lines only while someone watches the video. Light level and motion read the picture as the camera makes
  it, so the screen's picture engine and memory are free for the live tiles the rest of the time. Nothing to do.

## 0.6.5 - 2026-10-27
- **Full is the default field of view** on the Guition screens' camera (OV02C10): the whole of what it sees, at half
  the resolution, 15 pictures a second. The two Full modes are one now: the sensor averages each two columns and the
  screen halves the lines, which runs where the sensor's own halving of the lines stopped after one frame. Sharp, the
  middle at full resolution and 30 a second, is still there. A screen whose setting shows Sharp keeps it: choose Full
  once to see the whole room.
- **A brighter picture in a dim room**: auto exposure can now use the sensor's digital gain too, up to 63x instead of
  15.5x. And since 0.6.0 a change of field of view put exposure and gain back to their start while auto exposure kept
  its choice, so the picture could stay dark; they now stay where auto exposure set them.
- **Guition's own image tuning** for the OV02C10 (colours, white balance, black level), for both chip revisions of the
  ESP32-P4.
- **The camera rests while nobody watches**: for the light level and motion, or with the part Video and nothing at
  all, it makes 5 pictures a second (the OV02C10 of the Guition screens and the SC202CS of the reTerminal D1001), and
  goes back to full speed at once when someone opens the video or Home Assistant asks for a picture, slowing down
  again ten seconds after the last of those.
- **The camera dot tells the two apart**: solid while the camera's picture goes somewhere, a ring while it only senses
  the light and motion. This needs Tessera's dev firmware (plugin API 0.20), so this version is on the dev branch until
  Tessera's next release.
- The video keeps 15 pictures a second when the camera makes 15; before, it could skip every other one.

## 0.6.4 - 2026-10-26
- Camera field of view on the Guition: with the lines halved the sensor still stopped after one frame at 960 x 544,
  and the P4 said why: a frame the sensor started and never ended. How many halved lines the sensor's own image
  processor closes a frame with is not in its datasheet, so a Full mode now finds out by itself: it tries a row of
  windows (the height and its offsets) one after the other, each until it runs or stops, keeps the first that runs,
  and starts from it the next time. If none runs, it goes back to Sharp as before. The log says each window it
  tried and which one ran, and the P4's report comes in two lines that are no longer cut off. Testers: switch to
  each Full mode once, wait about a minute, and send all log lines with "Field of view" and a screenshot.

## 0.6.3 - 2026-10-26
- Camera field of view on the Guition 10.1-inch: Jeroen Peters found with his test builds that the Full modes stall
  after one frame whenever the lines are halved, while the columns halved alone keep running. Both Full modes now
  ask for every halved line (960 x 544 instead of 960 x 528). When a Full mode still stalls, the log says what the
  P4's camera input saw (from his test build). Testers: try both Full modes again and send the log lines with
  "Field of view" and a screenshot.

## 0.6.2 - 2026-10-25
- Camera field of view on the Guition 4.3-inch (GitHub issue #326): both Full modes gave no picture there. They now
  start from the table Sharp uses, which leaves starting the camera to the screen; the one before turned the
  camera on halfway through being set up. Same lane rate and timing as before.
- A Full mode whose picture stops in its first ten seconds, or that the camera does not recover from, goes back to
  Sharp by itself now, whatever takes the pictures (the video, a still, light level and motion); before, it could
  stay on Full with no picture until a restart. The log then says what the sensor made of the mode.

## 0.6.1 - 2026-10-25
- Camera field of view on the Guition: the first test showed more of the room, but moved towards the top right
  and with stripes. Both Full modes now keep their window's offsets even and where they are when the picture is
  mirrored or flipped, and ten seconds after a change the log says what the sensor makes of it: its binning, flip
  and mirror, the part of the array it reads, and the size and place of its output. Testers: try both Full modes
  again, and send that log line with a screenshot of each.

## 0.6.0 - 2026-10-25
- A new setting, **Camera field of view**, for the Guition ESP32-P4 screens' camera (OV02C10), to try. **Sharp** is
  the old behaviour, the middle of the camera's picture, and the default. **Full (binning)** and **Full (skip)** show
  the whole of what the camera sees at half the resolution (960 x 528), as OmniVision's datasheet for the sensor
  describes it: binning averages each two pixels and lines, skip reads every other one, so fine patterns (blinds,
  tiles) may shimmer with skip. Neither is proven on a Guition yet. Guition owners with a camera and the part Video:
  switch back to Sharp if anything looks odd, and say for Full which of the two worked, how much of the room you see
  compared with Sharp, the colours, the frames a second (the log says it ten seconds after the change, and whether the
  sensor took the binning), and whether it stays stable over a few minutes. A mode that gives no picture goes back to
  Sharp by itself. Every other camera keeps Sharp (the M5Stack Tab5's and the reTerminal D1001's sensor has no such
  mode we know of). Changing it restarts the camera and the video's encoder at the new size.

## 0.5.1 - 2026-10-25
- The video's server counts its connections (its listener and its three viewers) into the ones the screen's firmware
  allows. With four live camera tiles on the glass beside the video's viewers the screen ran out of them, and a fifth
  camera tile's picture found no connection. Update when it is offered; the screen builds again.

## 0.5.0 - 2026-10-25
- The camera stands upright by itself: on a screen whose board mounts it upside down (the M5Stack Tab5) and on a
  screen turned half way round. The sensor turns its own picture, so the pictures in Home Assistant, the video and the
  light level and motion all follow, at no cost. A new setting, **Camera upside down**, turns it once more for a screen
  mounted the other way round. Needs Tessera's firmware with plugin API 0.19 (dev); update the screen with it.
- The video's line in the log says how long the encoder takes a frame and how much internal RAM it holds.
- Tab5 owners: check that the camera stands upright in Home Assistant, and say so.

## 0.4.3 - 2026-10-24
- Nothing changes on the screen or in Home Assistant: the camera's code is laid out anew for what comes next. The
  camera's frames reach light level and motion and the video through one hub of their own instead of through the
  video, the sensors the camera finds by itself are a table of the plugin's own, and the video's encoder, server and
  sound are each a file. Update when it is offered; the screen builds again.

## 0.4.2 - 2026-10-22
- Builds on an ESPHome build server on Windows. The camera's colour tuning was handed to the build as a Windows path,
  which lost its backslashes on the way, so the build stopped at a file `sc202cs_ipa.json` it could not find.

## 0.4.1 - 2026-10-22
- Video on the Guition ESP32-P4 screens. It asked for exactly 1280 x 720, which their camera (OV02C10) did not give
  (1288 x 728), so Video showed nothing there. That camera now has a 1280 x 720 mode, its first, and Video takes the
  camera's own size on every board: a size that is not in whole macroblocks of 16 is cut to its middle on the way
  (at a cost in frames a second, the log says so), and a camera above 1280 x 800 asks for a smaller mode in the part's
  YAML. No board names a size. Not yet tried on a Guition screen: please say what you see.

## 0.4.0 - 2026-10-21
- **Video**, a part that is on when you add the plugin: the camera as live video with sound in Home Assistant, a camera
  **Video** on the screen's device that plays in the browser and the app, about a third of a second behind. H.264 from
  the screen's video encoder (1280 x 720, 15 frames a second) and the microphone as Opus, in sync. Install Video in
  Home Assistant once under Tessera's Settings (needs Tessera with that card); Tessera then adds each screen by itself.
  With the part the camera runs from the start and **Camera** leaves the device: **Video** gives the stills. Light level
  and motion keep working. A screen that has the plugin already keeps it as it is until you tick Video.

## 0.3.2 - 2026-10-20
- Truer colours on the M5Stack Tab5 and the reTerminal D1001: black is black again. Their camera sent black as a dark
  grey that the screen's chip could not take off, so dark parts of the picture looked pink and the whole picture
  looked faded. The plugin now sets the sensor's own black level.

## 0.3.1 - 2026-10-20
- **Motion wakes the screen** (off to start with) and **Motion time-out** (5 seconds to 10 minutes, 10 to start
  with) are settings now.
- Tessera's own dot in the top right corner of the screen now shows while the camera is in use, also while it senses
  light and motion; the plugin's top bar items **Camera in use** and **Camera senses motion** are gone. Needs Tessera
  with plugin API 0.17.

## 0.3.0 - 2026-10-20
- Sharper pictures: they were compressed far too much (JPEG quality 12 of 100, now 80). Each picture is larger,
  about 150 to 300 KB.
- Also the camera of the Guition ESP32-P4 screens (the OmniVision OV02C10 of the JC8012P4A1, JC1060P470 V2 and
  JC4880P443 in their camera version), on a Tessera whose board names it. Tried on the JC8012P4A1 V3.
- New, off until you switch it on: **Light level** (lux, an estimate) and **Motion** on the screen's device in Home
  Assistant, worked out from the camera on the screen itself: no picture leaves the screen for them. Switch on
  **Light level and motion** in the plugin's settings; the camera then keeps running. Switched off, the light level
  reads unknown.

## 0.2.0 - 2026-10-10
- Also on a board that clocks its camera from a pin, such as the M5Stack Tab5.

## 0.1.0 - 2026-10-10
- First version: the screen's own camera as a camera in Home Assistant, and a camera in the top bar while it is watched.
