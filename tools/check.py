#!/usr/bin/env python3
"""Check plugins the way the Tessera app reads them, before a push or a pull request.

    python3 tools/check.py                      # every folder in plugins/ and template/
    python3 tools/check.py plugins/my_plugin    # one plugin (any folder with a tessera-plugin.yaml)

It runs the app's own manifest check: plugin_manifest.py from the Tessera repository (homeassistant_espscreen,
screen_manager/app/plugin_manifest.py), fetched into tools/.cache/ and fetched again when it is an hour old (offline,
the copy there serves). The same file decides in the app whether a
plugin is shown, so a plugin that passes here is read the same way there. Then the rules the manifest check cannot see:
an `api` the core can build (docs/FIRMWARE_API.md, "Versions"), none of the template's placeholders outside template/,
the folder layout, the translations (English complete), the README and the changelog, the licence, the drawing rules for the firmware
code (docs/FIRMWARE_API.md, "Rules"), what plugin.yaml may set (docs/LIMITS.md, "plugin.yaml"), and, across the index,
that every plugin and feature a plugin needs is there (docs/MANIFEST.md, "What it needs and brings").

TESSERA_MANIFEST=/path/to/plugin_manifest.py uses a local copy instead (a checkout of the Tessera repository), and
TESSERA_BOARDS=/path/to/boards.json the boards a local checkout knows (screen_manager/app/boards.json).

It needs Python 3 with PyYAML (pip install pyyaml).
"""
import ast
import datetime
import importlib.util
import json
import os
import re
import sys
import time
import urllib.request
from pathlib import Path

try:
    import yaml
except ImportError:
    raise SystemExit('tools/check.py needs PyYAML to read the YAML files: pip install pyyaml') from None

ROOT = Path(__file__).resolve().parents[1]
SOURCE = 'https://raw.githubusercontent.com/MaxGramser/homeassistant_espscreen/{ref}/screen_manager/app/{name}'
CACHE = ROOT / 'tools' / '.cache'

# Code rules for a plugin's C++ (docs/FIRMWARE_API.md): what the core forbids itself, so a plugin draws like the rest.
# CODE_RULES hold for every file; SCREEN_RULES for a component that uses the plugin API (its tiles, its top bar), and
# not for a driver of hardware beside it that never touches the screen, which may run a task of its own as ESPHome's
# own drivers do (a camera's capture, docs/LIMITS.md).
CODE_RULES = [
    (re.compile(r'heap_caps_get_largest_free_block|heap_caps_get_info'), 'a walk of the heap: it shifts the picture of an RGB panel'),
    (re.compile(r'http_request|HTTPClient|esp_http_client|WiFiClient'), 'a connection from the screen: data comes through the app (fetch)'),
]
SCREEN_RULES = [
    (re.compile(r'lv_color_hex\s*\(|0x[0-9a-fA-F]{6}\b'), 'a colour as a number: use a theme role (tessera::ui::color)'),
    (re.compile(r'LV_EVENT_CLICKED|LV_EVENT_PRESSED'), 'an LVGL tap of its own: use Tile::on_tap, which has the touch filter in front'),
    (re.compile(r'lv_timer_create|xTaskCreate|delay\s*\(\s*\d'), 'a timer, a task or a wait of its own: use on_tick'),
    (re.compile(r'lv_font_t\s+\w+\s*=|font:\s*$|LV_FONT_DECLARE'), 'a font of its own: use the screen\'s fonts (tessera::Font)'),
]
SCREEN_CODE = re.compile(r'plugin_api\.h|\btessera::|\blv_')
# Parts of the C++ API by the minor that brought them (docs/FIRMWARE_API.md, "Versions"): a plugin that uses one names
# at least that minor in `api`, or a core before it stops its build with a compiler error.
# A member a tile or a card keeps as a std::string or a std::vector (a name ending in _, the plugins' own way): it lives in
# the memory inside the chip, which Wi-Fi needs, where tessera::Text and tessera::List keep it in PSRAM (plugin API 0.10,
# docs/FIRMWARE_API.md, "Memory"). A hint, not a failure: a short word costs little.
KEPT_IN_CHIP = re.compile(r'^\s+(?:const\s+)?std::(?:string|vector<[^;]*>)\s+\w+_\s*(?:[;={]|,)', re.M)
API_PARTS = [((0, 9), re.compile(r'Font::DIGITS|\bui::covers\s*\(|Tone::GOOD|\bui::set_fill\s*\(|\bui::tone_color\s*\('),
               'Font::DIGITS, ui::covers, Tone::GOOD, ui::set_fill and ui::tone_color'),
             # A screensaver (docs/FIRMWARE_API.md, "A screensaver"): the manifest's `savers` needs 0.14 as well.
             ((0, 14), re.compile(r'\badd_saver\s*\(|\bSaver(?:Context|Step)?\b|\bui::takes_taps\s*\(|\btessera::wish\s*\('
                                  r'|\bWish::|\btessera::drift\s*\('),
              'a screensaver (add_saver, Saver, SaverContext, SaverStep), ui::takes_taps, tessera::wish and tessera::drift'),
             # A popup (docs/FIRMWARE_API.md, "A popup").
             ((0, 18), re.compile(r'\bshow_popup\s*\(|\bclose_popup\s*\(|\bpopup_shown\s*\(|\bPopup(?:Context|Width)?\b'
                                  r'|\bui::animate\s*\(|\bui::still\s*\(|\bMotion::'),
              'a popup (show_popup, close_popup, popup_shown, Popup, PopupContext, PopupWidth), ui::animate and ui::still')]

# Forms of the C++ API a minor took away (docs/FIRMWARE_API.md, "Versions"): a plugin that still uses one fails its
# screen's build at that call on a core of that minor or later, whatever its `api` says, since such a minor is not in
# PLUGIN_API_BREAKS (that would refuse every plugin written before it). So the check refuses the form itself.
REMOVED_PARTS = [((0, 24), re.compile(r'\bvoid\s+on_touch\s*\('),
                  'Plugin::on_touch went in plugin API 0.24: use on_press'),
                 ((0, 24), 'action-key-value',
                  'the key and value form of tessera::action went in plugin API 0.24: pass the fields as a JSON object, '
                  'action(service, entity, fields)')]


def call_arguments(code, name):
    """Where `name(` is called in `code`, with how many arguments it has at its own depth."""
    for match in re.finditer(re.escape(name) + r'\s*\(', code):
        depth, count, i = 1, 1, match.end()
        empty = True
        while i < len(code) and depth:
            c = code[i]
            if c in '([{':
                depth += 1
            elif c in ')]}':
                depth -= 1
            elif c == ',' and depth == 1:
                count += 1
            elif c == '"':
                i += 1
                while i < len(code) and code[i] != '"':
                    i += 2 if code[i] == '\\' else 1
            if not c.isspace() and depth:
                empty = False
            i += 1
        yield match.start(), 0 if empty else count


def removed_part(code, rule):
    """The offset of the first use of a removed form in `code`, or None."""
    if rule == 'action-key-value':
        return next((at for at, count in call_arguments(code, 'tessera::action') if count >= 4), None)
    match = rule.search(code)
    return match.start() if match else None


# What plugin.yaml (and a part's file) never sets at its top level (docs/LIMITS.md, "plugin.yaml"). It is merged into
# the screen's own configuration, so these would change what the core and the screen's own YAML hold, or open the
# screen to the network and fetch from it, where a plugin's data comes through the app.
OWNED_KEYS = ('esphome', 'esp32', 'esp8266', 'rp2040', 'psram', 'wifi', 'ethernet', 'network', 'api', 'ota', 'logger',
              'improv_serial', 'esp32_improv', 'external_components', 'packages', 'substitutions')
NETWORK_KEYS = ('web_server', 'captive_portal', 'mqtt', 'http_request', 'online_image', 'socket', 'udp',
                'packet_transport', 'wireguard')
# One exception to NETWORK_KEYS: ESPHome's own update of the Wi-Fi co-processor beside an ESP32-P4 (esp32_hosted), from
# ESPHome's own firmware site, which needs http_request. Only in a file that holds that update and nothing else, and it
# installs only when someone presses Install in Home Assistant (docs/LIMITS.md, "plugin.yaml").
COPROCESSOR_SOURCE = 'https://esphome.github.io/esp-hosted-firmware/manifest/'
COPROCESSOR_KEYS = {'platform', 'type', 'source', 'name', 'id', 'icon', 'update_interval', 'entity_category',
                    'disabled_by_default'}
# Under these keys an ESPHome configuration does something (an automation), and an `id:` there names a part that
# exists, it does not make one.
ACTION_KEYS = re.compile(r'^(on_.+|.+_action|then|else)$')


def fetched(name, env, max_age=3600):
    """A file of the Tessera repository's screen_manager/app/: the path in `env` when set, else a copy in tools/.cache/,
    fetched again when it is older than `max_age` seconds (offline, the copy there serves)."""
    local = os.environ.get(env)
    if local:
        return Path(local)
    path = CACHE / name
    if not path.is_file() or time.time() - path.stat().st_mtime > max_age:
        path.parent.mkdir(parents=True, exist_ok=True)
        ref = os.environ.get('TESSERA_REF', 'dev')
        try:
            with urllib.request.urlopen(SOURCE.format(ref=ref, name=name), timeout=20) as response:
                path.write_bytes(response.read())
        except OSError:
            if not path.is_file():
                raise
    return path


def manifest_module(max_age=3600):
    path = fetched('plugin_manifest.py', 'TESSERA_MANIFEST', max_age)
    spec = importlib.util.spec_from_file_location('plugin_manifest', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PM = None


def pm(fresh=False):
    """The app's manifest module. `fresh`: fetched again now, not from a copy up to an hour old (the index names the API
    of the core as it is)."""
    global PM
    if PM is None:
        PM = manifest_module(0 if fresh else 3600)
    return PM


def plugin_api():
    return '.'.join(map(str, pm().PLUGIN_API))


def api_problem(wanted):
    """Why a plugin written for plugin API `wanted` ("0.7") does not build on the core the manifest module belongs to,
    as a sentence, or None. The module's own api_fits decides: the same major, at most the core's minor, and no minor
    after the plugin's own up to the core's that changed a name (PLUGIN_API_BREAKS, docs/FIRMWARE_API.md, "Versions")."""
    module = pm()
    offered = plugin_api()
    breaks = getattr(module, 'PLUGIN_API_BREAKS', {})
    fits = getattr(module, 'api_fits', None)
    match = module.API.match(str(wanted or ''))
    if not match:
        return None    # the manifest check says what is wrong with its form
    major, minor = int(match.group(1)), int(match.group(2))
    if fits is not None and fits(wanted):
        return None
    if major == module.PLUGIN_API[0] and minor <= module.PLUGIN_API[1]:
        broke = [b for b in breaks.get(major, ()) if minor < b <= module.PLUGIN_API[1]]
        if not broke:
            return None
        return (f'api "{wanted}" is from before plugin API {major}.{broke[-1]}, which changed a name, and the core '
                f'offers plugin API {offered}; update the plugin to it')
    if major == module.PLUGIN_API[0]:
        return f'api "{wanted}" is newer than plugin API {offered}, the one the core offers'
    return f'api "{wanted}" has another major than plugin API {offered}, the one the core offers'


BOARDS = None


def board_features():
    """{board key: {features}}: what each board brings itself, from the core's boards.json (written from boards.yaml),
    read like the manifest module. A board without a `features` list brings none."""
    global BOARDS
    if BOARDS is None:
        BOARDS = {}
        try:
            shapes = json.loads(fetched('boards.json', 'TESSERA_BOARDS').read_text(encoding='utf-8'))
        except (OSError, ValueError) as error:
            fail('boards.json', f'cannot read the boards of the Tessera repository ({error})')
        for key, shape in shapes.items():
            if isinstance(shape, dict):
                features = shape.get('features') or []
                BOARDS.setdefault(str(shape.get('board') or key), set()).update(
                    f for f in features if isinstance(f, str))
    return BOARDS


# Where the docs name the API the core offers now. The phrase "plugin API <n>" (lowercase plugin) always means the
# current one; a part's own version is written "API 0.2" or "(0.2)". So the docs cannot fall behind the core unnoticed.
DOC_FILES = ('README.md', 'AGENTS.md', 'llms.txt', 'docs/FIRMWARE_API.md', 'docs/MANIFEST.md', 'docs/MAKING_A_PLUGIN.md')
CURRENT_API = re.compile(r'plugin API (?:is )?(\d+\.\d+)')


# Where the docs list a plugin's files or say what a new version takes: each names the changelog.
CHANGELOG_DOCS = ('README.md', 'AGENTS.md', 'llms.txt', 'docs/MAKING_A_PLUGIN.md', 'docs/MANIFEST.md',
                  'docs/PUBLISHING.md', 'docs/TESTING.md', 'docs/TRANSLATIONS.md')


def check_docs():
    """Every doc that names the plugin API the core offers names the one the manifest check knows, and every doc that
    lists a plugin's files or says what a new version takes names the changelog."""
    wanted = plugin_api()
    for name in DOC_FILES:
        text = (ROOT / name).read_text(encoding='utf-8')
        found = CURRENT_API.findall(text)
        if not found:
            fail(name, f'names no plugin API; it should say "plugin API {wanted}" where it means the current one')
        stale = sorted(set(found) - {wanted})
        if stale:
            fail(name, f'says plugin API {", ".join(stale)}; the core offers {wanted}')
    for name in CHANGELOG_DOCS:
        if 'CHANGELOG.md' not in (ROOT / name).read_text(encoding='utf-8'):
            fail(name, 'names no CHANGELOG.md; every plugin has one (docs/MAKING_A_PLUGIN.md, "The changelog")')


def fail(folder, message):
    raise SystemExit(f'{folder}: {message}')


# ---- The README and the changelog: texts the app shows, per language ----

# The app reads at most this much of each (screen_manager/app/plugins.py), so a longer one fails here instead.
TEXT_LIMIT = 64 * 1024


def texts(folder, name, stem):
    """{language: text} of a plugin's <stem>.md (English) and <stem>.<language>.md, named as the app reads them
    (README.nl.md, CHANGELOG.pt-BR.md). SystemExit when one is longer than the app reads."""
    pattern = re.compile(rf'{stem}(?:\.([a-z]{{2}}(?:-[A-Za-z]{{2}})?))?\.md')
    out = {}
    for path in sorted(Path(folder).iterdir()):
        match = pattern.fullmatch(path.name)
        if not match or not path.is_file():
            continue
        text = path.read_text(encoding='utf-8')
        if len(text) > TEXT_LIMIT:
            fail(name, f'{path.name} is longer than the {TEXT_LIMIT // 1024} KB the app reads')
        out[match.group(1) or 'en'] = text
    return out


CHANGELOG_HEADING = re.compile(r'## (\S+?)(?: - (\S+))?')


def real_date(text):
    """Whether a text is a day of the calendar written YYYY-MM-DD."""
    try:
        return bool(re.fullmatch(r'\d{4}-\d{2}-\d{2}', text)) and bool(datetime.date.fromisoformat(text))
    except ValueError:
        return False


def changelog_versions(name, file, text):
    """The versions of a changelog, newest first, or SystemExit with what is wrong. One `## <version>` heading per
    version, optionally ` - <YYYY-MM-DD>`, newest first; under each, bullet lines (`- `, a longer one goes on indented
    by two spaces). An optional `# ` title before the first version, nothing else."""
    versions, bullets = [], 0
    for number, line in enumerate(text.splitlines(), 1):
        where = f'{file}:{number}'
        if not line.strip():
            continue
        if line.startswith('#'):
            if line.startswith('# ') and not versions and number == 1:
                continue
            match = CHANGELOG_HEADING.fullmatch(line.rstrip())
            if not match or not pm().VERSION.match(match.group(1)):
                fail(name, f'{where}: a heading is "## <version>" (three numbers), optionally " - <YYYY-MM-DD>"')
            if match.group(2) and not real_date(match.group(2)):
                fail(name, f'{where}: the date is YYYY-MM-DD, such as 2026-10-09')
            version = tuple(int(n) for n in match.group(1).split('.'))
            if versions and not version < versions[-1][0]:
                fail(name, f'{where}: {match.group(1)} comes after {versions[-1][1]}; the newest version goes first, '
                           f'each once')
            if versions and not bullets:
                fail(name, f'{file}: {versions[-1][1]} has no lines; say what changed in it')
            versions.append((version, match.group(1)))
            bullets = 0
        elif not versions:
            fail(name, f'{where}: the first version heading ("## <version>") comes before any text')
        elif line.startswith('- '):
            bullets += 1
        elif line.startswith('  ') and bullets:
            continue
        else:
            fail(name, f'{where}: under a version, each line is a bullet ("- "), a longer one goes on indented by two '
                       f'spaces')
    if versions and not bullets:
        fail(name, f'{file}: {versions[-1][1]} has no lines; say what changed in it')
    return [v[1] for v in versions]


def check_changelog(name, version, changelog):
    """CHANGELOG.md starts with the manifest's version and lists versions newest first; a translation lists only
    versions the English one has (it may lag behind)."""
    if 'en' not in changelog:
        fail(name, 'CHANGELOG.md is required: a "## <version>" heading per version, newest first')
    english = changelog_versions(name, 'CHANGELOG.md', changelog['en'])
    if not english or english[0] != version:
        fail(name, f'CHANGELOG.md: its first heading is "## {version}", the version in tessera-plugin.yaml')
    for language, text in sorted(changelog.items()):
        if language == 'en':
            continue
        extra = set(changelog_versions(name, f'CHANGELOG.{language}.md', text)) - set(english)
        if extra:
            fail(name, f'CHANGELOG.{language}.md: versions CHANGELOG.md does not have: {", ".join(sorted(extra))}')


# ---- plugin.yaml: ESPHome's YAML, read as data ----

class Tagged(str):
    """A value with one of ESPHome's tags (!lambda, !secret, !include, !extend, !remove, ...), kept as its plain text."""
    tag = ''


class TaggedMap(dict):
    """A mapping with a tag (`!include {file: x, vars: {...}}`), kept as the mapping."""
    tag = ''


class EsphomeLoader(yaml.SafeLoader):
    """Reads ESPHome YAML without ESPHome: a tag is no error, its value stays as it is written."""


def _tagged(loader, suffix, node):
    if isinstance(node, yaml.SequenceNode):
        return loader.construct_sequence(node, deep=True)
    if isinstance(node, yaml.MappingNode):
        value = TaggedMap(loader.construct_mapping(node, deep=True))
        value.tag = '!' + suffix
        return value
    value = Tagged(loader.construct_scalar(node))
    value.tag = '!' + suffix
    return value


EsphomeLoader.add_multi_constructor('!', _tagged)


def esphome_yaml(folder, name, file):
    """A plugin's ESPHome file as data, or SystemExit."""
    try:
        data = yaml.load((folder / file).read_text(encoding='utf-8'), Loader=EsphomeLoader)
    except (OSError, yaml.YAMLError) as error:
        fail(name, f'{file}: {error}')
    if data is None:
        return {}
    if not isinstance(data, dict):
        fail(name, f'{file}: an ESPHome configuration is a mapping of components')
    return data


def tagged_values(node):
    """Every tagged value in an ESPHome file, however deep."""
    if isinstance(node, (Tagged, TaggedMap)):
        yield node
    if isinstance(node, dict):
        for value in node.values():
            yield from tagged_values(value)
    elif isinstance(node, list):
        for value in node:
            yield from tagged_values(value)


def entries_of(value):
    """The entries of a component: a list of mappings, or one mapping."""
    return [item for item in (value if isinstance(value, list) else [value]) if isinstance(item, dict)]


def made_ids(node):
    """Every id an ESPHome configuration makes (`id: x`), not one an automation names or one it extends (`!extend x`)."""
    if isinstance(node, dict):
        for key, value in node.items():
            if key == 'id' and isinstance(value, str):
                if not isinstance(value, Tagged):
                    yield value
            elif not ACTION_KEYS.match(str(key)):
                yield from made_ids(value)
    elif isinstance(node, list):
        for item in node:
            yield from made_ids(item)


def coprocessor_update(data):
    """Whether an ESPHome file is only the update of the Wi-Fi co-processor (COPROCESSOR_SOURCE): `http_request:` without
    settings and `update:` entries of platform esp32_hosted, type http, from ESPHome's firmware site."""
    if set(data) != {'http_request', 'update'} or data['http_request'] not in (None, {}):
        return False
    raw = data['update'] if isinstance(data['update'], list) else [data['update']]
    updates = entries_of(data['update'])
    return bool(updates) and len(updates) == len(raw) and all(
        u.get('platform') == 'esp32_hosted' and u.get('type') == 'http' and set(u) <= COPROCESSOR_KEYS
        and isinstance(u.get('source'), str) and u['source'].startswith(COPROCESSOR_SOURCE) for u in updates)


def check_esphome(folder, name, file, provides):
    """The rules for an ESPHome file of a plugin: no key the core owns or that opens the screen, and an id with
    Tessera's prefix only for a feature the plugin brings. Returns the file as data."""
    data = esphome_yaml(folder, name, file)
    for key in data:
        if key in OWNED_KEYS:
            fail(name, f'{file}: "{key}" belongs to the core and the screen\'s own YAML; a plugin never sets it')
        if key in NETWORK_KEYS and not (key == 'http_request' and coprocessor_update(data)):
            fail(name, f'{file}: "{key}" opens the screen to the network or fetches from it; data comes through the app')
    for tagged in tagged_values(data):
        # The screen's secrets.yaml (its Wi-Fi, its keys) is the screen's: a plugin's YAML never reads it.
        if tagged.tag == '!secret':
            fail(name, f'{file}: !secret {tagged}: the screen\'s secrets are not a plugin\'s to read; a value of a plugin '
                       f'is an input of its manifest (docs/MANIFEST.md, "Inputs")')
        # An include stays in the plugin's folder: not a file of the computer the screen is built on.
        if tagged.tag.startswith('!include'):
            where = tagged.get('file', '') if isinstance(tagged, dict) else str(tagged)
            if str(where).startswith('/') or '..' in Path(str(where)).parts or '~' in str(where):
                fail(name, f'{file}: {tagged.tag} {where}: an include names a file in the plugin\'s folder, by a relative path')
    promised = {pm().FEATURES[feature][1] for feature in provides}
    for made in made_ids(data):
        if made.startswith('ts_') and made not in promised:
            fail(name, f'{file}: id {made}: ids that start with ts_ are Tessera\'s; a plugin makes one only for a '
                       f'feature it provides (docs/MANIFEST.md, "Features")')
    return data


def check_provides(name, manifest, esphome):
    """A plugin that provides a feature makes it: the feature's ESPHome component with the feature's id."""
    for feature in manifest['provides']:
        domain, wanted = pm().FEATURES[feature]
        ids = [item.get('id') for item in entries_of(esphome.get(domain))]
        if not any(isinstance(i, str) and not isinstance(i, Tagged) and i == wanted for i in ids):
            fail(name, f'plugin.yaml: provides {feature}, so it makes a "{domain}:" with id: {wanted}')


# ---- A component's Python ----
# ESPHome runs a component's __init__.py, and what it imports, where the screen is built: in the Tessera app's own
# container, beside Home Assistant's config folder and on its network. A plugin's Python does what a component needs
# and no more (docs/PUBLISHING.md, "What a plugin's Python may do"): it imports ESPHome and a little of the standard
# library, reads files beside itself, and never reaches that computer's files, environment, processes or network.
# Read as a syntax tree, not with a pattern, so a name in a string or a comment neither passes nor fails. Tessera's own
# plugins keep to the same rules. A way around them for someone who wants one exists (nothing static holds every
# door); what they stop is the plain `import os` and the honest slip, and they make the rest stand out in a diff.
PYTHON_MODULES = {'esphome', 'pathlib', 'json', 're', 'math', 'struct', 'typing', 'dataclasses', 'enum', 'functools',
                  'itertools', 'collections', 'logging', 'voluptuous', 'string', 'textwrap', 'base64', 'hashlib',
                  'datetime', 'copy', 'operator', 'decimal', 'fractions', 'numbers', 'abc', 'contextlib', 'warnings'}
# ESPHome's own parts that run commands, fetch, or read and write beyond a build: not a component's to use.
PYTHON_NEVER = ('esphome.util', 'esphome.git', 'esphome.external_files', 'esphome.dashboard', 'esphome.storage_json',
                'esphome.espota2', 'esphome.wizard', 'esphome.zeroconf', 'esphome.mqtt', 'esphome.helpers',
                'esphome.__main__', 'esphome.log', 'esphome.vscode', 'esphome.platformio_api', 'esphome.idf_component')
PYTHON_CALLS = {'open', 'eval', 'exec', 'compile', '__import__', 'globals', 'locals', 'vars', 'breakpoint', 'input',
                'exit', 'quit', 'memoryview'}
# A file beside the plugin is read through a Path made from __file__; nothing is written, and nothing else is read.
PYTHON_READS = {'read_text', 'read_bytes', 'open', 'iterdir', 'glob', 'rglob', 'exists', 'is_file', 'is_dir', 'stat',
                'walk', 'samefile', 'readlink'}
PYTHON_WRITES = {'write_text', 'write_bytes', 'unlink', 'mkdir', 'rmdir', 'rename', 'replace', 'touch', 'chmod',
                 'symlink_to', 'hardlink_to', 'link_to', 'lchmod', 'expanduser'}
PYTHON_DUNDERS = {'__file__', '__name__', '__doc__', '__init__', '__call__', '__eq__', '__hash__', '__repr__', '__str__',
                  '__lt__', '__le__', '__gt__', '__ge__', '__len__', '__iter__', '__getitem__', '__contains__',
                  '__post_init__', '__slots__', '__enter__', '__exit__', '__version__', '__all__'}
PYTHON_PLACES = ('/config', '/data', '/homeassistant', '/root', '/home', '/etc', '/proc', '/sys', '/run', '/var', '/tmp',
                 '/ssl', '/share', '/media', '/addons', '/backup', '/usr', '/opt', '/mnt')


def check_python(folder, name, path):
    """The rules above for one Python file of a plugin's component."""
    rel = path.relative_to(folder)
    try:
        tree = ast.parse(path.read_text(encoding='utf-8'), filename=str(path))
    except (SyntaxError, ValueError) as error:
        fail(name, f'{rel}: {error}')

    def no(node, why):
        fail(name, f'{rel}:{node.lineno}: {why} (docs/PUBLISHING.md, "What a plugin\'s Python may do")')

    # Names made from __file__ at the top of the file (HERE = Path(__file__).parent): a path beside the plugin as well.
    beside = {'__file__'}

    def beside_file(expr):
        return any(isinstance(n, ast.Name) and n.id in beside for n in ast.walk(expr))

    for statement in tree.body:
        if isinstance(statement, ast.Assign) and beside_file(statement.value):
            beside.update(t.id for t in statement.targets if isinstance(t, ast.Name))
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            modules = [(alias.name, alias.name) for alias in node.names]
        elif isinstance(node, ast.ImportFrom):
            if node.level:
                continue   # the component's own files
            modules = [(node.module or '', f'{node.module}.{alias.name}') for alias in node.names]
        else:
            modules = []
        for module, full in modules:
            if module.split('.')[0] not in PYTHON_MODULES:
                no(node, f'import {module}: a component imports esphome and the standard library\'s '
                         f'{", ".join(sorted(PYTHON_MODULES - {"esphome"}))}, nothing else')
            if any(full == never or full.startswith(never + '.') for never in PYTHON_NEVER):
                no(node, f'import {full}: that part of ESPHome runs commands, fetches or reaches files of its own')
        if isinstance(node, ast.Call):
            func = node.func
            if isinstance(func, ast.Name) and func.id in PYTHON_CALLS:
                no(node, f'{func.id}(): a component has no use for it')
            if isinstance(func, ast.Attribute):
                if func.attr in PYTHON_WRITES:
                    no(node, f'.{func.attr}(): a build of a plugin writes no file')
                if func.attr in PYTHON_READS and not beside_file(func.value):
                    no(node, f'.{func.attr}(): a plugin reads only files beside itself, through a Path from __file__')
        elif isinstance(node, ast.Attribute) and node.attr.startswith('__') and node.attr not in PYTHON_DUNDERS:
            no(node, f'.{node.attr}: a way around these rules')
        elif isinstance(node, ast.Name) and node.id in {'__builtins__', '__loader__', '__spec__', '__import__', '__dict__'}:
            no(node, f'{node.id}: a way around these rules')
        elif isinstance(node, ast.Constant) and isinstance(node.value, str):
            if any(node.value == place or node.value.startswith(place + '/') for place in PYTHON_PLACES):
                no(node, f'"{node.value}": a folder of the computer the screen is built on')
            # getattr(x, "__dict__"), "__import__" and the like: a dunder in a string is the same door as the attribute.
            if re.fullmatch(r'__\w+__', node.value) and node.value not in PYTHON_DUNDERS:
                no(node, f'"{node.value}": a way around these rules')


# What template/ holds until a maker fills it in (tools/new_plugin.py copies it as it is): a plugin other than the
# template itself may not keep it.
TEMPLATE_MAINTAINER = 'your-github-name'
TEMPLATE_LICENSE = '<year> <your name>'
TEMPLATE_README = re.compile(r'This is the template for a Tessera plugin', re.I)


STORE_IMAGE = 'store.png'
STORE_IMAGE_MAX = 1024 * 1024
PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'


def check_store_image(folder, name):
    """The picture of the plugin on the Plugins page, when it has one: store.png beside the manifest, a PNG of 16:9 from
    640 x 360 to 2560 x 1440 and at most 1 MB (docs/PUBLISHING.md, "The picture on the Plugins page"). Read from its
    header, so no image library is needed."""
    for other in ('store.jpg', 'store.jpeg', 'store.webp', 'store.gif'):
        if (folder / other).is_file():
            fail(name, f'{other}: the picture on the Plugins page is store.png, a PNG')
    path = folder / STORE_IMAGE
    if not path.is_file():
        return None
    body = path.read_bytes()
    if len(body) > STORE_IMAGE_MAX:
        fail(name, f'{STORE_IMAGE}: {len(body) // 1024} KB, at most {STORE_IMAGE_MAX // 1024} KB')
    if not body.startswith(PNG_SIGNATURE) or body[12:16] != b'IHDR':
        fail(name, f'{STORE_IMAGE}: not a PNG')
    width, height = int.from_bytes(body[16:20], 'big'), int.from_bytes(body[20:24], 'big')
    if width * 9 != height * 16 or not 640 <= width <= 2560:
        fail(name, f'{STORE_IMAGE}: {width} x {height}; it is 16:9, from 640 x 360 to 2560 x 1440 (1280 x 720 is best)')
    return STORE_IMAGE


def check_folder(folder):
    """(raw manifest, translations, readmes, changelogs) of a plugin folder, or SystemExit with what is wrong."""
    folder = Path(folder)
    name = folder.name
    try:
        raw = yaml.safe_load((folder / 'tessera-plugin.yaml').read_text(encoding='utf-8'))
    except (OSError, yaml.YAMLError) as error:
        fail(name, f'tessera-plugin.yaml: {error}')
    translations = {}
    for path in sorted((folder / 'translations').glob('*.json')):
        try:
            translations[path.stem] = json.loads(path.read_text(encoding='utf-8'))
        except ValueError as error:
            fail(name, f'translations/{path.name}: {error}')
    if 'en' not in translations:
        fail(name, 'translations/en.json is required')
    try:
        manifest = pm().check(raw, translations['en'])
    except pm().ManifestError as error:
        fail(name, f'tessera-plugin.yaml: {error}')
    if manifest['id'] != name and folder.parent.name == 'plugins':
        fail(name, f'the folder must be called {manifest["id"]}, as the plugin\'s id')
    problem = api_problem(manifest['api'])
    if problem:
        fail(name, f'tessera-plugin.yaml: {problem} (docs/FIRMWARE_API.md, "Versions")')
    template = folder.resolve() == (ROOT / 'template').resolve()
    if not template and manifest['maintainer'] == TEMPLATE_MAINTAINER:
        fail(name, f'tessera-plugin.yaml: maintainer is still the template\'s {TEMPLATE_MAINTAINER}; write your GitHub name')
    english = set(translations['en'].get('screen') or {})
    for lang, data in translations.items():
        extra = set(data.get('screen') or {}) - english
        if extra:
            fail(name, f'translations/{lang}.json: screen texts English does not have: {", ".join(sorted(extra))}')
    if not (folder / 'plugin.yaml').is_file():
        fail(name, 'plugin.yaml is required (what a screen gets)')
    check_provides(name, manifest, check_esphome(folder, name, 'plugin.yaml', manifest['provides']))
    for part in manifest['parts']:
        if not (folder / part['file']).is_file():
            fail(name, f'parts: {part["file"]} is not in the folder')
        check_esphome(folder, name, part['file'], manifest['provides'])
    components = folder / 'components'
    if not components.is_dir() or not any(components.iterdir()):
        fail(name, 'components/<name>/ is required (the ESPHome component)')
    readme = texts(folder, name, 'README')
    if 'en' not in readme:
        fail(name, 'README.md is required')
    if not re.search(r'(?m)^##\s+(Set ?up|Setup)\b', readme['en']):
        fail(name, 'README.md needs a "## Set up" section')
    if not template and TEMPLATE_README.search(readme['en']):
        fail(name, 'README.md still says it is the template; say what your plugin does')
    if not template and (folder / 'LICENSE').is_file() \
            and TEMPLATE_LICENSE in (folder / 'LICENSE').read_text(encoding='utf-8', errors='replace'):
        fail(name, f'LICENSE still has the template\'s "{TEMPLATE_LICENSE}": fill in the year and your name')
    changelog = texts(folder, name, 'CHANGELOG')
    check_changelog(name, manifest['version'], changelog)
    check_store_image(folder, name)
    if folder.parent.name == 'plugins' and not (folder / 'LICENSE').is_file() and not (folder.parent.parent / 'LICENSE').is_file():
        fail(name, 'a LICENSE is required')
    for component in sorted(p for p in components.iterdir() if p.is_dir()):
        for path in sorted(component.rglob('*.py')):
            check_python(folder, name, path)
        sources = {path: re.sub(r'//[^\n]*|/\*.*?\*/', '', path.read_text(encoding='utf-8', errors='replace'), flags=re.S)
                   for path in sorted(component.rglob('*')) if path.suffix in ('.h', '.cpp', '.c', '.hpp')}
        screen = any(SCREEN_CODE.search(code) for code in sources.values())
        api = tuple(int(n) for n in str(manifest['api']).split('.'))
        for path, code in sources.items():
            for minor, rule, what in API_PARTS:
                match = rule.search(code)
                if match and api < minor:
                    line = code[:match.start()].count('\n') + 1
                    fail(name, f'{path.relative_to(folder)}:{line}: {what} came with plugin API {minor[0]}.{minor[1]}: '
                               f'the manifest says api "{manifest["api"]}"')
            for minor, rule, why in REMOVED_PARTS:
                at = removed_part(code, rule) if tuple(pm().PLUGIN_API) >= minor else None
                if at is not None:
                    fail(name, f'{path.relative_to(folder)}:{code[:at].count(chr(10)) + 1}: {why}')
            for rule, why in CODE_RULES + (SCREEN_RULES if screen else []):
                match = rule.search(code)
                if match:
                    line = code[:match.start()].count('\n') + 1
                    fail(name, f'{path.relative_to(folder)}:{line}: {why}')
    return raw, translations, readme, changelog


# ---- Across the index: what a plugin needs is there ----

def shares_board(a, b):
    """Whether two plugins' `boards` meet on at least one board (`any` meets everything)."""
    return a == 'any' or b == 'any' or bool(set(a) & set(b))


def needs_problem(manifest, known):
    """What a plugin needs that the index does not have, as a sentence, or None. `known` is {id: checked manifest} of
    every plugin in the index. A plugin it needs must be there and fit one of its boards; a feature it needs (also one a
    part uses) must be brought by a plugin for one of its boards, or by one of its boards itself (boards.json)."""
    for need in manifest['requires']['plugins']:
        other = known.get(need)
        if other is None:
            return f'requires.plugins: {need} is not in the index'
        if not shares_board(manifest['boards'], other['boards']):
            return f'requires.plugins: {need} is made for other boards ({", ".join(other["boards"])})'
    wanted = dict.fromkeys(manifest['requires']['features'])
    for part in manifest['parts']:
        wanted.update(dict.fromkeys(part['features']))
    for feature in wanted:
        if any(feature in other['provides'] and other['id'] != manifest['id']
               and shares_board(manifest['boards'], other['boards']) for other in known.values()):
            continue
        boards = board_features()
        if any(feature in features and (manifest['boards'] == 'any' or board in manifest['boards'])
               for board, features in boards.items()):
            continue
        return f'needs the feature {feature}, and no plugin in the index and no board brings it for its boards'
    return None


def needs_cycle(known):
    """A circle of plugins that need each other, as the ids around it ([a, b, a]), or None."""
    state = {}

    def visit(plugin, path):
        state[plugin] = 'open'
        for need in known[plugin]['requires']['plugins']:
            if need not in known:
                continue
            if state.get(need) == 'open':
                return path[path.index(need):] + [need]
            if need not in state:
                found = visit(need, path + [need])
                if found:
                    return found
        state[plugin] = 'done'
        return None

    for plugin in sorted(known):
        if plugin not in state:
            found = visit(plugin, [plugin])
            if found:
                return found
    return None


def index_manifests():
    """{id: checked manifest} of the community plugins of the committed index.json, read as the app reads them
    (leniently). This repository's own plugins are read from plugins/ instead, as they are now."""
    path = ROOT / 'index.json'
    known = {}
    if not path.is_file():
        return known
    for item in json.loads(path.read_text(encoding='utf-8')).get('plugins') or []:
        if not isinstance(item, dict) or item.get('label') == 'tessera':
            continue
        try:
            manifest = pm().check(item['manifest'], strict=False)
        except (pm().ManifestError, KeyError, TypeError):
            continue
        known[manifest['id']] = manifest
    return known


def own_manifests():
    """{id: checked manifest} of this repository's own plugins, as they are in plugins/ now."""
    known = {}
    for folder in sorted((ROOT / 'plugins').iterdir()) if (ROOT / 'plugins').is_dir() else []:
        if (folder / 'tessera-plugin.yaml').is_file():
            manifest = pm().check(yaml.safe_load((folder / 'tessera-plugin.yaml').read_text(encoding='utf-8')),
                                  strict=False)
            known[manifest['id']] = manifest
    return known


def check_needs(manifests, known):
    """Every plugin of `manifests` ({id: checked}) finds what it needs among `known` and them; no circle. SystemExit."""
    known = {**known, **manifests}
    for plugin, manifest in sorted(manifests.items()):
        problem = needs_problem(manifest, known)
        if problem:
            fail(plugin, problem)
    cycle = needs_cycle(known)
    if cycle:
        fail('requires.plugins', f'plugins that need each other: {" -> ".join(cycle)}')


# ---- The files of this repository ----

ENTRY_FIELDS = {'repo', 'path', 'ref', 'maintainer', 'topics'}
GITHUB = re.compile(r'^https://github\.com/([A-Za-z0-9_.-]+?)/([A-Za-z0-9_.-]+?)(?:\.git)?/?$')
GITHUB_FOLDER = re.compile(r'^https://github\.com/([A-Za-z0-9_.-]+?)/([A-Za-z0-9_.-]+?)(?:\.git)?'
                           r'(?:/tree/[^/]+(?:/(.+?))?)?/?$')


def normal_repo(url):
    """A repository's URL as the index writes and compares it: lowercase owner and name, no .git, no trailing slash."""
    match = GITHUB.match(str(url or '').strip())
    if not match:
        raise ValueError(f'{url} is no GitHub repository')
    return f'https://github.com/{match.group(1).lower()}/{match.group(2).lower()}'


def normal_path(path):
    path = str(path or '.').strip().strip('/')
    while path.startswith('./'):
        path = path[2:]
    return path or '.'


def place(url):
    """(repository, folder) of a link to a plugin: https://github.com/<owner>/<repo>, with /tree/<ref>/<folder> when it is
    in a folder (the ref says nothing about where the plugin lives)."""
    match = GITHUB_FOLDER.match(str(url or '').strip())
    if not match:
        raise ValueError(f'{url} is no link to a GitHub repository or a folder in one')
    return f'https://github.com/{match.group(1).lower()}/{match.group(2).lower()}', normal_path(match.group(3))


def check_entry(path):
    """An entry of community/<id>.yaml: a public GitHub repository of the maintainer, the plugin's folder in it, and
    optionally the release tag to list (the newest release when left out). SystemExit with what is wrong."""
    path = Path(path)
    try:
        entry = yaml.safe_load(path.read_text(encoding='utf-8'))
    except (OSError, yaml.YAMLError) as error:
        fail(path.name, str(error))
    if not isinstance(entry, dict) or set(entry) - ENTRY_FIELDS or not {'repo', 'maintainer'} <= set(entry):
        fail(path.name, f'an entry has repo and maintainer, and may have {", ".join(sorted(ENTRY_FIELDS - {"repo", "maintainer"}))}')
    if not re.fullmatch(r'[a-z][a-z0-9_]{0,31}', path.stem):
        fail(path.name, 'the file is named after the plugin\'s id: community/<id>.yaml')
    match = re.fullmatch(r'https://github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)', str(entry['repo']))
    if not match:
        fail(path.name, 'repo is https://github.com/<owner>/<repository>')
    if str(entry['maintainer']).lower() != match.group(1).lower():
        fail(path.name, 'the maintainer is the owner of the repository')
    if '..' in str(entry.get('path', '.')) or str(entry.get('path', '.')).startswith('/'):
        fail(path.name, 'path is a folder inside the repository')
    return entry


def read_featured():
    """featured.yaml: the ids of the plugins Tessera recommends to start with, in its order."""
    path = ROOT / 'featured.yaml'
    if not path.is_file():
        return []
    try:
        featured = yaml.safe_load(path.read_text(encoding='utf-8')) or []
    except yaml.YAMLError as error:
        fail('featured.yaml', str(error))
    if not isinstance(featured, list) or not all(isinstance(i, str) and re.fullmatch(r'[a-z][a-z0-9_]{0,31}', i)
                                                 for i in featured):
        fail('featured.yaml', 'a list of plugin ids')
    return list(dict.fromkeys(featured))


def read_transfers():
    """transfers.yaml: ids that moved to another repository or folder, with the consent of the one that had it
    (docs/PUBLISHING.md, "Whose id it is"). [{plugin, from: (repo, folder), to: (repo, folder), reason}]."""
    path = ROOT / 'transfers.yaml'
    if not path.is_file():
        return []
    try:
        moves = yaml.safe_load(path.read_text(encoding='utf-8')) or []
    except yaml.YAMLError as error:
        fail('transfers.yaml', str(error))
    out = []
    for i, move in enumerate(moves if isinstance(moves, list) else [None]):
        if not isinstance(move, dict) or set(move) != {'plugin', 'from', 'to', 'reason'} \
                or not re.fullmatch(r'[a-z][a-z0-9_]{0,31}', str(move['plugin'])) or not str(move['reason']).strip():
            fail('transfers.yaml', f'[{i}]: a move is {{plugin, from, to, reason}}')
        try:
            out.append({'plugin': move['plugin'], 'from': place(move['from']), 'to': place(move['to']),
                        'reason': str(move['reason'])})
        except ValueError as error:
            fail('transfers.yaml', f'[{i}]: {error}')
    return out


def kept_in_chip(folder):
    """The first member a component of the plugin API keeps as a std::string or a std::vector, as `file:line`, or None.
    A driver beside it that never touches the screen is not asked (its buffers are its own business)."""
    components = Path(folder) / 'components'
    if not components.is_dir():
        return None
    for path in sorted(components.rglob('*')):
        if path.suffix not in ('.h', '.cpp', '.hpp'):
            continue
        raw = path.read_text(encoding='utf-8', errors='replace')
        if not SCREEN_CODE.search(raw):
            continue
        for number, line in enumerate(raw.splitlines(), 1):
            if not line.lstrip().startswith('//') and KEPT_IN_CHIP.match(line):
                return f'{path.relative_to(folder)}:{number}'
    return None


def main():
    folders = [Path(arg) for arg in sys.argv[1:]] or [
        *(f for f in sorted((ROOT / 'plugins').iterdir()) if (f / 'tessera-plugin.yaml').is_file()), ROOT / 'template']
    checked = {}
    for folder in folders:
        raw, *_ = check_folder(folder)
        manifest = pm().check(raw)
        checked[manifest['id']] = manifest
        template = Path(folder).resolve() == (ROOT / 'template').resolve()
        hint = '' if template or (Path(folder) / STORE_IMAGE).is_file() else \
            f' (no {STORE_IMAGE} yet: python3 tools/store_image.py {folder} makes its picture on the Plugins page)'
        # Text and List came with plugin API 0.10: an older plugin keeps building on released screens as it is.
        kept = kept_in_chip(folder) if tuple(int(n) for n in str(manifest['api']).split('.')) >= (0, 10) else None
        if kept:
            hint += (f' (keeps {kept} in the memory inside the chip: tessera::Text and tessera::List keep it in PSRAM, '
                     'docs/FIRMWARE_API.md "Memory")')
        print(f'{folder.name}: ok{hint}')
    # What the plugins need, against the index as it would be: the community plugins of the committed index.json, this
    # repository's own plugins, and the folders checked (the template needs nothing and brings nothing).
    known = {**index_manifests(), **own_manifests()}
    check_needs(checked if sys.argv[1:] else known, known)
    print('needs: ok')
    if not sys.argv[1:]:
        for entry in sorted((ROOT / 'community').glob('*.yaml')):
            check_entry(entry)
            print(f'community/{entry.name}: ok')
        read_featured()
        read_transfers()
        print('featured.yaml, transfers.yaml: ok')
        check_docs()
        print('docs: ok')
    print(f'plugin API {plugin_api()}: {len(folders)} checked')


if __name__ == '__main__':
    main()
