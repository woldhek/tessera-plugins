# Changelog

## 0.4.0 - 2026-10-25
- The voice assistant takes its turn with the screen's other sounds. A doorbell's ring stops the wake word while someone is at the door and lets it listen again after; a question pauses music that plays, and the music stays paused after the answer, as it does for a ring.
- Needs Tessera with plugin API 0.19 (dev); update the screen with it.

## 0.3.0 - 2026-10-20
- A popup over the screen while it listens, thinks and answers, also when the screen was dark or asleep: rings that ripple while it listens, an arc that turns while it thinks, what it heard and the answer word by word as it is spoken. It stays 30 seconds after the answer, with a close key. A new setting, Show popup, turns it off.
- The thinking sound stops as soon as a long answer starts to play, instead of going on over its first words.
- Needs Tessera with plugin API 0.18.

## 0.2.3 - 2026-10-10
- A new wake sound.

## 0.2.2 - 2026-10-10
- New wake and thinking sounds. The thinking sound plays again as soon as it ends, so its own length sets the rhythm.

## 0.2.1 - 2026-10-10
- Its sounds play through Tessera's own sound player; on a screen whose microphone and speaker share one bus, a sound that cannot play while it listens is left out.

## 0.2.0 - 2026-10-10
- A short sound when the screen starts to listen, and a soft plop again and again while Assist thinks, each with a setting to turn it off (Wake sound, Thinking sound).
- In the top bar and on the tile, a microphone that lights up while it listens, dots in amber while it thinks and a speaker while it answers; the top bar item can show its icon alone.
- Needs the screen's speaker as well.

## 0.1.0 - 2026-10-10
- First version: Home Assistant's Assist on the screen, with a wake phrase recognised on the screen, a tile, a top bar item and two settings.
