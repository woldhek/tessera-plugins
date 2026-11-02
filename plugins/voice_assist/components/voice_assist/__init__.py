"""Voice assistant: Home Assistant's Assist on the screen, its state on a tile and in the top bar.

The wake word (micro_wake_word), the Assist pipeline (voice_assistant) and the settings are ESPHome components of
plugin.yaml; this component shows what they do (on a tile, in the top bar and in a popup while a question goes on), takes a tap,
and plays its two sounds on the screen's speaker
(ts_speaker): sounds/wake.wav when it listens, sounds/thinking.wav again and again while Assist thinks. A sound is a
WAV file of 16-bit samples, mono or stereo, best at the screen's own rate (48 kHz); swap the file to change it.
register_plugin() takes the id, version and the screen's texts from the manifest and translations/ beside this folder.
"""
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import media_player, micro_wake_word, select, smart_display, speaker, switch, voice_assistant
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display", "voice_assistant", "micro_wake_word"]

CONF_ASSISTANT = "assistant"
CONF_WAKE_WORD = "wake_word"
CONF_WAKE_WORD_ON = "wake_word_on"
CONF_WAKE_PHRASE = "wake_phrase"
CONF_PHRASES = "phrases"
CONF_SPEAKER = "speaker"
CONF_MEDIA_PLAYER = "media_player"
CONF_WAKE_SOUND_ON = "wake_sound_on"
CONF_THINKING_SOUND_ON = "thinking_sound_on"
CONF_SHOW_POPUP_ON = "show_popup_on"

SOUNDS = Path(__file__).parent / "sounds"
SOUND_NAMES = ("wake", "thinking")


def check_sounds(config):
    for name in SOUND_NAMES:
        smart_display.read_sound(SOUNDS / f"{name}.wav")
    return config

voice_assist_ns = cg.esphome_ns.namespace("voice_assist")
VoiceAssist = voice_assist_ns.class_("VoiceAssist", cg.Component)

CONFIG_SCHEMA = cv.All(cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VoiceAssist),
        cv.Required(CONF_ASSISTANT): cv.use_id(voice_assistant.VoiceAssistant),
        cv.Required(CONF_WAKE_WORD): cv.use_id(micro_wake_word.MicroWakeWord),
        cv.Required(CONF_WAKE_WORD_ON): cv.use_id(switch.Switch),
        cv.Required(CONF_WAKE_PHRASE): cv.use_id(select.Select),
        # The wake word models in the order of Wake phrase's options.
        cv.Required(CONF_PHRASES): cv.All(cv.ensure_list(cv.use_id(micro_wake_word.WakeWordModel)), cv.Length(min=1)),
        # The speaker its sounds play on, and the two settings that turn them off.
        cv.Required(CONF_SPEAKER): cv.use_id(speaker.Speaker),
        # The media player the answer plays on: the popup's words follow from when it announces it.
        cv.Optional(CONF_MEDIA_PLAYER): cv.use_id(media_player.MediaPlayer),
        cv.Required(CONF_WAKE_SOUND_ON): cv.use_id(switch.Switch),
        cv.Required(CONF_THINKING_SOUND_ON): cv.use_id(switch.Switch),
        # Whether a question shows its popup (on unless someone turns it off).
        cv.Optional(CONF_SHOW_POPUP_ON): cv.use_id(switch.Switch),
    }
).extend(cv.COMPONENT_SCHEMA), check_sounds)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    cg.add(var.set_assistant(await cg.get_variable(config[CONF_ASSISTANT])))
    cg.add(var.set_wake_word(await cg.get_variable(config[CONF_WAKE_WORD])))
    cg.add(var.set_wake_word_on(await cg.get_variable(config[CONF_WAKE_WORD_ON])))
    cg.add(var.set_wake_phrase(await cg.get_variable(config[CONF_WAKE_PHRASE])))
    for phrase in config[CONF_PHRASES]:
        cg.add(var.add_phrase(await cg.get_variable(phrase)))
    cg.add(var.set_speaker(await cg.get_variable(config[CONF_SPEAKER])))
    if CONF_MEDIA_PLAYER in config:
        cg.add(var.set_media_player(await cg.get_variable(config[CONF_MEDIA_PLAYER])))
    cg.add(var.set_wake_sound_on(await cg.get_variable(config[CONF_WAKE_SOUND_ON])))
    cg.add(var.set_thinking_sound_on(await cg.get_variable(config[CONF_THINKING_SOUND_ON])))
    if CONF_SHOW_POPUP_ON in config:
        cg.add(var.set_show_popup_on(await cg.get_variable(config[CONF_SHOW_POPUP_ON])))
    # The sounds in flash, as the samples of their WAV files (plugin_sound.h).
    cg.add(var.set_wake_sound(smart_display.sound(config[CONF_ID], "wake", SOUNDS / "wake.wav")))
    cg.add(var.set_thinking_sound(smart_display.sound(config[CONF_ID], "thinking", SOUNDS / "thinking.wav")))
