#!/usr/bin/env python3
"""The plugin bench's engine: a plugin on the real firmware of every screen it may go on, built for this computer, with
everything it draws photographed and checked. Run it through tools/preview.py, which sets it up (docs/PREVIEW.md).

For each screen (a board of Tessera's catalog lying down or standing up, or a glass of any size and density) and each
grid asked for, it shows every tile of the plugin at every size it offers, light and dark, the card a tap on each
opens, every card of the manifest opened by its id, its top bar items with words and as icons, its screensavers in
standby (plugin API 0.14) with the sample data of their step, and its pages on the settings page. It checks what it draws (a part outside its tile, a label cut with dots, a character its font lacks, a
word too faint to read), answers the plugin's questions to Home Assistant and the states its screen reads from
preview.yaml, and writes index.html, overview.png and summary.json into <plugin>/.preview/.

It runs on a copy of Tessera (TESSERA_TREE, which tools/preview.py fetches): the firmware the screens run, built for
this computer by Tessera's own host build (tools/render/host.py) and driven by its render harness (tools/render/run.py),
and the tiles' data shaped by the app's own code (plugins.Plugins.tile_message). What the bench adds to the screen, its
actions, comes with every build of it (BENCH_ACTIONS); it changes nothing in Tessera.
"""
import argparse
import asyncio
import dataclasses
import html
import json
import math
import os
import re
import shlex
import sys
import time
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

import logging

import yaml
from PIL import Image, ImageDraw

# The API driver says every refused try out loud; the bench says itself what went wrong.
logging.getLogger('aioesphomeapi').setLevel(logging.CRITICAL)

HERE = Path(__file__).resolve().parent
if not os.environ.get('TESSERA_TREE'):
    raise SystemExit('bench.py runs through tools/preview.py, which names the copy of Tessera it builds with')
REPO = Path(os.environ['TESSERA_TREE']).resolve()   # Tessera's repository: the firmware and the app
sys.path.insert(0, str(REPO / 'tools' / 'render'))
sys.path.insert(0, str(REPO / 'tools'))
import host  # noqa: E402
import run  # noqa: E402  (puts diagnostics/ and screen_manager/app/ on the path)
import send_layout  # noqa: E402
import core  # noqa: E402
import plugin_fetch  # noqa: E402
import plugins as app_plugins  # noqa: E402
import profiles  # noqa: E402
import propose_grid  # noqa: E402

# What the bench asks of the screen besides the harness's own actions (tools/render/host.py): a plugin's settings page,
# where a tile lies, any card of the plugin opened by its id, what a top bar item says, and an audit of what the
# plugin drew. The audit walks every object of each plugin tile on the glass (or, with `card`, of the card that is
# open, and with `saver`, of the plugin's screensaver in standby) and counts what a person would see wrong: a part outside its tile (or the glass), a label cut with dots, a
# character its font lacks, a word too faint to read on its ground.
BENCH_ACTIONS = '''    - action: render_settings
      variables:
        page: int
      then:
        - lambda: |-
            // The settings page at a plugin's page: 0 the list of plugins, then each plugin's own; below 0 closes it.
            if (page < 0) settings_screen::close();
            else if ((size_t) page < settings_screen::plugin_pages.size()) settings_screen::open(settings_screen::PLUGINS_PAGE + page);
            ESP_LOGI("render", "settings pages=%u", (unsigned) settings_screen::plugin_pages.size());
    - action: render_tile_box
      variables:
        index: int
      then:
        - lambda: |-
            lv_obj_update_layout(lv_screen_active());
            lv_area_t a{{0, 0, -2, -2}};
            for (auto &w : runtime_tiles::widgets)
              if (w.tile && w.index == (size_t) index && !lv_obj_has_flag(w.tile, LV_OBJ_FLAG_HIDDEN)) lv_obj_get_coords(w.tile, &a);
            ESP_LOGI("render", "tile_box index=%d box=%d,%d,%d,%d", index, (int) a.x1, (int) a.y1, (int) a.x2, (int) a.y2);
    - action: render_open_card
      variables:
        card: string
        entity: string
        title: string
      then:
        - lambda: |-
            const bool opened = tessera::open_card("{plugin}", card.c_str(), entity, -1, title);
            ESP_LOGI("render", "card %s opened=%d", card.c_str(), (int) opened);
    - action: render_bar_item
      variables:
        key: string
      then:
        - lambda: |-
            const auto shown = header_bar::plugin_item ? header_bar::plugin_item(key) : header_bar::Shown{{}};
            ESP_LOGI("render", "bar item %s shown=%d icon=%X text=[%s]", key.c_str(), (int) shown.shown, (unsigned) shown.icon,
                     shown.text.c_str());
    - action: render_plugin_audit
      variables:
        card: bool
        saver: bool
      then:
        - lambda: |-
            lv_obj_update_layout(lv_screen_active());
            const int sw = lv_display_get_horizontal_resolution(lv_display_get_default()),
                      sh = lv_display_get_vertical_resolution(lv_display_get_default());
            // A part that shows: a label with words, or a block with a ground of its own (a bar, a badge, a dot).
            struct Part {{ lv_obj_t *o; lv_area_t a; bool label; std::string text; }};
            struct Count {{ int objects = 0, outside = 0, cut = 0, glyph = 0, faint = 0, clipped = 0, overlap = 0; std::string notes;
                           std::vector<Part> parts; }};
            // A label's ink against the ground it stands on (the nearest opaque background up its parents), as the WCAG
            // contrast ratio; below 2.5 a person cannot read it, a colour left behind by a change of look most often.
            auto luminance = [](lv_color_t c) {{
              auto channel = [](uint8_t v) {{ const float s = v / 255.0f; return s <= 0.03928f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f); }};
              return 0.2126f * channel(c.red) + 0.7152f * channel(c.green) + 0.0722f * channel(c.blue);
            }};
            // A screensaver stands on the black glass, or on the step's own picture under a clear glass (`backdrop`),
            // which the screensaver darkens or which the saver lays a shade over: the ground is the glass's black there.
            auto ground = [&](lv_obj_t *o) {{
              for (lv_obj_t *p = lv_obj_get_parent(o); p; p = lv_obj_get_parent(p))
                if (lv_obj_get_style_bg_opa(p, LV_PART_MAIN) >= LV_OPA_80) return lv_obj_get_style_bg_color(p, LV_PART_MAIN);
              return theme::color(saver ? theme::CAMERA_PAGE : theme::PAGE);
            }};
            std::function<void(lv_obj_t *, const lv_area_t &, Count &)> walk = [&](lv_obj_t *o, const lv_area_t &room, Count &n) {{
              if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
              ++n.objects;
              lv_area_t a; lv_obj_get_coords(o, &a);
              const bool label = lv_obj_check_type(o, &lv_label_class);
              const std::string text = label ? lv_label_get_text(o) : "";
              auto note = [&](const char *what) {{
                if (n.notes.size() < 160) n.notes += std::string(what) + (text.empty() ? "" : "[" + text.substr(0, 24) + "]") + ";";
              }};
              if (a.x2 >= a.x1 && a.y2 >= a.y1 && (a.x1 < room.x1 - 1 || a.y1 < room.y1 - 1 || a.x2 > room.x2 + 1 || a.y2 > room.y2 + 1)) {{
                ++n.outside; note("outside");
              }}
              if (label && !text.empty() && lv_obj_get_style_text_opa(o, LV_PART_MAIN) >= LV_OPA_50 && lv_obj_get_style_opa(o, LV_PART_MAIN) >= LV_OPA_50)
                n.parts.push_back({{o, a, true, text}});
              else if (!label && lv_obj_get_style_bg_opa(o, LV_PART_MAIN) >= LV_OPA_50 && lv_obj_get_style_opa(o, LV_PART_MAIN) >= LV_OPA_50)
                n.parts.push_back({{o, a, false, ""}});
              if (label && !text.empty()) {{
                const lv_font_t *face = lv_obj_get_style_text_font(o, LV_PART_MAIN);
                // Lower than four fifths of its font's line: the letters are cut off at the top or the bottom.
                if (face && lv_obj_get_height(o) * 5 < lv_font_get_line_height(face) * 4) {{ ++n.clipped; note("clipped"); }}
                if (text.size() >= 3 && text.compare(text.size() - 3, 3, "\\u2026") == 0) {{ ++n.cut; note("cut"); }}
                else if (face && !runtime_tiles::face_covers(face, text)) {{ ++n.glyph; note("glyph"); }}
                if (lv_obj_get_style_text_opa(o, LV_PART_MAIN) >= LV_OPA_50) {{
                  const float a = luminance(lv_obj_get_style_text_color(o, LV_PART_MAIN)), b = luminance(ground(o));
                  if ((std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f) < 2.5f) {{ ++n.faint; note("faint"); }}
                }}
              }}
              for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) walk(lv_obj_get_child(o, i), room, n);
            }};
            // Two parts over each other where neither holds the other: words under a bar, or two lines on one place.
            auto overlaps = [&](Count &n) {{
              auto holds = [](lv_obj_t *outer, lv_obj_t *inner) {{
                for (lv_obj_t *p = lv_obj_get_parent(inner); p; p = lv_obj_get_parent(p)) if (p == outer) return true;
                return false;
              }};
              for (size_t i = 0; i < n.parts.size(); ++i)
                for (size_t j = i + 1; j < n.parts.size(); ++j) {{
                  const Part &x = n.parts[i], &y = n.parts[j];
                  if (!x.label && !y.label) continue;
                  if (holds(x.o, y.o) || holds(y.o, x.o)) continue;
                  const int w = std::min(x.a.x2, y.a.x2) - std::max(x.a.x1, y.a.x1), h = std::min(x.a.y2, y.a.y2) - std::max(x.a.y1, y.a.y1);
                  if (w > 2 && h > 2) {{
                    ++n.overlap;
                    if (n.notes.size() < 160) n.notes += "overlap[" + (x.label ? x.text : y.text).substr(0, 24) + "]" + (x.label && y.label ? "[" + y.text.substr(0, 24) + "]" : std::string("[a block]")) + ";";
                  }}
                }}
            }};
            if (saver) {{
              // A plugin's screensaver (plugin API 0.14): its parts in the stage the host gave it (a child of the black
              // glass runtime_tiles laid on the top layer), whose area is the saver's, wherever the drift has moved it.
              Count n;
              lv_obj_t *root = runtime_tiles::saver_root;
              for (uint32_t s = 0; root && s < lv_obj_get_child_count(root); ++s) {{
                lv_obj_t *stage = lv_obj_get_child(root, s);
                lv_area_t room; lv_obj_get_coords(stage, &room);
                for (uint32_t i = 0; i < lv_obj_get_child_count(stage); ++i) walk(lv_obj_get_child(stage, i), room, n);
              }}
              overlaps(n);
              ESP_LOGI("render", "audit saver objects=%d outside=%d cut=%d glyph=%d faint=%d clipped=%d overlap=%d notes=%s", n.objects,
                       n.outside, n.cut, n.glyph, n.faint, n.clipped, n.overlap, n.notes.c_str());
              return;
            }}
            if (card) {{
              Count n; const lv_area_t glass{{0, 0, (int32_t) (sw - 1), (int32_t) (sh - 1)}};
              // The card's root is the newest object on the screen (plugin_host::open_card).
              lv_obj_t *screen = lv_screen_active();
              const uint32_t count = lv_obj_get_child_count(screen);
              if (count) walk(lv_obj_get_child(screen, count - 1), glass, n);
              overlaps(n);
              ESP_LOGI("render", "audit card objects=%d outside=%d cut=%d glyph=%d faint=%d clipped=%d overlap=%d notes=%s", n.objects,
                       n.outside, n.cut, n.glyph, n.faint, n.clipped, n.overlap, n.notes.c_str());
              return;
            }}
            for (auto &w : runtime_tiles::widgets) {{
              if (!w.plugin || !w.tile || lv_obj_has_flag(w.tile, LV_OBJ_FLAG_HIDDEN)) continue;
              Count n; lv_area_t room; lv_obj_get_coords(w.tile, &room);
              for (uint32_t i = 0; i < lv_obj_get_child_count(w.tile); ++i) walk(lv_obj_get_child(w.tile, i), room, n);
              overlaps(n);
              ESP_LOGI("render", "audit tile=%u objects=%d outside=%d cut=%d glyph=%d faint=%d clipped=%d overlap=%d notes=%s",
                       (unsigned) w.index, n.objects, n.outside, n.cut, n.glyph, n.faint, n.clipped, n.overlap, n.notes.c_str());
            }}
            ESP_LOGI("render", "audit end");
'''

class BenchBuild(host.Build):
    """Tessera's host build of a board with the plugin in it, the bench's actions beside the harness's own, and for a
    glass no board has the board's sizes, density, look and grids replaced in its mirror (`glass`: {'PANEL_W': '800',
    ..., 'look': 'compact'})."""

    def __init__(self, item, glass=None, window=False, **kw):
        super().__init__(item, **kw)
        self.glass, self.window = dict(glass or {}), window

    def package(self):
        text = super().package()
        if self.window:
            # Tessera's host build draws without a window (set_headless at boot, for renders on a machine without a
            # display); --window keeps the window, so a person can tap and swipe it with the mouse.
            core_yaml = self.work / 'host' / self.variant.key / 'packages' / 'core.yaml'
            body = core_yaml.read_text()
            if 'id(my_display).set_headless(true);' not in body:
                raise SystemExit('Tessera\'s host build changed how it draws without a window; --window needs an update of tools/bench.py')
            core_yaml.write_text(body.replace('id(my_display).set_headless(true);', 'id(my_display).set_headless(false);'))
        if self.glass:
            item, tree = self.variant, self.tree
            mirror = self.work / 'host' / item.key / 'packages'
            mirrored = mirror / 'boards' / item.file
            board = mirrored.read_text()
            for key, value in self.glass.items():
                if key == 'look':
                    board, found = re.subn(r'(?m)^(  look: !include \.\./looks/)\w+(\.yaml)', rf'\g<1>{value}\2', board)
                else:
                    board, found = re.subn(rf'(?m)^  {key}: .*$', f'  {key}: "{value}"', board)
                    if not found:
                        board, found = re.subn(r'(?m)^substitutions:\n', f'substitutions:\n  {key}: "{value}"\n', board, count=1)
                if not found:
                    raise SystemExit(f'{item.file}: no {key} to give another glass')
            mirrored.write_text(board)
            # The chain as host.Build walked it (its displays, touch and outputs), for the SDL stand-ins at the new size.
            chain = host.Chain()
            scratch = self.work / 'host' / f'{item.key}-walk'
            host.walk(tree, tree / 'packages' / 'boards' / item.file, scratch, chain)
            if 'look' in self.glass:
                host.walk(tree, tree / 'packages' / 'looks' / f'{self.glass["look"]}.yaml', mirror, host.Chain())
            (self.work / 'host' / item.key / 'host-hw.yaml').write_text(host.host_hw(board, chain))
        # The plugin's id from its manifest, not its folder's name: a plugin in a repository of its own lives in a
        # folder named after the repository (tessera-something), and its cards are registered under its id.
        plugin_id = yaml.safe_load((self.plugin / 'tessera-plugin.yaml').read_text())['id']
        return text + BENCH_ACTIONS.format(plugin=plugin_id)


def sample_photo(width, height):
    """A photo for the photos step when preview.yaml names none: a sky, a sun and hills, the glass's shape, as PNG."""
    import io
    width, height = max(64, width), max(64, height)
    image = Image.new('RGB', (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / height
        draw.line((0, y, width, y), fill=(round(70 + 150 * t), round(130 + 80 * t), round(210 - 40 * t)))
    sun = min(width, height) // 7
    draw.ellipse((width * 0.68 - sun, height * 0.3 - sun, width * 0.68 + sun, height * 0.3 + sun), fill=(255, 226, 150))
    draw.polygon([(0, height * 0.72), (width * 0.3, height * 0.55), (width * 0.6, height * 0.7), (width, height * 0.58),
                  (width, height), (0, height)], fill=(62, 110, 72))
    draw.polygon([(0, height * 0.86), (width * 0.45, height * 0.74), (width, height * 0.88), (width, height), (0, height)],
                 fill=(40, 78, 52))
    out = io.BytesIO()
    image.save(out, 'PNG')
    return out.getvalue()


def answer_of(entry, ask, result, zone=None):
    """What a screen gets of Home Assistant's answer to a plugin's command, as the app sends it (Plugins.answer): the
    fields of the manifest's `answers` when it maps that command, else the answer as it came; both bounded."""
    spec = next((a for a in entry.manifest.get('answers') or [] if a['command'] == ask), None)
    if spec:
        result = {name: plugin_fetch.field_value(result, {**field, 'tz': field.get('tz') or zone})
                  for name, field in spec['fields'].items()}
    return app_plugins.bounded(result)


# Small, middle and large glass (tools/profiles.RENDER_SAMPLE) and one standing up: the screens a bench builds unless it
# is told others.
DEFAULT_BOARDS = ('cyd', 'guition', 'waveshare43-portrait', 'jc8012p4a1')
# A glass no board has is built on the Guition's chain with its sizes, density, look and grids replaced.
CUSTOM_BASE = 'guition'
PORT_OFFSET = 200   # beside run.py's plugin builds (+100), so the two never meet on one port
REGION = dict(keepalive=120, clock_24h=True, numbers='point', group_min=1, percent_space=False)
SETTINGS = re.compile(r'settings pages=(\d+)')
AUDIT = re.compile(r'audit (?:tile=(\d+)|card|saver) objects=(\d+) outside=(\d+) cut=(\d+) glyph=(\d+) faint=(\d+) clipped=(\d+) overlap=(\d+) notes=(.*)$')
BOX = re.compile(r'box=(-?\d+),(-?\d+),(-?\d+),(-?\d+)')
ZONE = 'Europe/Amsterdam'
SHOWN = re.compile(r'[^a-z0-9_.-]+')
# A plugin's screensaver (plugin API 0.14, docs/PREVIEW.md "Screensavers"): the step's own picture, the line the screen
# logs when a saver comes on the glass, and what each step draws from when preview.yaml gives nothing (the contract of
# docs/FIRMWARE_API.md, "A screensaver"). Home Assistant's MediaPlayerEntityFeature: PAUSE 1, VOLUME_SET 4,
# VOLUME_MUTE 8, PREVIOUS_TRACK 16, NEXT_TRACK 32, VOLUME_STEP 1024, PLAY 16384.
SAVER_PICTURE = 'step:'
SAVER_SHOWN = re.compile(r'screensaver (plugin:\S+) \(\w+\) on the glass')
SAVER_STILL_SECONDS = 120
STEP_SAMPLES = {
    'clock': {'temperature': '21°', 'items': []},
    'media': {'entity': 'media_player.living_room', 'name': 'Living room', 'title': 'Evening Drive', 'artist': 'Nova Coast',
              'album': 'Coastlines', 'state': 'playing', 'features': 1 | 4 | 8 | 16 | 32 | 1024 | 16384, 'muted': False,
              'volume': 0.4, 'picture': SAVER_PICTURE, 'mark': 'c0ffee1234', 'colour': '#35506b'},
    'photos': {'picture': SAVER_PICTURE, 'mark': 'a1b2c3d4e5', 'index': 3, 'count': 24, 'album': 'Summer'},
}


# ---- the screens

@dataclasses.dataclass
class Screen:
    """One screen of the bench: what it is built from and what its glass is."""
    key: str                 # 'guition', 'waveshare43-portrait', 'custom-1280x800-149'
    variant: host.Variant
    title: str               # 'Guition 4.0 in, lying down'
    width: int
    height: int
    dpi: int
    look: str
    columns: int
    rows: int
    glass: dict = dataclasses.field(default_factory=dict)   # host.Build's substitutions for a glass no board has


def board_screen(key, index):
    item = host.variant(key)
    shape = json.loads((REPO / 'screen_manager/app/boards.json').read_text())[item.board]
    side = shape['orientations']['portrait' if item.rotation else 'landscape']
    catalog = shape['catalog']
    title = f'{catalog["name"]} {catalog["inch"]:g} in, {"standing up" if item.rotation else "lying down"}'
    return Screen(key, dataclasses.replace(item, key=f'{item.key}-pv', port=item.port + PORT_OFFSET), title,
                  side['width'], side['height'], shape['dpi'], shape['look'], side['columns'], side['rows'])


GLASS = re.compile(r'^(\d+)x(\d+)(?:@(\d+(?:\.\d+)?)|:(\d+))(?:/(standard|compact))?$')


def custom_screen(text, index):
    """A glass given as WIDTHxHEIGHT@INCHES or WIDTHxHEIGHT:DPI (lying as given), with /standard or /compact to choose
    the look; the grid is the one tools/propose_grid.py proposes, as for a new board."""
    found = GLASS.match(text.strip())
    if not found:
        raise SystemExit(f'--screen {text}: as 800x480@7 (pixels and inches) or 800x480:133 (pixels and dpi), '
                         'optionally with /standard or /compact')
    width, height = int(found[1]), int(found[2])
    inch = float(found[3]) if found[3] else math.hypot(width, height) / int(found[4])
    proposal = propose_grid.propose(width, height, inch, found[5])
    base = host.variant(CUSTOM_BASE)
    key = f'custom-{width}x{height}-{proposal["dpi"]}'
    glass = {'PANEL_W': width, 'PANEL_H': height, 'DISPLAY_DPI': proposal['dpi'], 'GRID_COLS': proposal['cols'],
             'GRID_ROWS': proposal['rows'], 'GRID_COLS_PORTRAIT': proposal['cols'], 'GRID_ROWS_PORTRAIT': proposal['rows'],
             'look': proposal['look']}
    variant = dataclasses.replace(base, key=f'{key}-pv', port=host.PORT_BASE + PORT_OFFSET + 150 + index)
    return Screen(key, variant, f'Your glass {width} x {height}, {inch:.1f} in', width, height, proposal['dpi'],
                  proposal['look'], proposal['cols'], proposal['rows'], {k: str(v) for k, v in glass.items()})


def all_screens():
    return [variant.key for variant in host.variants()]


# ---- preview.yaml

MOMENT = re.compile(r'\{(now|epoch)(?:([+-]\d+)([smhd]))?\}')
UNITS = {'s': 1, 'm': 60, 'h': 3600, 'd': 86400}


def moments(value, zone, now):
    """preview.yaml's moments made from the time of the render, so a countdown never runs out: "{now+5m}" is the local
    time in five minutes as ISO text (2026-10-11T15:20:00), "{epoch-2h}" two hours ago in seconds since 1970 (a number
    when it is the whole text)."""
    def at(match):
        return now + (int(match[2]) * UNITS[match[3]] if match[2] else 0)

    def word(match):
        moment = at(match)
        return str(int(moment)) if match[1] == 'epoch' else datetime.fromtimestamp(moment, ZoneInfo(zone)).strftime('%Y-%m-%dT%H:%M:%S')
    if isinstance(value, str):
        whole = MOMENT.fullmatch(value)
        if whole and whole[1] == 'epoch':
            return int(at(whole))
        return MOMENT.sub(word, value)
    if isinstance(value, list):
        return [moments(v, zone, now) for v in value]
    if isinstance(value, dict):
        return {k: moments(v, zone, now) for k, v in value.items()}
    return value


def read_preview(folder):
    """The plugin's preview.yaml (docs/PREVIEW.md in the plugins repository), or an empty one."""
    path = folder / 'preview.yaml'
    if not path.is_file():
        return {}
    spec = yaml.safe_load(path.read_text(encoding='utf-8')) or {}
    if not isinstance(spec, dict) or set(spec) - {'tiles', 'answers', 'states', 'commands', 'cards', 'time_zone', 'pictures', 'savers'}:
        raise SystemExit(f'{path}: a mapping of tiles, answers, states, commands, cards, savers, pictures and time_zone (docs/PREVIEW.md)')
    return spec


# ---- what the plugin adds, from its manifest and its code

CODE = {
    'tiles': re.compile(r'\badd_tile\(\s*"(\w+)"'),
    'cards': re.compile(r'\badd_card\(\s*"(\w+)"'),
    'bar_items': re.compile(r'\badd_bar_item\(\s*"(\w+)"'),
    'tap_actions': re.compile(r'\badd_tap_action\(\s*"(\w+)"'),
    'savers': re.compile(r'\badd_saver\(\s*"(\w+)"'),
}
SETTINGS_ROWS = re.compile(r'\bbool\s+(?:\w+::)?settings\s*\(\s*(?:tessera::)?SettingsPage\s*&')
ACTION = re.compile(r'tessera::action\(\s*"([\w.]+)"')
API_0_9 = re.compile(r'Font::DIGITS|ui::covers\(|Tone::GOOD|ui::set_fill\(|ui::tone_color\(')
# The parts of a screensaver (plugin API 0.14), as tools/check.py's API_PARTS names them.
API_0_14 = re.compile(r'\badd_saver\s*\(|\bSaver\b|\bSaverContext\b|\bSaverStep\b|\bui::takes_taps\s*\(|\btessera::wish\s*\(|\bWish::')


def adds(entry, folder):
    """What the plugin adds, as its manifest says it and as its code registers it, and where the two differ."""
    code = '\n'.join(path.read_text(encoding='utf-8', errors='replace')
                     for path in sorted((folder / 'components').rglob('*')) if path.suffix in ('.h', '.cpp', '.hpp', '.c'))
    manifest = entry.manifest
    out = {'tiles': [{'id': k['id'], 'name': entry.text(k['name']), 'sizes': f'{k["min"]} to {k["max"]}' + (', and the whole page' if k.get('full') else ''),
                      'memory': k['memory'], 'data': 'a fetch' if k.get('data') else 'an entity' if k.get('domains') else 'none'}
                     for k in manifest['tiles']],
           'cards': [{'id': c['id'], 'name': entry.text(c['name'])} for c in manifest['cards']],
           'bar_items': [{'id': b['id'], 'name': entry.text(b['label'])} for b in manifest['bar_items']],
           'tap_actions': [{'id': a['id'], 'name': entry.text(a['label']), 'domains': a['domains']} for a in manifest['tap_actions']],
           'savers': [{'id': v['id'], 'name': entry.text(v['name']), 'step': v['step'], 'pictures': v['pictures'],
                       'taps': v['taps'], 'moves': v['moves'], 'memory': v['memory']} for v in manifest.get('savers') or []],
           'settings_entities': [s['key'] for s in manifest['settings']],
           'settings_rows': bool(SETTINGS_ROWS.search(code)),
           'questions': manifest['permissions']['ha_commands'],
           'actions': sorted(set(ACTION.findall(code))),
           'api_0_9': bool(API_0_9.search(code)), 'mismatches': []}
    for part, pattern in CODE.items():
        named = {item['id'] for item in manifest.get(part) or []}
        found = set(pattern.findall(code))
        for missing in sorted(named - found):
            out['mismatches'].append(f'the manifest names {part[:-1].replace("_", " ")} {missing!r}, the code registers none of that id')
        for extra in sorted(found - named):
            out['mismatches'].append(f'the code registers {part[:-1].replace("_", " ")} {extra!r}, the manifest does not name it, '
                                     'so the app never offers it')
    allowed = set(manifest['permissions']['home_assistant_actions'])
    for action in out['actions']:
        if action not in allowed:
            out['mismatches'].append(f'the code calls {action}, which permissions.home_assistant_actions does not name')
    api = tuple(int(n) for n in str(manifest['api']).split('.'))
    if out['api_0_9'] and api < (0, 9):
        out['mismatches'].append(f'the code uses a part of plugin API 0.9 (Font::DIGITS, ui::covers, Tone::GOOD, ui::set_fill, ui::tone_color): the manifest '
                                 f'says api "{manifest["api"]}"')
    if API_0_14.search(code) and api < (0, 14):
        out['mismatches'].append(f'the code uses a part of plugin API 0.14 (a screensaver: add_saver, Saver, ui::takes_taps, '
                                 f'tessera::wish): the manifest says api "{manifest["api"]}"')
    if re.search(r'\bui::takes_taps\s*\(', code) and not any(v['taps'] for v in manifest.get('savers') or []):
        out['mismatches'].append('the code makes a part with ui::takes_taps, but no saver of the manifest says taps: true: '
                                 'say it there, so the app and a person know a tap on the saver may do something')
    return out


# ---- the app's side, with one plugin and no Home Assistant

class SampleFetcher:
    """Stands in for plugin_fetch.Fetcher: the answer of a fetch is the one preview.yaml gives for its id."""

    def __init__(self):
        self.answers = {}

    async def get(self, plugin, manifest, fetch, values, secrets):
        return None, self.peek(plugin, fetch, values, secrets)

    def peek(self, plugin, fetch, values, secrets):
        if fetch['id'] not in self.answers:
            return None
        return {'data': self.answers[fetch['id']], 'error': None, 'stale': False}


class PreviewApp(app_plugins.Plugins):
    """The app's plugin side with one plugin from a folder: tile_message, values_of, tile_data and entity_part are the
    app's own; only where they read the app's stores is something else in hand."""

    def __init__(self, entry, states, live, secrets, zone):
        self.entry, self.secret_map = entry, secrets
        self.manager = type('Manager', (), {})()
        self.manager.ha = type('HA', (), {'states': states, 'time_zone': zone})()
        self.fetcher = plugin_fetch.Fetcher(f'Tessera/{core.FIRMWARE_VERSION} (+plugins preview)') if live else SampleFetcher()

    def known(self, plugin, inbox=None):
        return self.entry if plugin == self.entry.id else None

    def secret_values(self, entry, inbox=None):
        return dict(self.secret_map)


def secrets_from_env(entry):
    """A secret input's value from TESSERA_SECRET_<ID> (never from a file in the plugin's folder, which is shared)."""
    return {i['id']: os.environ[f'TESSERA_SECRET_{i["id"].upper()}'] for i in entry.manifest['inputs']
            if i['kind'] == 'secret' and os.environ.get(f'TESSERA_SECRET_{i["id"].upper()}')}


# ---- the layout of a bench

def size_name(columns, rows, grid, full=False):
    """The layout's name for a tile of columns x rows on this grid, or None when the grid does not take it."""
    if (columns, rows) == (grid.columns, grid.rows):
        return 'full' if full else None
    if columns > grid.columns or rows > grid.rows:
        return None
    named = {(1, 1): 'single', (2, 1): 'wide', (1, 2): 'tall', (2, 2): 'square'}
    if (columns, rows) in named:
        return named[(columns, rows)]
    return f'{columns}x{rows}' if core.span_offered(columns, rows, grid) else None


def spans(kind):
    """Every columns x rows between the tile's min and max, smallest first."""
    low, high = (tuple(int(n) for n in kind[key].split('x')) for key in ('min', 'max'))
    return [f'{c}x{r}' for r in range(low[1], high[1] + 1) for c in range(low[0], high[0] + 1)]


def examples_of(entry, spec):
    """preview.yaml's tiles, or one per tile type with its defaults; each with an id for the pictures."""
    kinds = {kind['id']: kind for kind in entry.manifest['tiles']}
    examples = spec.get('tiles') or [{'tile': tile_id} for tile_id in kinds]
    out = []
    for i, example in enumerate(examples):
        kind = kinds.get(example.get('tile'))
        if not kind:
            raise SystemExit(f'preview.yaml tiles[{i}]: tile {example.get("tile")!r} is not in the manifest '
                             f'({", ".join(kinds) or "it has none"})')
        name = example.get('name') or entry.text(kind['name'])
        out.append({**example, 'key': f'{i + 1}-{SHOWN.sub("-", name.lower()).strip("-") or kind["id"]}',
                    'name': name, 'kind': kind,
                    'spans': [s for s in (example.get('sizes') or spans(kind))] + (['full'] if kind.get('full') else [])})
    return out


def layout_of(entry, examples, grid):
    """(record, compiled tiles, [(example, span)] by tile index, bars, left out) of the bench's layout on this grid:
    every example at every span this grid takes, packed in order."""
    wanted, left = [], []
    for example in examples:
        for span in example['spans']:
            name = 'full' if span == 'full' else size_name(*(int(n) for n in span.split('x')), grid, example['kind'].get('full'))
            if not name:
                left.append((example, span))
                continue
            options = {'size': name, 'plugin': dict(example.get('options') or {})}
            if example.get('entity'):
                options['plugin_entity'] = example['entity']
            wanted.append(({'entity': f'plugin:{entry.id}.{example["kind"]["id"]}', 'name': example['name'],
                            'options': options}, (example, span)))
    slots = grid.pack([tile for tile, _ in wanted]) if wanted else []
    kept = []
    for (tile, origin), slot in zip(wanted, slots):
        if grid.footprint(slot, core.tile_size(tile))[-1] >= grid.max_slots:
            left.append(origin)
            continue
        kept.append((dict(tile, slot=slot), origin))
    title = 'Home'
    record = send_layout.migrate_legacy(dict(title=title, tiles=[tile for tile, _ in kept]), grid)
    compiled = send_layout.compile_tiles(record['layout'], grid)
    # compile_tiles keeps the order of the slots, which is the order they were packed in
    by_slot = {tile['slot']: origin for tile, origin in kept}
    origins = {i: by_slot.get(tile.get('slot')) for i, tile in enumerate(compiled)}
    items = [{'k': 'plugin', 't': f'plugin:{entry.id}.{item["id"]}'} for item in entry.manifest['bar_items']]
    bars = [items + [{'k': 'clock'}] for _ in record['layout']['pages']]
    return record, compiled, origins, bars, left


async def messages(app, compiled, origins, spec, now):
    """The state message of every tile, as the app shapes it, from preview.yaml's answers (or the services)."""
    zone = spec.get('time_zone') or ZONE
    out = []
    for i, tile in enumerate(compiled):
        example = (origins.get(i) or ({}, None))[0]
        if isinstance(app.fetcher, SampleFetcher):
            app.fetcher.answers = moments({**(spec.get('answers') or {}), **(example.get('answers') or {})}, zone, now)
        message = await app.tile_message(i, tile)
        if 'data' in example:   # what the screen gets, as it is: a state the map cannot reach (a "wait")
            message['x'] = moments(example['data'], zone, now)
        out.append(message)
    return out


def saver_examples(entry, spec):
    """preview.yaml's savers (docs/PREVIEW.md, "Screensavers"), or one per saver of the manifest with its step's sample
    data; each with a key for the pictures."""
    kinds = {kind['id']: kind for kind in entry.manifest.get('savers') or []}
    given = spec.get('savers') or [{'saver': saver_id} for saver_id in kinds]
    if not isinstance(given, list):
        raise SystemExit('preview.yaml savers: a list of {saver, name, options, data, picture, entity, answers}')
    out = []
    for i, example in enumerate(given):
        kind = kinds.get(example.get('saver')) if isinstance(example, dict) else None
        if not kind:
            raise SystemExit(f'preview.yaml savers[{i}]: saver {(example or {}).get("saver")!r} is not in the manifest '
                             f'({", ".join(kinds) or "it has none"})')
        if example.get('picture') and kind['step'] not in ('media', 'photos'):
            raise SystemExit(f'preview.yaml savers[{i}]: a picture is the music\'s cover or the photos\' photo; a saver of '
                             f'step {kind["step"]} has none')
        name = example.get('name') or entry.text(kind['name'])
        out.append({**example, 'kind': kind, 'name': name,
                    'key': f'{i + 1}-{SHOWN.sub("-", name.lower()).strip("-") or kind["id"]}'})
    return out


def saver_word(entry, kind, options, data, motion=False):
    """The app's word for the screensaver (screen_saver.message) with the plugin's saver in `pf`: a face goes with its
    step's own fields, so a screen that lacks the plugin would show the step's own face; a step of its own is `plugin`."""
    pf = {'p': f'plugin:{entry.id}.{kind["id"]}', 'o': dict(options or {}), 'x': data}
    word = {'v': 1, 'op': 'saver', 'pf': pf}
    step = kind['step']
    if step == 'clock':
        return {**word, 'k': 'clock', 'w': data.get('temperature') or ''}
    if step == 'media':
        extra = {key: data[key] for key in ('artist', 'album') if data.get(key)}
        if data.get('mark'):
            extra['pic'] = data['mark']
        return {**word, 'k': 'media', 'e': data.get('entity') or STEP_SAMPLES['media']['entity'], 'n': data.get('name') or '',
                't': data.get('title') or '', 'x': extra, 's': data.get('state') or '', 'f': int(data.get('features') or 0),
                **({'m': 1} if data.get('muted') else {})}
    if step == 'photos':
        # `motion: true` of the example: the photos move slowly (the person's Motion: Slow), under a face of `backdrop`.
        return {**word, 'k': 'photos', 'e': 'saver.photos', 'n': '', 'x': {'pic': data.get('mark') or '', **({'mv': 1} if motion else {})}}
    return {**word, 'k': 'plugin'}


# ---- one screen's program

class Bench(run.Run):
    """One screen's program with the plugin built in, driven over its API, every picture and finding kept."""

    def __init__(self, build, screen, out, entry, spec, args):
        super().__init__(build, out, run.Pictures(), (960, 540), 'plugin')
        self.screen, self.entry, self.spec, self.args = screen, entry, spec, args
        self.views, self.findings, self.asked, self.actions = [], [], [], []
        self.now = int(time.time())
        self.watch = None
        self.states = {entity: {'state': str(value.get('state', '')), 'attributes': value.get('attributes') or {}}
                       for entity, value in moments(spec.get('states') or {}, spec.get('time_zone') or ZONE, self.now).items()}
        self.read = set()
        self.saver_shown = None   # the example of preview.yaml's savers on the glass, for its picture

    def find(self, level, where, text):
        self.findings.append({'level': level, 'screen': self.screen.key, 'where': where, 'text': text})

    async def connect(self):
        # Again here: a module of the driver may have set a level of its own since it was imported.
        for name in [n for n in logging.root.manager.loggerDict if n.startswith('aioesphomeapi')]:
            logging.getLogger(name).setLevel(logging.CRITICAL)
        self.client = run.APIClient('127.0.0.1', self.item.port, None)
        for _ in range(240):
            if self.process.poll() is not None:
                raise RuntimeError(f'the program exited with {self.process.returncode}; see {self.out / "program.log"}')
            # The port first: until the program listens, a try to connect only fills the output with refusals.
            if run.free(self.item.port):
                await asyncio.sleep(0.5)
                continue
            try:
                await self.client.connect(login=True)
                break
            except Exception:
                await asyncio.sleep(0.5)
        else:
            raise RuntimeError('the API never came up')
        entities, services = await self.client.list_entities_services()
        self.services = {s.name: s for s in services}
        self.inbox = next(e for e in entities if type(e).__name__ == 'TextInfo' and e.name == 'Tile settings')
        self.dark_switch = next(e for e in entities if getattr(e, 'name', '') == 'Dark mode')
        self.subscribe_logs()
        self.client.subscribe_service_calls(self.heard)
        # The bench stands in for Home Assistant: an entity the screen reads itself (a homeassistant sensor of the
        # plugin's plugin.yaml) gets preview.yaml's state, as Home Assistant would send it.
        self.client.subscribe_home_assistant_states(self.wants)
        if 'render_skip_calibration' in self.services:
            await self.call('render_skip_calibration')
        # One moment for the whole bench, the one preview.yaml's {now} is made from, so nothing ticks between pictures.
        await self.call('render_time', epoch=self.now)
        self.sender = send_layout.api_sender(self.client, services)

    def wants(self, entity, attribute):
        if (entity, attribute) in self.read:
            return
        self.read.add((entity, attribute))
        known = self.states.get(entity)
        # The API sends "" for a sensor of the entity's state, not None: both are the state.
        value = None if not known else known['state'] if not attribute else known['attributes'].get(attribute)
        if value is None:
            self.find('info', 'Home Assistant', f'the screen reads {entity}{" " + attribute if attribute else ""}; '
                      'preview.yaml has no such state, so it stays empty')
            return
        text = json.dumps(value) if isinstance(value, (list, dict)) else str(value).lower() if isinstance(value, bool) else str(value)
        self.client.send_home_assistant_state(entity, attribute, text)

    def heard(self, call):
        """A call of the screen to Home Assistant: a plugin's question (answered from preview.yaml), a plugin tile's,
        card's or screensaver's question for its pictures (answered from preview.yaml's pictures, plugin API 0.10) or an
        action."""
        if call.service == 'esphome.screen_plugin':
            asyncio.get_running_loop().create_task(self.answer(call.data))
        elif call.service == 'esphome.screen_camera' and call.data.get('saver') in ('media', 'photos'):
            # The step's own picture under a face of `backdrop: true` (plugin API 0.14): as the app makes it.
            asyncio.get_running_loop().create_task(self.answer_backdrop(dict(call.data)))
            self.last_ask = time.monotonic()
        elif call.service == 'esphome.screen_camera' and core.plugin_tile(str(call.data.get('tiles') or '')):
            job = asyncio.get_running_loop().create_task(self.answer_pictures(dict(call.data)))
            self.picture_jobs = getattr(self, 'picture_jobs', set())
            self.picture_jobs.add(job)
            job.add_done_callback(self.picture_jobs.discard)
            self.last_ask = time.monotonic()
        elif not call.service.startswith('esphome.'):
            self.actions.append(f'{call.service} {dict(call.data)}')

    async def pictures_settled(self, timeout=8.0):
        """Until the pictures the screen asked for are answered (and a moment for their download and drawing), so a
        snapshot shows the posters, not the blocks before them: no question open, none new for 0.8 s."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if not getattr(self, 'picture_jobs', None) and time.monotonic() - getattr(self, 'last_ask', 0) > 0.8:
                break
            await asyncio.sleep(0.1)
        if getattr(self, 'last_ask', 0):
            await asyncio.sleep(0.6)

    async def answer_backdrop(self, data):
        """The step's own picture under a face of `backdrop: true`: the example's `picture` or a drawn cover or photo, made
        as the app makes the screensaver's (camera_feed.encode_photo, moving a little larger where the word says `mv`;
        encode_saver for the music), answered as "saver"."""
        import camera_feed
        import media_art
        shown = self.saver_shown or {}
        kind = data.get('saver')
        box = camera_feed.capped((self.screen.width, self.screen.height), cap=camera_feed.picture_cap(data))
        if shown.get('picture'):
            raw = (self.args.plugin.resolve() / str(shown['picture'])).read_bytes()
        else:
            raw = sample_photo(self.screen.width, self.screen.height) if kind == 'photos' else media_art.png(7, 640)
        if kind == 'photos':
            moving = camera_feed.motion_box(box, camera_feed.picture_cap(data)) if shown.get('motion') else None
            body = camera_feed.encode_photo(raw, box, 'fill', moving)
        else:
            body = camera_feed.encode_saver(raw, box, 'media', 0)
        try:
            view = int(data.get('view', 0))
        except (TypeError, ValueError):
            view = 0
        url = self.pictures.url(f'{self.screen.key}-backdrop-{view}.bmp', body)
        await self.send({'v': 1, 'op': 'camera', 't': 'saver', 'e': data.get('entity', ''), 'u': url, 'view': view})

    async def answer_pictures(self, data):
        """The pictures of a plugin tile's, card's or screensaver's places (a card: plugin API 0.13, a screensaver: 0.14),
        as the app would answer (server.answer_plugin_pictures): the frames of
        the question painted with the files preview.yaml's `pictures` names for each address (relative to the plugin's
        folder), a drawn stand-in for an address it does not name, and the checks the app makes (the manifest's
        `pictures`, where a picture may come from). A screensaver's `step:`, the step's own picture, is the
        example's `picture` of preview.yaml's `savers`, or a drawn cover (the music) or photo (the photos)."""
        import plugin_pictures
        import tile_art
        import media_art
        question = plugin_pictures.parse(data, (self.screen.width, self.screen.height))
        # A card over the page asks too (plugin API 0.13, `card`): the app answers it as "pcard".
        card = bool(getattr(question, 'card', False))
        # A screensaver asks too (plugin API 0.14, `saver`): the app answers it as "psaver".
        saver = bool(getattr(question, 'saver', False))
        reply = {'v': 1, 'op': 'camera', 't': 'pcard' if card else 'psaver' if saver else 'live', 'e': data.get('tiles', ''), 'u': ''}
        try:
            view = int(data.get('view', 0))
        except (TypeError, ValueError):
            view = 0
        if not question:
            self.find('warning', 'pictures', f'a tile or card asked for pictures in a way the app does not take: {data}')
            return
        if saver:
            kind = self.entry.saver(question.tile) if hasattr(self.entry, 'saver') else None
            problem = plugin_pictures.refused(question, self.entry.manifest, kind, bool(question.of))
        elif card:
            # The manifest's card; Home Assistant's own pictures when the card was opened for an entity (`for`), as the
            # app allows them for a card shown for an entity of the layout.
            kind = next((c for c in self.entry.manifest.get('cards') or [] if c['id'] == question.tile), None)
            problem = plugin_pictures.refused(question, self.entry.manifest, kind, bool(question.of))
        else:
            kind = self.entry.tile(question.tile)
            problem = plugin_pictures.refused(question, self.entry.manifest, kind)
        if problem:
            self.find('warning', 'pictures', f'{question.entity} asked for pictures the app refuses: {problem}')
        else:
            named = self.spec.get('pictures') or {}
            raws = []
            for n, url in enumerate(question.sources):
                file = named.get(url) if isinstance(named, dict) else None
                if url == SAVER_PICTURE:
                    shown = self.saver_shown or {}
                    if shown.get('picture'):
                        raws.append((self.args.plugin.resolve() / str(shown['picture'])).read_bytes())
                    else:
                        raws.append(sample_photo(self.screen.width, self.screen.height) if shown.get('step') == 'photos'
                                    else media_art.png(7, 640))
                elif file:
                    raws.append((self.args.plugin.resolve() / str(file)).read_bytes())
                else:
                    self.find('info', 'pictures', f'preview.yaml names no picture for {url[:80]}: a drawn one stands in')
                    raws.append(media_art.png(sum(map(ord, url)) + n, 320))
            # As the app paints them (plugin_pictures.Pictures.atlas): layers in their order, turned and mirrored as asked.
            paint = getattr(tile_art, 'layers', None)
            if paint and hasattr(question, 'styles'):
                body = paint(raws, question.ground, question.atlas, question.styles(), compact=True)
            else:   # a Tessera from before layers
                body = tile_art.encode(raws, [question.ground] * len(raws), question.atlas,
                                       [('fill' if fit == 'cover' else 'within', False) for fit in question.fits], compact=True)
            reply['u'] = self.pictures.url(f'{self.screen.key}-pictures-{view}.bmp', body)
        for attempt in range(8):
            try:
                await self.send({**reply, 'view': view})
                return
            except Exception as error:
                if attempt == 7:
                    self.find('warning', 'pictures', f'the pictures of {question.entity} did not reach the screen: {error}')
                await asyncio.sleep(0.5)

    async def answer(self, data):
        try:
            body = json.loads(data.get('body') or '{}')
        except ValueError:
            return
        ask, reply = body.get('ask'), {'re': body.get('re') if isinstance(body.get('re'), int) else 0}
        commands = self.spec.get('commands') or {}
        self.asked.append(ask)
        if ask not in self.entry.manifest['permissions']['ha_commands']:
            reply.update(ok=False, error='not_allowed')
            self.find('warning', 'questions', f'asked {ask!r}, which permissions.ha_commands does not name: the app refuses it')
        elif ask not in commands:
            reply.update(ok=False, error='no answer in preview.yaml')
            self.find('info', 'questions', f'asked {ask!r}; preview.yaml has no answer under commands, so it got none')
        else:
            result = moments(commands[ask], self.spec.get('time_zone') or ZONE, self.now)
            reply.update(ok=True, result=answer_of(self.entry, ask, result, self.spec.get('time_zone') or ZONE))
        # An answer that meets a layout on its way waits for it, as the app's own does.
        for attempt in range(8):
            try:
                await self.send({'v': 2, 'op': 'plugin', 'p': self.entry.id, 'm': reply})
                return
            except Exception as error:
                if attempt == 7:
                    self.find('warning', 'questions', f'the answer to {ask!r} did not reach the screen: {error}')
                await asyncio.sleep(0.5)

    async def dark(self, on):
        self.client.switch_command(self.dark_switch.key, on)
        await asyncio.sleep(0.6)

    async def audit(self, card=False, saver=False):
        start = len(self.lines)
        await self.call('render_plugin_audit', card=card, saver=saver)
        if card or saver:
            what = 'saver' if saver else 'card'
            line = await self.until(lambda l: f'audit {what} ' in l, 5, f'the {what} audit', start)
            return [AUDIT.search(line).groups()]
        await self.until(lambda l: 'audit end' in l, 5, 'the tile audit', start)
        return [AUDIT.search(l).groups() for l in self.lines[start:] if 'audit tile=' in l]

    def faults(self, groups, where, names=None):
        for tile, _, outside, cut, glyph, faint, clipped, overlap, notes in groups:
            place = f'{names[int(tile)]}, {where}' if names and tile is not None and int(tile) in names else where
            found = [f'{n} {what}' for n, what in ((int(outside), 'outside its area'), (int(cut), 'cut with dots'),
                                                   (int(glyph), 'with a character its font lacks'),
                                                   (int(faint), 'too faint to read on its ground'),
                                                   (int(clipped), 'lower than its letters'),
                                                   (int(overlap), 'over another part')) if int(n)]
            if found:
                self.find('warning', place, f'{", ".join(found)}: {notes.strip(";")}')

    async def box(self, index):
        start = len(self.lines)
        await self.call('render_tile_box', index=index)
        line = await self.until(lambda l: f'tile_box index={index} ' in l, 5, f'tile {index}', start)
        x1, y1, x2, y2 = (int(v) for v in BOX.search(line).groups())
        return ((x1 + x2) // 2, (y1 + y2) // 2), (x1, y1, x2, y2) if x2 > x1 else None

    async def drive(self):
        await self.connect()
        app = PreviewApp(self.entry, self.states, self.args.live, secrets_from_env(self.entry), self.spec.get('time_zone') or ZONE)
        # A tile's options may hold moments too (a date to count down to).
        examples = examples_of(self.entry, moments(self.spec, self.spec.get('time_zone') or ZONE, self.now))
        own = send_layout.Grid(self.screen.columns, self.screen.rows)
        grids, first = [own], True
        for grid in await self.grids_asked():
            if (grid.columns, grid.rows) != (own.columns, own.rows):
                grids.append(grid)
        for grid in grids:
            await self.view(app, examples, grid, first)
            first = False
        await self.savers(app)
        await self.settings()
        if self.args.window:
            await self.stay(app, examples, own)

    async def grids_asked(self):
        """The grids of --grid this screen takes the way it hangs; 'max' is the finest it takes."""
        if not self.args.grid:
            return []
        await self.sender.probe()
        ranges = self.sender.grids
        way = 'portrait' if self.item.rotation else 'landscape'
        if not ranges:
            self.find('info', 'grid', 'this firmware takes no other grid')
            return []
        least, most = ranges[way]['min'], ranges[way]['max']
        out = []
        for text in self.args.grid:
            columns, rows = most if text == 'max' else (int(n) for n in text.split('x'))
            if least[0] <= columns <= most[0] and least[1] <= rows <= most[1]:
                out.append(send_layout.Grid(columns, rows))
            else:
                self.find('info', 'grid', f'{columns} x {rows} is outside what this glass takes '
                                          f'({least[0]} x {least[1]} to {most[0]} x {most[1]})')
        return out

    async def view(self, app, examples, grid, own):
        """Everything of the plugin on one grid of this screen."""
        name = '' if own else f'{grid.columns}x{grid.rows}'
        out = self.out / name if name else self.out
        out.mkdir(parents=True, exist_ok=True)
        view = {'screen': self.screen.key, 'grid': f'{grid.columns} x {grid.rows}', 'folder': name, 'tiles': [],
                'pages': [], 'taps': [], 'cards': [], 'bar': None, 'settings': []}
        self.views.append(view)
        record, compiled, origins, bars, left = layout_of(self.entry, examples, grid)
        for example, span in left:
            self.find('info', f'{example["name"]} {span}', f'not on a {grid.columns} x {grid.rows} grid: it does not take '
                      'that size there, or the pages are full')
        if not compiled:
            if self.entry.manifest['tiles']:
                self.find('warning', 'tiles', f'no tile of the plugin fits a {grid.columns} x {grid.rows} grid')
            elif self.entry.manifest['bar_items'] and own:
                await self.bar_only(record, bars, out, view)
            return
        values = await messages(app, compiled, origins, self.spec, self.now)
        for i, message in enumerate(values):
            wait = (message.get('x') or {}).get('wait')
            if wait:
                self.find('info', compiled[i]['name'], f'the tile waits ({wait}): '
                          + ('preview.yaml has no answer for its fetch' if wait == 'asking' else 'see docs/PREVIEW.md'))
        await self.sender.synchronize(self.inbox.object_id, record, REGION, values, bars)
        if not any(p['id'] == self.entry.id for p in self.sender.plugins):
            raise RuntimeError(f'the screen does not name {self.entry.id} in its hello: is register_plugin() called?')
        pages = len(record['layout']['pages'])
        if self.entry.manifest['bar_items']:
            # The top bar reads its plugin items every five seconds (runtime_tiles: the device items), so data that
            # came with the layout is in the bar after that round.
            await asyncio.sleep(5.5)
        boxes, top = {}, None
        for theme in ('light', 'dark'):
            await self.dark(theme == 'dark')
            for page in range(pages):
                await self.call('render_page', page=page)
                await self.page_done(page)
                await asyncio.sleep(1.2 if theme == 'light' else 0.3)   # the plugin's first ticks
                await self.pictures_settled()
                shot = f'page-{page + 1}-{theme}'
                image = await self.render(shot, keep=False)
                image.save(out / f'{shot}.png')
                view['pages'].append({'file': f'{shot}.png', 'theme': theme, 'page': page + 1})
                for i, tile in enumerate(compiled):
                    if grid.page_of(tile['slot']) != page:
                        continue
                    if theme == 'light':
                        boxes[i] = await self.box(i)
                    centre, area = boxes[i]
                    if not area:
                        continue
                    top = area[1] if top is None else min(top, area[1])
                    example, span = origins[i]
                    file = f'tile-{example["key"]}-{span}-{theme}.png'
                    pad = 3
                    image.crop((max(0, area[0] - pad), max(0, area[1] - pad), min(image.width, area[2] + pad + 1),
                                min(image.height, area[3] + pad + 1))).save(out / file)
                    entry = next((t for t in view['tiles'] if t['index'] == i), None)
                    if not entry:
                        entry = {'index': i, 'example': example['key'], 'name': example['name'], 'span': span,
                                 'size': tile['options'].get('size'), 'px': [area[2] - area[0] + 1, area[3] - area[1] + 1]}
                        view['tiles'].append(entry)
                    entry[theme] = file
                names = {i: f'{o[0]["name"]} {o[1]}' for i, o in origins.items() if o}
                on = '' if own else f'the {grid.columns} x {grid.rows} grid, '
                self.faults(await self.audit(), f'{on}page {page + 1}, {theme}', names)
                if page == 0 and theme == 'light':
                    for item in self.entry.manifest['bar_items']:
                        start = len(self.lines)
                        key = f'plugin:{self.entry.id}.{item["id"]}'
                        await self.call('render_bar_item', key=key)
                        line = await self.until(lambda l: f'bar item {key} shown=' in l, 5, f'the bar item {item["id"]}', start)
                        if 'shown=0' in line:
                            self.find('info', f'top bar {item["id"]}', 'the item shows nothing with this data')
                if page == 0 and top is not None and self.entry.manifest['bar_items']:
                    file = f'bar-{theme}.png'
                    image.crop((0, 0, image.width, max(1, top - 2))).save(out / file)
                    view['bar'] = {**(view['bar'] or {}), theme: file}
        await self.dark(False)
        if self.entry.manifest['bar_items'] and top is not None:
            await self.icons_only(record, values, top, out, view)
        await self.taps(compiled, origins, grid, boxes, out, view)
        await self.cards(out, view)

    async def bar_only(self, record, bars, out, view):
        """A plugin that adds to the top bar and has no tile: an empty first page with its items in the bar, light and
        dark (the pages of view['pages']; tools/store_image.py shows them)."""
        items = [{'k': 'plugin', 't': f'plugin:{self.entry.id}.{item["id"]}'} for item in self.entry.manifest['bar_items']]
        bars = bars or [items + [{'k': 'clock'}]]
        await self.sender.synchronize(self.inbox.object_id, record, REGION, [], bars)
        await asyncio.sleep(5.5)   # the bar reads its plugin items every five seconds
        for theme in ('light', 'dark'):
            await self.dark(theme == 'dark')
            await self.call('render_page', page=0)
            # An empty page draws no card, so page_done (which waits for one) never ends: the page placed is enough.
            for _ in range(50):
                placed = await self.probe()
                if placed[0] == 0 and placed[1] == 0:
                    break
                await asyncio.sleep(0.2)
            await asyncio.sleep(0.6)
            shot = f'page-1-{theme}'
            (await self.render(shot, keep=False)).save(out / f'{shot}.png')
            view['pages'].append({'file': f'{shot}.png', 'theme': theme, 'page': 1})
        await self.dark(False)

    async def icons_only(self, record, values, top, out, view):
        """The top bar with the plugin's items as their icon alone, as a person may choose in the editor (Show: icon),
        then as it was."""
        items = [{'k': 'plugin', 't': f'plugin:{self.entry.id}.{item["id"]}', 'o': 1} for item in self.entry.manifest['bar_items']]
        bars = [items + [{'k': 'clock'}] for _ in record['layout']['pages']]
        await self.sender.synchronize(self.inbox.object_id, record, REGION, values, bars)
        await self.call('render_page', page=0)
        await self.page_done(0)
        await asyncio.sleep(5.5)
        for theme in ('light', 'dark'):
            await self.dark(theme == 'dark')
            image = await self.render('_icons', keep=False)
            file = f'bar-icon-{theme}.png'
            image.crop((0, 0, image.width, max(1, top - 2))).save(out / file)
            view['bar_icon'] = {**(view.get('bar_icon') or {}), theme: file}
        await self.dark(False)
        plain = [[{'k': 'plugin', 't': item['t']} for item in items] + [{'k': 'clock'}] for _ in record['layout']['pages']]
        await self.sender.synchronize(self.inbox.object_id, record, REGION, values, plain)

    async def taps(self, compiled, origins, grid, boxes, out, view):
        """A tap on every plugin tile: what it opens (a card of the plugin) is kept, and closed again."""
        for i, tile in enumerate(compiled):
            centre, area = boxes.get(i, (None, None))
            if not area:
                continue
            page = grid.page_of(tile['slot'])
            await self.call('render_page', page=page)
            await self.page_done(page)
            before = await self.render('_before', keep=False)
            heard = len(self.actions)
            await self.tap(*centre)
            await asyncio.sleep(0.6)
            await self.pictures_settled()
            after = await self.render('_after', keep=False)
            example, span = origins[i]
            did = self.actions[heard:]
            if after.tobytes() != before.tobytes() or did:
                file = f'tap-{example["key"]}-{span}.png'
                after.save(out / file)
                view['taps'].append({'file': file, 'name': example['name'], 'span': span, 'actions': did})
                if after.tobytes() != before.tobytes():
                    self.faults(await self.audit(card=True), f'the tap on {example["name"]} {span}')
            await self.call('render_close_cards')
            await asyncio.sleep(0.4)

    async def cards(self, out, view):
        """Every card of the manifest, opened by its id with the entity preview.yaml gives it, light and dark."""
        given = {c.get('card'): c for c in (self.spec.get('cards') or [])}
        for card in self.entry.manifest['cards']:
            spec = given.get(card['id'], {})
            entity = spec.get('entity') or next(
                (e for e in (self.spec.get('states') or {}) if any(e.split('.')[0] in action['domains']
                                                                   for action in self.entry.manifest['tap_actions']
                                                                   if action.get('card') == card['id'])), '')
            title = spec.get('title') or self.entry.text(card['name'])
            item = {'id': card['id'], 'title': title, 'entity': entity}
            for theme in ('light', 'dark'):
                await self.dark(theme == 'dark')
                start = len(self.lines)
                await self.call('render_open_card', card=card['id'], entity=entity, title=title)
                line = await self.until(lambda l: f'card {card["id"]} opened=' in l, 5, f'card {card["id"]}', start)
                if 'opened=1' not in line:
                    self.find('warning', f'card {card["id"]}', 'the manifest names it, but the plugin registers no card '
                              'of that id (add_card)')
                    break
                await asyncio.sleep(1.2)   # a card that asks Home Assistant first: the answer and its first tick
                file = f'card-{card["id"]}-{theme}.png'
                (await self.render(f'card-{card["id"]}-{theme}', keep=False)).save(out / file)
                item[theme] = file
                self.faults(await self.audit(card=True), f'card {card["id"]}, {theme}')
                await self.call('render_close_cards')
                await asyncio.sleep(0.4)
            await self.dark(False)
            view['cards'].append(item)

    async def saver_data(self, app, example):
        """What a saver draws from: its step's contract (STEP_SAMPLES, with preview.yaml's `data` over it), or for a step
        of its own the app's own shaping of its fetch's answer and its entity, as a plugin tile's, unless `data` gives
        it whole."""
        kind = example['kind']
        if kind['step'] != 'own':
            return {**STEP_SAMPLES[kind['step']], **(example.get('data') or {})}
        if 'data' in example:
            return example['data']
        zone = self.spec.get('time_zone') or ZONE
        if isinstance(app.fetcher, SampleFetcher):
            app.fetcher.answers = moments({**(self.spec.get('answers') or {}), **(example.get('answers') or {})}, zone, self.now)
        options = app.values_of(self.entry, kind, example.get('options'))
        _, part = app.entity_part(kind, {'options': {'plugin_entity': example.get('entity') or ''}})
        fetched = await app.tile_data(self.entry, kind, options, True, None)
        return {**(part or {}), **(fetched or {})}

    async def session(self):
        """A layout on the screen, so it takes the screensaver's word: an empty page for a plugin without tiles."""
        if self.sender.session is not None:
            return
        record, _, _, bars, _ = layout_of(self.entry, [], send_layout.Grid(self.screen.columns, self.screen.rows))
        await self.sender.synchronize(self.inbox.object_id, record, REGION, [], bars or [[{'k': 'clock'}]])

    async def show_saver(self, example, data, theme):
        """The screen awake in this look, then in standby with the example's word: the saver as it comes on the glass,
        its first ticks and its pictures, photographed. None when the screen shows no screensaver."""
        kind = example['kind']
        await self.call('render_standby', enter=0)
        # No word while awake, so the next one is shown fresh (show()) rather than painted into the last (on_state).
        await self.send({'v': 1, 'op': 'saver', 'k': ''})
        await self.dark(theme == 'dark')
        await self.call('render_time', epoch=self.now)
        await self.call('render_standby', enter=1)
        await asyncio.sleep(0.4)
        self.saver_shown = {'step': kind['step'], 'picture': example.get('picture'), 'motion': bool(example.get('motion'))}
        start = len(self.lines)
        await self.send(saver_word(self.entry, kind, example.get('options'), data, bool(example.get('motion'))))
        key = f'plugin:{self.entry.id}.{kind["id"]}'
        try:
            await self.until(lambda l: (SAVER_SHOWN.search(l) or [None, None])[1] == key, 8, f'the saver {kind["id"]}', start)
        except RuntimeError:
            return None
        await asyncio.sleep(1.2)   # its first ticks
        await self.pictures_settled()
        return await self.render('_saver', keep=False)

    async def still(self, example, first):
        """Two minutes later the saver must look otherwise: the screen moves a saver a little every minute (its drift),
        and one that says it moves itself (`moves: true`) must do so; a saver that stands still marks the glass."""
        kind = example['kind']
        await self.call('render_time', epoch=self.now + SAVER_STILL_SECONDS)
        await asyncio.sleep(2.2)   # its tick, and the drift's
        later = await self.render('_saver-later', keep=False)
        await self.call('render_time', epoch=self.now)
        if later.tobytes() != first.tobytes():
            return
        if kind['moves']:
            why = ('the manifest says moves: true, so the screen leaves it where it draws: move it yourself in on_tick '
                   '(a minute at a time is enough), or leave moves out and the screen moves it')
        else:
            why = ('the screen\'s drift did not move it: make every part in SaverContext::parent, the stage the screen '
                   'moves')
        self.find('warning', f'saver {example["name"]}', f'it stands still: {SAVER_STILL_SECONDS // 60} minutes later every '
                  f'pixel is the same, which marks the glass over the hours of a night; {why}')

    async def savers(self, app):
        """Every screensaver of the plugin (plugin API 0.14) in standby, with preview.yaml's data of its step (or the
        step's sample), on the screen light and dark (the saver itself is drawn dark in both): its picture, the audit
        that tiles and cards get, and the same saver two minutes later, which must not be the same picture."""
        examples = saver_examples(self.entry, moments(self.spec, self.spec.get('time_zone') or ZONE, self.now))
        if not examples:
            return
        view = self.views[0]
        view['savers'] = []
        if 'render_standby' not in self.services:
            self.find('info', 'screensavers', 'this copy of Tessera has no standby for the bench (render_standby): '
                      'python3 tools/preview.py --setup fetches a newer one')
            return
        await self.session()
        mine = next((p for p in self.sender.plugins if p['id'] == self.entry.id), {})
        named = set(mine.get('savers') or [])
        shape = json.loads((REPO / 'screen_manager/app/boards.json').read_text()).get(self.item.board) or {}
        pictured = 'camera' in shape
        for example in examples:
            kind = example['kind']
            if kind['id'] not in named:
                self.find('warning', f'saver {kind["id"]}', 'the screen does not name it in its hello: the code registers '
                          'no saver of that id in setup() (add_saver), so the app never shows it')
                continue
            if kind['step'] in ('media', 'photos') and not pictured:
                self.find('info', f'saver {example["name"]}', f'not on this screen: the {kind["step"]} step shows only on '
                          'a screen with pictures')
                continue
            data = await self.saver_data(app, example)
            item = {'key': example['key'], 'name': example['name'], 'id': kind['id'], 'step': kind['step']}
            for theme in ('light', 'dark'):
                image = await self.show_saver(example, data, theme)
                if image is None:
                    self.find('info', f'saver {example["name"]}', 'this screen showed no screensaver: it never goes into '
                              'standby (the Waveshare 4.3, 5 and 7-inch), or its standby level is 0')
                    break
                file = f'saver-{example["key"]}-{theme}.png'
                image.save(self.out / file)
                item[theme] = file
                self.faults(await self.audit(saver=True), f'saver {example["name"]}, {theme} screen')
                if theme == 'light':
                    await self.still(example, image)
            if item.get('light') or item.get('dark'):
                view['savers'].append(item)
        self.saver_shown = None
        await self.call('render_standby', enter=0)
        await self.send({'v': 1, 'op': 'saver', 'k': ''})
        await self.dark(False)

    async def settings(self):
        """The plugin's pages on the screen's settings page (Settings, Plugins), when it adds rows there."""
        start = len(self.lines)
        await self.call('render_settings', page=0)
        line = await self.until(lambda l: SETTINGS.search(l), 5, 'the settings page', start)
        count = int(SETTINGS.search(line).group(1))
        for page in range(count):
            await self.call('render_settings', page=page)
            await asyncio.sleep(0.4)
            file = f'settings-{page + 1}.png'
            (await self.render(f'settings-{page + 1}', keep=False)).save(self.out / file)
            self.views[0]['settings'].append({'file': file, 'title': 'Settings, Plugins' if page == 0 else f'its page {page}'})
        await self.call('render_settings', page=-1)

    async def stay(self, app, examples, grid):
        """The window stays: a person taps and swipes it with the mouse, the data goes again every 15 s as the app's loop
        sends it, until Ctrl-C or, with --watch, a change in the plugin's files."""
        record, compiled, origins, bars, _ = layout_of(self.entry, examples, grid)
        await self.sender.synchronize(self.inbox.object_id, record, REGION, await messages(app, compiled, origins, self.spec, self.now), bars)
        await self.call('render_page', page=0)
        print(f'{self.screen.key}: the window is yours, tap and swipe it with the mouse; Ctrl-C to stop', flush=True)
        while self.process.poll() is None:
            for _ in range(30):
                await asyncio.sleep(0.5)
                if self.watch and self.watch.differs():
                    return
            self.now = int(time.time())
            await self.call('render_time', epoch=self.now)
            await self.sender.synchronize(self.inbox.object_id, record, REGION,
                                          await messages(app, compiled, origins, self.spec, self.now), bars)

    async def run(self):
        self.out.mkdir(parents=True, exist_ok=True)
        for old in self.out.rglob('*.png'):
            old.unlink()
        (Path.home() / '.esphome' / 'prefs' / f'{self.item.name}.prefs').unlink(missing_ok=True)
        log = open(self.out / 'program.log', 'w')
        # Without --window the screen draws in no window at all (SDL's dummy driver): nothing pops up on the desk, and a
        # computer without a display (a CI runner) benches as well.
        env = os.environ if self.args.window else {**os.environ, 'SDL_VIDEODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software'}
        self.process = run.subprocess.Popen([str(self.build.program)], stdout=log, stderr=run.subprocess.STDOUT,
                                            cwd=self.build.work, env=env)
        try:
            await self.drive()
        finally:
            try:
                await self.client.disconnect()
            except Exception:
                pass
            self.process.terminate()
            try:
                self.process.wait(5)
            except run.subprocess.TimeoutExpired:
                self.process.kill()
            (self.out / 'log.txt').write_text('\n'.join(self.lines) + '\n')
            for old in self.out.glob('_*.p*'):
                old.unlink()


# ---- what a person looks at

def overview(out, entry, screens):
    """One picture of every tile at every size on every screen, light, at the screens' own pixels: a quick look, and
    what an assistant reads."""
    rows = {}
    for screen in screens:
        for view in screen['views']:
            if view['folder']:
                continue
            for tile in view['tiles']:
                rows.setdefault((tile['example'], tile['name'], tile['span']), {})[screen['key']] = out / screen['key'] / tile['light']
    if not rows:
        return
    keys = [s['key'] for s in screens]
    cell, gap, label = 300, 14, 150
    # One scale per screen, so the sizes of a tile stand side by side as they are on that glass.
    widest = {key: max((Image.open(r[key]).width for r in rows.values() if key in r and r[key].exists()), default=1) for key in keys}
    scale = {key: min(1, cell / widest[key]) for key in keys}
    heights = [max((round(Image.open(p).height * scale[k]) for k, p in r.items() if p.exists()), default=40) for r in rows.values()]
    canvas = Image.new('RGB', (label + len(keys) * (cell + gap) + gap, 46 + sum(h + gap for h in heights) + gap), (236, 236, 236))
    draw = ImageDraw.Draw(canvas)
    draw.text((gap, 12), f'{entry.id} {entry.version}: every tile at every size, light', fill=(20, 20, 20))
    for j, key in enumerate(keys):
        draw.text((label + j * (cell + gap), 30), key, fill=(60, 60, 60))
    y = 46
    for (example, name, span), height in zip(rows, heights):
        draw.text((gap, y + 4), f'{name}\n{span}', fill=(40, 40, 40))
        for j, key in enumerate(keys):
            path = rows[(example, name, span)].get(key)
            if path and path.exists():
                with Image.open(path) as image:
                    s = scale[key]
                    shown = image.resize((max(1, round(image.width * s)), max(1, round(image.height * s))), Image.LANCZOS)
                    canvas.paste(shown, (label + j * (cell + gap), y))
            else:
                draw.text((label + j * (cell + gap), y + 4), 'not on this screen', fill=(150, 150, 150))
        y += height + gap
    canvas.save(out / 'overview.png')


PAGE_STYLE = '''
:root{--bg:#f3f3f1;--card:#fff;--ink:#1d1d1f;--muted:#6b6b70;--line:#e2e2e0;--warn:#a15c00;--bad:#b00020}
@media (prefers-color-scheme:dark){:root{--bg:#141415;--card:#1e1e20;--ink:#eee;--muted:#9a9aa0;--line:#2c2c2f;--warn:#e0a040;--bad:#ff6b7f}}
*{box-sizing:border-box}body{margin:0;font:14px/1.45 system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--ink)}
header{position:sticky;top:0;z-index:2;background:var(--bg);border-bottom:1px solid var(--line);padding:14px 20px;display:flex;gap:20px;align-items:center;flex-wrap:wrap}
header h1{font-size:18px;margin:0}header .controls{display:flex;gap:8px;flex-wrap:wrap}
button{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--ink);border-radius:8px;padding:5px 10px;cursor:pointer}
button[aria-pressed=true]{background:var(--ink);color:var(--bg)}
main{padding:8px 20px 40px}h2{margin:28px 0 10px;font-size:16px}h3{margin:18px 0 8px;font-size:14px}
.muted{color:var(--muted)}table{border-collapse:collapse;background:var(--card);border-radius:10px;overflow:hidden}
th,td{text-align:left;padding:6px 10px;border-bottom:1px solid var(--line);vertical-align:top}
.scroll{overflow-x:auto;padding-bottom:6px}.matrix td{min-width:120px}
figure{margin:0 0 6px;display:inline-block;vertical-align:top}figure img{display:block;border-radius:6px;box-shadow:0 1px 3px #0003}
figcaption{font-size:12px;color:var(--muted);margin-top:3px}.gallery figure{margin-right:14px}
.warning{color:var(--warn)}.error{color:var(--bad)}.info{color:var(--muted)}
body[data-theme=light] .dark,body[data-theme=dark] .light{display:none}
'''

PAGE_SCRIPT = '''
const body=document.body;
function setTheme(t){body.dataset.theme=t;for(const b of document.querySelectorAll('[data-set-theme]'))b.setAttribute('aria-pressed',b.dataset.setTheme===t)}
function setScale(s){body.dataset.scale=s;for(const b of document.querySelectorAll('[data-set-scale]'))b.setAttribute('aria-pressed',b.dataset.setScale===s);
 for(const img of document.querySelectorAll('img[data-dpi]')){const w=img.naturalWidth;if(!w)continue;
  img.style.width=s==='real'?(w/img.dataset.dpi*96)+'px':s==='fit'?Math.min(w,320)+'px':w+'px'}}
for(const b of document.querySelectorAll('[data-set-theme]'))b.onclick=()=>setTheme(b.dataset.setTheme);
for(const b of document.querySelectorAll('[data-set-scale]'))b.onclick=()=>setScale(b.dataset.setScale);
window.addEventListener('load',()=>setScale(body.dataset.scale));
'''


def index_page(out, entry, screens, findings, started, parts_of):
    """The bench on one page: the screens, what it found, every tile at every size per screen, the cards, the top bar,
    the settings, and the full pages."""
    e = html.escape

    def img(screen, view, file, dpi, theme=None, caption=None):
        path = f'{screen["key"]}/{view["folder"] + "/" if view["folder"] else ""}{file}'
        cls = f' class="{theme}"' if theme else ''
        return (f'<figure{cls}><img src="{e(path)}" data-dpi="{dpi}" alt="" loading="lazy">'
                + (f'<figcaption>{e(caption)}</figcaption>' if caption else '') + '</figure>')

    def both(screen, view, item, dpi, caption=None):
        return ''.join(img(screen, view, item[t], dpi, t, caption) for t in ('light', 'dark') if item.get(t))

    views = [(s, v) for s in screens for v in s['views']]
    parts = [f'<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
             f'<title>{e(entry.id)} bench</title><style>{PAGE_STYLE}</style>',
             '<body data-theme="both" data-scale="real"><header>'
             f'<h1>{e(entry.id)} {e(entry.version)}</h1><span class="muted">{len(screens)} screens, rendered '
             f'{started} on the real firmware</span><div class="controls">'
             '<button data-set-theme="light">Light</button><button data-set-theme="dark">Dark</button>'
             '<button data-set-theme="both" aria-pressed="true">Both</button></div><div class="controls">'
             '<button data-set-scale="real" aria-pressed="true" title="Each picture at the size of the glass, by its density">Real size</button>'
             '<button data-set-scale="pixels" title="One pixel of the glass is one pixel here">Pixels</button>'
             '<button data-set-scale="fit">Fit</button></div></header><main>']
    # What it adds
    parts.append('<h2>What your plugin adds</h2><div class="scroll"><table>')
    row = lambda what, items: parts.append(f'<tr><th>{e(what)}</th><td>{items or "<span class=muted>nothing</span>"}</td></tr>')
    row('Tiles', '<br>'.join(f'<b>{e(t["name"])}</b> <span class="muted">{e(t["id"])}</span>: {e(t["sizes"])}, data from '
                             f'{e(t["data"])}, {t["memory"]} bytes each' for t in parts_of['tiles']))
    row('Cards', ', '.join(f'<b>{e(c["name"])}</b> <span class="muted">{e(c["id"])}</span>' for c in parts_of['cards']))
    row('Top bar', ', '.join(f'<b>{e(b["name"])}</b> <span class="muted">{e(b["id"])}</span>' for b in parts_of['bar_items']))
    row('Tap actions', ', '.join(f'<b>{e(a["name"])}</b> on {e(", ".join(a["domains"]))}' for a in parts_of['tap_actions']))
    row('Screensavers', '<br>'.join(f'<b>{e(v["name"])}</b> <span class="muted">{e(v["id"])}</span>: '
                                    + (f'a step of its own' if v['step'] == 'own' else f'a face of the {e(v["step"])} step')
                                    + (f', {v["pictures"]} pictures' if v['pictures'] else '') + (', takes taps' if v['taps'] else '')
                                    + (', moves itself' if v['moves'] else '') + f', {v["memory"]} bytes'
                                    for v in parts_of.get('savers') or []))
    row('Settings', ', '.join(filter(None, ['rows on the screen\'s settings page' if parts_of['settings_rows'] else '',
                                            ', '.join(e(s) for s in parts_of['settings_entities'])])))
    row('Asks Home Assistant', ', '.join(e(q) for q in parts_of['questions']))
    row('Actions in Home Assistant', ', '.join(e(a) for a in parts_of['actions']))
    row('Parts of plugin API 0.9', 'yes' if parts_of['api_0_9'] else '')
    parts.append('</table></div>')
    # The screens
    parts.append('<h2>Screens</h2><div class="scroll"><table><tr><th>Screen</th><th>Glass</th><th>Look</th><th>Grids</th></tr>')
    for screen in screens:
        parts.append(f'<tr><td><b>{e(screen["key"])}</b><br><span class="muted">{e(screen["title"])}</span></td>'
                     f'<td>{screen["width"]} x {screen["height"]} px, {screen["dpi"]} dpi</td><td>{e(screen["look"])}</td>'
                     f'<td>{", ".join(e(v["grid"]) for v in screen["views"]) or "-"}</td>'
                     '</tr>'
                     if not screen.get('error') else
                     f'<tr><td><b>{e(screen["key"])}</b></td><td colspan="3" class="error">{e(screen["error"])}</td></tr>')
    parts.append('</table></div>')
    # What it found
    parts.append(f'<h2>Findings</h2>')
    if findings:
        order = {'error': 0, 'warning': 1, 'info': 2}
        parts.append('<div class="scroll"><table><tr><th></th><th>Screen</th><th>Where</th><th>What</th></tr>')
        for f in sorted(findings, key=lambda f: (order[f['level']], f['screen'])):
            parts.append(f'<tr><td class="{f["level"]}">{f["level"]}</td><td>{e(f["screen"])}</td><td>{e(f["where"])}</td>'
                         f'<td>{e(f["text"])}</td></tr>')
        parts.append('</table></div>')
    else:
        parts.append('<p class="muted">Nothing: every part lies inside its tile or card, no label is cut, no character is '
                     'missing and every word can be read, light and dark.</p>')
    # Every tile at every size, per screen and grid
    parts.append('<h2>Tiles</h2><p class="muted">Every size the tile offers on every screen and grid that takes it.</p>')
    examples = list(dict.fromkeys((t['example'], t['name']) for _, v in views for t in v['tiles']))
    for key, name in examples:
        spans = list(dict.fromkeys(t['span'] for _, v in views for t in v['tiles'] if t['example'] == key))
        parts.append(f'<h3>{e(name)}</h3><div class="scroll"><table class="matrix"><tr><th>Size</th>'
                     + ''.join(f'<th>{e(s["key"])}{" " + e(v["grid"]) if v["folder"] else ""}</th>' for s, v in views) + '</tr>')
        for span in spans:
            parts.append(f'<tr><td><b>{e(span)}</b></td>')
            for screen, view in views:
                tile = next((t for t in view['tiles'] if t['example'] == key and t['span'] == span), None)
                size = f'{tile["px"][0]} x {tile["px"][1]} px' if tile else ''
                parts.append(f'<td>{both(screen, view, tile, screen["dpi"], size)}</td>' if tile else '<td class="muted">not here</td>')
            parts.append('</tr>')
        parts.append('</table></div>')
    # Cards, taps, the top bar, settings, pages
    for title, field, caption in (('Cards', 'cards', lambda c: f'{c["id"]}: {c["title"]}'),
                                  ('What a tap opens', 'taps', lambda t: f'{t["name"]} {t["span"]}' + (f', asks {"; ".join(t["actions"])}' if t['actions'] else ''))):
        items = [(s, v, item) for s, v in views for item in v[field]]
        if not items:
            continue
        parts.append(f'<h2>{title}</h2>')
        for screen, view, item in items:
            if field == 'taps':
                parts.append(f'<div class="gallery">{img(screen, view, item["file"], screen["dpi"], None, screen["key"] + ": " + caption(item))}</div>')
            else:
                parts.append(f'<div class="gallery"><b>{e(screen["key"])}</b> <span class="muted">{e(caption(item))}</span><br>'
                             f'{both(screen, view, item, screen["dpi"])}</div>')
    savers = [(s, v, item) for s, v in views for item in v.get('savers') or []]
    if savers:
        parts.append('<h2>Screensavers</h2><p class="muted">In standby, with the screen light and dark: a screensaver is '
                     'drawn dark in both.</p>')
        for screen, view, item in savers:
            parts.append(f'<div class="gallery"><b>{e(screen["key"])}</b> <span class="muted">{e(item["name"])}, '
                         f'{e(item["step"])}</span><br>{both(screen, view, item, screen["dpi"])}</div>')
    bars = [(s, v) for s, v in views if v['bar']]
    if bars:
        parts.append('<h2>Top bar</h2><p class="muted">With its words, and as its icon alone (the editor\'s Show: icon).</p>'
                     + ''.join(f'<div class="gallery"><b>{e(s["key"])}</b><br>{both(s, v, v["bar"], s["dpi"])}'
                               + (both(s, v, v['bar_icon'], s['dpi']) if v.get('bar_icon') else '') + '</div>' for s, v in bars))
    settings = [(s, v, item) for s, v in views for item in v['settings']]
    if settings:
        parts.append('<h2>Settings</h2><div class="gallery">' + ''.join(img(s, v, item['file'], s['dpi'], None, f'{s["key"]}: {item["title"]}')
                                                                      for s, v, item in settings) + '</div>')
    parts.append('<h2>Pages</h2>')
    for screen, view in views:
        parts.append(f'<h3>{e(screen["key"])}{" " + e(view["grid"]) if view["folder"] else ""}</h3><div class="gallery">'
                     + ''.join(img(screen, view, p['file'], screen['dpi'], p['theme'], f'Page {p["page"]}') for p in view['pages']) + '</div>')
    parts.append(f'</main><script>{PAGE_SCRIPT}</script></body></html>')
    (out / 'index.html').write_text('\n'.join(parts) + '\n', encoding='utf-8')


# ---- the run

class Watch:
    """The plugin's files as they were; changed() says when one is newer, added or gone."""

    def __init__(self, folder):
        self.folder = folder
        self.seen = self.stamp()

    def stamp(self):
        return {str(p): p.stat().st_mtime for p in self.folder.rglob('*')
                if p.is_file() and '.preview' not in p.parts and '__pycache__' not in p.parts}

    def differs(self):
        """Whether a file changed, without taking the change: the next changed() still says so."""
        return self.stamp() != self.seen

    def changed(self):
        now = self.stamp()
        if now != self.seen:
            self.seen = now
            return True
        return False


YAML_LINE = re.compile(r'^\s*(?:- )?(?:[\w"\'.-]+:|#|$)')


def build_errors(output):
    """The lines of a failed build that say what is wrong: ESPHome's words under "Failed config" (the YAML around them
    left out), else the compiler's error lines."""
    if 'Failed config' in output:
        said = [line.strip() for line in output.split('Failed config', 1)[1].splitlines()
                if not YAML_LINE.match(line) and not re.match(r'\s*(INFO|WARNING|DEBUG) ', line)]
        if said:
            return said[:12]
    return [line for line in output.splitlines() if 'error' in line.lower()][-12:] or output.splitlines()[-20:]


def once(args, folder, screens, esphome, watch=None):
    """Build and bench every screen once; returns the number that failed."""
    try:
        entry = app_plugins.read_folder(folder)
    except Exception as error:
        print(f'{folder}: the manifest does not read: {error}', flush=True)
        return len(screens), 0
    hardware = sorted(set(entry.manifest['requires']['features']) | set(entry.manifest['provides']))
    if hardware:
        print(f'{entry.id} needs or brings {", ".join(hardware)}: hardware of a screen, which the firmware built for this '
              'computer does not have, so the bench cannot build it. Try it on a screen (docs/TESTING.md).', flush=True)
        return 1, 0
    boards = entry.manifest['boards']
    if boards != 'any':
        kept = [s for s in screens if not s.glass and s.variant.board in boards]
        for s in screens:
            if s not in kept:
                print(f'{s.key}: left out, {entry.id} is for {", ".join(boards)} only', flush=True)
        if not kept:
            print(f'no screen to build: name one of {", ".join(boards)} with --board', flush=True)
            return 1, 0
        screens = kept
    spec = read_preview(folder)
    started = time.strftime('%Y-%m-%d %H:%M')
    results, findings, failed = [], [], 0
    parts_of = adds(entry, folder)
    saver_examples(entry, spec)   # preview.yaml's savers name savers of the manifest, before anything builds
    findings += [{'level': 'warning', 'screen': 'all', 'where': 'manifest and code', 'text': text} for text in parts_of['mismatches']]
    print(f'{entry.id} {entry.version}: {len(parts_of["tiles"])} tiles, {len(parts_of["cards"])} cards, '
          f'{len(parts_of["bar_items"])} top bar items, {len(parts_of["savers"])} screensavers, '
          f'settings rows: {"yes" if parts_of["settings_rows"] else "no"}', flush=True)
    for text in parts_of['mismatches']:
        print(f'  warning: {text}', flush=True)
    for screen in screens:
        build = BenchBuild(screen.variant, glass=screen.glass, window=args.window, tree=REPO, work=args.work, esphome=esphome, plugin=folder)
        result = {'key': screen.key, 'title': screen.title, 'width': screen.width, 'height': screen.height,
                  'dpi': screen.dpi, 'look': screen.look, 'views': []}
        results.append(result)
        t0 = time.monotonic()
        print(f'{screen.key}: building (a first build takes several minutes, later ones only what changed)', flush=True)
        ok, output = build.compile()
        if not ok:
            failed += 1
            errors = build_errors(output)
            result['error'] = 'The build failed: ' + ' | '.join(errors)
            findings.append({'level': 'error', 'screen': screen.key, 'where': 'build', 'text': result['error']})
            print(f'{screen.key}: BUILD FAILED (the whole log: {build.work / "logs"})\n' + '\n'.join(errors), flush=True)
            continue
        t1 = time.monotonic()
        bench = Bench(build, screen, args.out / screen.key, entry, spec, args)
        bench.watch = watch
        try:
            asyncio.run(bench.run())
        except KeyboardInterrupt:
            raise
        except Exception as error:
            failed += 1
            result['error'] = f'{type(error).__name__}: {error} (the screen\'s log: {screen.key}/log.txt)'
            bench.find('error', 'run', result['error'])
            print(f'{screen.key}: STOPPED: {error}', flush=True)
        result['views'] = bench.views
        result['asked'], result['actions'] = sorted(set(a for a in bench.asked if a)), bench.actions
        findings += bench.findings
        pictures = len(list((args.out / screen.key).rglob('*.png')))
        print(f'{screen.key}: {pictures} pictures, {len(bench.findings)} findings (build {t1 - t0:.0f} s, bench '
              f'{time.monotonic() - t1:.0f} s)', flush=True)
    args.out.mkdir(parents=True, exist_ok=True)
    overview(args.out, entry, results)
    index_page(args.out, entry, results, findings, started, parts_of)
    (args.out / 'summary.json').write_text(json.dumps({'plugin': entry.id, 'version': entry.version, 'rendered': started,
                                                       'adds': parts_of, 'screens': results, 'findings': findings}, indent=1) + '\n')
    warnings = sum(f['level'] in ('warning', 'error') for f in findings)
    # The findings that need a change, on the output too: who runs the bench from a terminal (or an assistant) reads
    # them without opening a file.
    for f in [f for f in findings if f['level'] in ('error', 'warning')][:30]:
        print(f'  {f["level"]}: {f["screen"]}, {f["where"]}: {f["text"]}', flush=True)
    print(f'{len(screens) - failed} of {len(screens)} screens rendered, {warnings} warnings; open {args.out / "index.html"}', flush=True)
    return failed, warnings


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('plugin', type=Path, help='the plugin\'s folder (the one with tessera-plugin.yaml)')
    parser.add_argument('--board', action='append', default=[], help='a board of boards.yaml, with -portrait for one '
                        f'standing up (default: {", ".join(DEFAULT_BOARDS)}); more than once for several')
    parser.add_argument('--all', action='store_true', help='every board of the catalog, lying down and standing up')
    parser.add_argument('--screen', action='append', default=[], help='a glass of your own: 800x480@7 (pixels and '
                        'inches) or 800x480:133 (pixels and dpi), /compact or /standard to choose the look')
    parser.add_argument('--grid', action='append', default=[], help='another grid on every screen that takes it, as 4x3, '
                        'or max for the finest it takes; more than once for several')
    parser.add_argument('--out', type=Path, help='where the pictures go (default: <plugin>/.preview)')
    parser.add_argument('--work', type=Path, help='where the builds go (default: tools/.cache/bench-build)')
    parser.add_argument('--live', action='store_true', help='ask the web services themselves instead of preview.yaml\'s '
                        'answers (secrets from TESSERA_SECRET_<INPUT ID>)')
    parser.add_argument('--window', action='store_true', help='keep the screen open in its window afterwards, to tap and '
                        'swipe with the mouse (one screen)')
    parser.add_argument('--watch', action='store_true', help='build and render again whenever a file of the plugin changes')
    parser.add_argument('--strict', action='store_true', help='exit with 2 when a finding is a warning, so a script or an '
                        'assistant knows the plugin is not done')
    parser.add_argument('--list', action='store_true', help='list the boards and stop')
    args = parser.parse_args()
    if args.list:
        for key in all_screens():
            print(f'{key:28} {board_screen(key, 0).title}')
        return 0
    folder = args.plugin.resolve()
    if not (folder / 'tessera-plugin.yaml').is_file():
        raise SystemExit(f'{folder} has no tessera-plugin.yaml')
    for text in args.grid:
        if text != 'max' and not re.fullmatch(r'[1-8]x[1-8]', text):
            raise SystemExit(f'--grid {text}: columns x rows, as 4x3, or max')
    args.out = (args.out or folder / '.preview').resolve()
    args.work = (args.work or Path(os.environ.get('TESSERA_BENCH_WORK') or HERE / '.cache' / 'bench-build')).resolve()
    keys = all_screens() if args.all else args.board or ([] if args.screen else list(DEFAULT_BOARDS))
    screens = [board_screen(key, i) for i, key in enumerate(dict.fromkeys(keys))]
    screens += [custom_screen(text, i) for i, text in enumerate(args.screen)]
    if args.window and len(screens) != 1:
        raise SystemExit('--window shows one screen: name it with --board or --screen')
    busy = [str(s.variant.port) for s in screens if not run.free(s.variant.port)]
    if busy:
        raise SystemExit(f'ports {", ".join(busy)} are in use: another bench is running; stop it first')
    esphome = shlex.split(os.environ.get('ESPHOME', 'esphome'))
    watch = Watch(folder) if args.watch else None
    try:
        failed, warnings = once(args, folder, screens, esphome, watch)
        while watch:
            if not watch.changed():
                print('watching the plugin\'s files; Ctrl-C to stop', flush=True)
                while not watch.changed():
                    time.sleep(1)
            print('a file changed: building again', flush=True)
            failed, warnings = once(args, folder, screens, esphome, watch)
    except KeyboardInterrupt:
        print('stopped', flush=True)
        return 130
    if failed:
        return 1
    if args.strict and warnings:
        print(f'--strict: {warnings} warnings; see the findings in {args.out / "summary.json"}', flush=True)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
