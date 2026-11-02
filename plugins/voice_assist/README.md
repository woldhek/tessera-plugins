# Voice assistant

Talk to Home Assistant's Assist from the screen. The screen listens for a wake phrase on its own, and only then sends
what you say to Home Assistant; the answer comes from the screen's speaker.

Speech to text, the conversation and text to speech are whatever your Home Assistant uses for Assist: its own
conversation agent with Whisper and Piper, Home Assistant Cloud, or an AI such as OpenAI or Claude. The screen is a
voice satellite in Home Assistant, in the area you give it.

## What you need

- A screen with a microphone and a speaker: a board that brings them (the reTerminal D1001) or a plugin that does.
- An Assist pipeline in Home Assistant (Settings, Voice assistants) with speech to text and text to speech.

## Set up

1. In Tessera, open **Plugins**, choose **Voice assistant** and tick your screens. Each screen builds once with it.
2. In Home Assistant, open the screen's device (Settings, Devices) and choose its Assist pipeline under
   **Assist pipeline**, if it is not the preferred one.
3. Put the tile **Assistant** on a page: it says what the assistant does, and a tap asks a question without the wake
   phrase, or stops one.
4. Optional: add **Assistant** to a page's top bar (Top bar, Add, From plugins). It shows while the assistant listens,
   thinks or answers.

## On the screen

Hold the top bar, then **Settings**, **Plugins**, **Voice assistant**:

- **Wake word**: whether the screen listens for its wake phrase. Off, a tap on the tile still asks.
- **Wake phrase**: Okay Nabu, Hey Jarvis or Hey Mycroft.
- **Wake sound**: a short sound when the screen starts to listen, after the wake phrase or a tap.
- **Thinking sound**: a soft sound, again and again, while Home Assistant works out the answer.
- **Show popup**: a popup over the screen while it listens, thinks and answers (on unless you turn it off). See below.
- **Ask now**: asks a question at once.

The settings are entities of the screen in Home Assistant too ("Wake word", "Wake phrase", "Wake sound", "Thinking
sound", "Show popup"), and the editor shows them in the plugin's details on the screen's Plugins tab. The screen's own **Microphone** switch, on a board that has one,
silences it for the assistant as well.

## The popup

As soon as the screen hears its wake phrase, or you tap the tile, a popup comes over the screen, also when the screen
was dark or asleep. Rings ripple out of the microphone while it listens, an arc turns while Home Assistant thinks, and
under them comes what it heard and then the answer, word by word as the speaker says it; a long answer shows its last
lines. The popup stays 30 seconds after the answer so you can read it again. Its close key, a tap on it or a tap beside
it closes it sooner; while a question goes on, a tap on the popup stops it. It needs Tessera with plugin API 0.18.

The words of the answer come all at once from Home Assistant, at the end of its answer: Home Assistant does not send
an answer in pieces to an ESPHome screen while it is being written. The popup shows them at the pace of speech, in
step with the speaker.

## The sounds

They are two files in `components/voice_assist/sounds/`: `wake.wav` and `thinking.wav` (the screen plays it again as
soon as it ends, so its length sets the rhythm). Each is a WAV file of 16-bit samples, mono, at 48 kHz like the screen's speaker. A plugin of your
own made from this one can swap them; the build says so when a file is not such a WAV.

## Privacy

The wake phrase is recognised on the screen itself; nothing leaves the screen until it is heard or you tap the tile.
After that the question goes to your Home Assistant and on to whatever speech and conversation services its pipeline
uses.
