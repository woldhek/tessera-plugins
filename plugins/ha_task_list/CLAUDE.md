# CLAUDE.md - Tessera HA Task List Plugin

## Project Overview
This project implements a native **Tessera Plugin** (`ha_task_list`) to display, toggle, add, and manage Home Assistant task lists (`todo` domain entities) directly on ESPHome-powered Tessera screens.

- **Plugin ID:** `ha_task_list`
- **Target Runtime:** ESPHome / C++ (`tessera::Tile`) with Home Assistant backend integrations.
- **Supported Sizes:** `1x1` up to `3x4`; a card (`list`) shows the whole list a page at a time.

---

## Directory Structure
```
plugins/ha_task_list/
├── tessera-plugin.yaml        # Manifest: plugin ID, tiles, settings, permissions
├── plugin.yaml                # ESPHome component inclusion and configuration
├── components/
│   └── ha_task_list/
│       ├── __init__.py        # ESPHome Python schema and code generator
│       ├── ha_task_list.h     # Header file for plugin and Tile classes
│       └── ha_task_list.cpp   # C++ implementation: UI layout, drawing, events
├── translations/
│   └── en.json                # App editor & screen UI translations
├── README.md                  # Documentation & setup instructions
└── CHANGELOG.md               # Version history
```

---

## Technical Architecture & APIs

- **Manifest (`tessera-plugin.yaml`):**
  - The tile belongs to a `todo` entity (`domains: [todo]`); one option, `show_completed`.
  - `permissions.ha_commands` names `call_service:todo.get_items` (the items, asked through the app with `tessera::send`);
    `home_assistant_actions` names `todo.update_item` (tick and untick).
  - The app's answer is at most ~3 KB with texts cut to 48 bytes: about 25 to 30 items reach the screen.
- **C++ Tile Class (`tessera::Tile`):**
  - `create(const tessera::TileContext &c)`: Instantiates labels, checkboxes, and touch targets.
  - `on_state(JsonObjectConst data)`: Receives entity updates and task item JSON payloads from Home Assistant.
  - `on_tick(uint32_t epoch)`: Handles UI refresh timers and animations.
  - `on_theme()`: Switches styling between dark and light themes dynamically.
- **Home Assistant Communication:**
  - Uses `tessera::action()` for executing task state updates and item additions.

---

## Essential Commands

- **Scaffold Plugin:** `python3 tools/new_plugin.py ha_task_list`
- **Validate Plugin:** `python3 tools/check.py plugins/ha_task_list`
- **Preview UI:** `python3 tools/preview.py plugins/ha_task_list --open --watch`
- **Generate Store Image:** `python3 tools/store_image.py plugins/ha_task_list`

---

## Coding Standards & Rules

1. **Touch UI Targets:** Ensure tap targets (checkboxes, buttons) have a minimum physical size of `ui::mm(10)` or at least 44x44 pixels.
2. **Dynamic Formatting:** Use standard Tessera fonts (`tessera::Font`) and handle text truncation to prevent overflow beyond tile boundaries.
3. **Optimistic Rendering:** Update local checkbox state on touch immediately before dispatching `tessera::action()` to Home Assistant; revert state on failure.
4. **Localization:** Keep all user-facing string literals in `translations/en.json` divided into `app` (editor UI) and `screen` (display UI) namespaces.
5. **Validation Compliance:** Ensure `python3 tools/check.py` returns `ok` without warnings prior to commits.
