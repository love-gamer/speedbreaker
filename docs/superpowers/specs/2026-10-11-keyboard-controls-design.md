# Keyboard controls page: design

Date: 2026-10-11. Status: approved in conversation, awaiting review of this
document.

## Goal

A **Controls** page in the F1 settings menu where every Xbox 360 control the
keyboard can drive has a row, and the player presses a key to reassign it.
Keyboard only: the controller keeps its fixed mapping. Bindings persist in
settings.toml and survive a relaunch.

Success: rebind gas (the right trigger) from W to Up in the menu, close it,
the car accelerates on Up, and it still does after a relaunch.

## Scope

In: 24 controls, one key each, a key may serve several controls, the file,
the page, a "press a key" capture, Reset page, docs.

Out: controller remapping, mouse control, key chords (Shift+X), a second key
per control, clearing a control to "no key", in-game button prompts, a new
phase in `scripts/verify_settings.py` (a natural follow-up).

## Decisions taken in brainstorming

- One key per control. A key may be bound to several controls (the defaults
  share four arrow keys between the D-pad and the left stick).
- A key binding is a setting: a new option type in the existing settings
  table, so save, load, defaults, Reset page, dirty tracking and the change
  log line come from the existing machinery.
- Rows are named by the Xbox 360 control (Right Trigger, Left Stick Left,
  D-pad Up). The game has its own control presets, so the description
  mentions the usual meaning ("gas in the game's default layout") rather than
  the label promising it.
- Left and Right arrows stop steering; A and D steer. Every other key that
  works today keeps working. Right stick left and right and the left stick
  click gain keys they never had.
- Esc (or B) cancels a capture, so Esc itself comes back to a control only
  through Reset page or a hand edit of the file.

## Section 1: `Type::Key` in the settings table

`runtime/user/settings.h` gains `Type::Key`. The value is the SDL scancode
(the physical key position) stored as a double, which is what
`SDL_GetKeyboardState` is indexed by and what the existing keyboard code
already uses, so a binding survives a layout switch.

Each type switch in `runtime/user/settings.cpp` gains a Key case:

| Function | Key behaviour |
|---|---|
| `RawFromText` | `SDL_GetScancodeFromName(text)`; `SDL_SCANCODE_UNKNOWN` means "not a key name". |
| `RawFromToml` | Strings only, through `RawFromText`. A number or boolean is "not a key name" and ignored with the existing log line, as a bad enum token is today. |
| `Normalize` | Accepts a whole number in [1, `SDL_SCANCODE_COUNT`) whose `SDL_GetScancodeName` is non-empty, returned as that integer. Anything else is invalid (so `Set` returns `Invalid`). |
| `RangeText` | `a key name as SDL spells it ("W", "Left Shift", "Return", "Keypad 1")`. |
| `TomlValue` | The quoted scancode name: `right_trigger = "W"`. |
| `FormatValue` | The scancode name; "?" for NaN or out of range. Used by the log line and the bug report's settings list. |
| `StepValue` | Returns the value unchanged (Left and Right do nothing on a key row). |
| `TableIsConsistent` | A Key option's default is in [1, `SDL_SCANCODE_COUNT`) and has no min/max/step/entries. |

`Option.env` becomes nullable. `nullptr` means no environment variable:
`TableIsConsistent` allows it only for Key options, `ReadEnvLocked` returns
at once, `EnvOverride` returns nullptr (as it does when the variable is not
set), and `Serialize` writes the per-option comment without the parenthesis.
The first line of the file's header comment, which says every option has an
environment variable, is reworded to "most options".

settings.cpp includes `SDL3/SDL_keyboard.h` and `SDL3/SDL_scancode.h`. The
name functions are table lookups, safe from any thread, which matters because
`FormatValue` runs on the bug report's thread.

A new `Category::Controls` sits between `Input` and `Advanced`;
`CategoryName` returns "Controls". The tab bar reads Display, Graphics,
Audio, Input, Controls, Advanced.

The 24 ids are appended after `Vibration` and before `AsyncShaders`, in
page order, in a `[keys]` table (the consistency check needs a table's keys
contiguous). All are `Apply::Live`, not hidden.

## Section 2: the controls and their defaults

| Id | Key in `[keys]` | Label | Default | Description (menu help, file comment) |
|---|---|---|---|---|
| KeyRightTrigger | right_trigger | Right Trigger | W | Gas in the game's default layout. |
| KeyLeftTrigger | left_trigger | Left Trigger | S | Brake and reverse in the game's default layout. |
| KeyLeftStickLeft | left_stick_left | Left Stick Left | A | Steer left. |
| KeyLeftStickRight | left_stick_right | Left Stick Right | D | Steer right. |
| KeyLeftStickUp | left_stick_up | Left Stick Up | Up | The left stick pushed up (menus; the camera in some views). |
| KeyLeftStickDown | left_stick_down | Left Stick Down | Down | The left stick pulled down. |
| KeyA | a | A | Space | The A button. |
| KeyB | b | B | Backspace | The B button. |
| KeyX | x | X | Left Shift | The X button. |
| KeyY | y | Y | Left Ctrl | The Y button. |
| KeyLB | lb | LB | Q | The left shoulder button. |
| KeyRB | rb | RB | E | The right shoulder button. |
| KeyDpadUp | dpad_up | D-pad Up | Up | The D-pad, up. |
| KeyDpadDown | dpad_down | D-pad Down | Down | The D-pad, down. |
| KeyDpadLeft | dpad_left | D-pad Left | Left | The D-pad, left. |
| KeyDpadRight | dpad_right | D-pad Right | Right | The D-pad, right. |
| KeyStart | start | Start | Return | Start: pause. |
| KeyBack | back | Back | Escape | Back. |
| KeyRightStickUp | right_stick_up | Right Stick Up | O | The right stick pushed up. |
| KeyRightStickDown | right_stick_down | Right Stick Down | L | The right stick pulled down. |
| KeyRightStickLeft | right_stick_left | Right Stick Left | K | The right stick pushed left. |
| KeyRightStickRight | right_stick_right | Right Stick Right | ; | The right stick pushed right. |
| KeyLeftStickClick | left_stick_click | Left Stick Click | Z | Pressing the left stick in. |
| KeyRightStickClick | right_stick_click | Right Stick Click | C | Pressing the right stick in. |

The defaults are written as SDL scancode constants in the table
(`SDL_SCANCODE_W`, `SDL_SCANCODE_SEMICOLON`, ...). The description text may
be polished during implementation but keeps the Xbox-control framing.

An old settings.toml with no `[keys]` table loads the defaults. A name SDL
doesn't know is skipped with the existing log line and the default stays.
Every save writes all 24, like every other option.

## Section 3: how the game reads the keys

### `runtime/hid/key_map.h` (new, header-only, no SDL calls)

Namespace `hid::keymap`. Holds the list of the 24 controls in page order and
the resolution rules, so a unit test can build it alone:

```cpp
enum class Control : uint8_t { RightTrigger, LeftTrigger, LeftStickLeft, ..., RightStickClick, Count };
constexpr size_t kControls = size_t(Control::Count);

enum class Role : uint8_t { Button, LeftTrigger, RightTrigger, AxisNeg, AxisPos };
enum class Axis : uint8_t { LX, LY, RX, RY };

struct Entry
{
    Control control;
    settings::Id id;     // the option holding its scancode
    Role role;
    uint16_t button;     // Role::Button: the XInput bit (literal; input.cpp static_asserts them against XAMINPUT_*)
    Axis axis;           // AxisNeg / AxisPos
};
constexpr Entry kEntries[kControls] = { ... };   // one per Control, in Control order

struct Resolved
{
    uint16_t buttons = 0;
    bool leftTrigger = false, rightTrigger = false;   // the key is down: full trigger
    bool axisSet[4] = {};                             // either key of the axis is down
    float axis[4] = {};                               // +1, 0 or -1 when set
};
// `pressed[i]` is whether the key bound to Control(i) is down.
constexpr Resolved Resolve(std::span<const bool, kControls> pressed);
```

Rules, as agreed: buttons OR their bit; a trigger key means that trigger at
full; an axis is `pressed[pos] - pressed[neg]`, and if either key is down the
axis is "set" (the keyboard value replaces the controller's for that axis,
so both keys held centres the stick). Up is positive for LY and RY, as
XInput has it.

`settings.h` is included for `Id` only; the header calls nothing from it.

### `runtime/hid/input.cpp`

In `Update()`, the fixed scancode table and the stick, trigger and O/L
special cases are replaced by: for each `Entry`, `pressed[i]` is
`k[scancode]` where the scancode is `settings::GetInt(entry.id)` and is in
[1, `SDL_SCANCODE_COUNT`); then `Resolve`, then: `p.buttons |= r.buttons`;
`rt = 1` / `lt = 1` for the triggers; each set axis replaces `lx`, `ly`,
`rx`, `ry`. The keyboard pass stays where it is (after the gamepad pass,
before the triggers' deadzone and the menu's blocking and latching), so
nothing else in the input path changes. Keyboard Back still sets the BACK bit
directly, bypassing the controller's Back + Start chord timer, as today.

Static asserts tie each `Entry::button` literal to its `XAMINPUT_GAMEPAD_*`
constant. The header comment in `input.h` describes the defaults as "Settings
> Controls" and the file's `[keys]` table instead of the fixed list.

The uncommitted O/L edit in input.cpp is subsumed: O and L are the defaults
for the right stick's up and down.

## Section 4: the Controls page

The page loop in `runtime/ui/settings_menu.cpp` draws one row per option of
the selected category, so the 24 rows, the Reset page button and its
confirm dialog ("Reset every Controls setting to its default?") need no new
page code. `OptionRow` dispatches a `Type::Key` option to a new `KeyRow`.

**KeyRow:** the same highlight, label inset and markers as other rows (ENV
and RESTART never show for a key). The value column shows the key's name
centred, with no arrows and no range bar. The name is the current layout's,
looked up on the main thread as `SDL_GetKeyName(SDL_GetKeyFromScancode(sc,
SDL_KMOD_NONE, false))`, falling back to `SDL_GetScancodeName` when that is
empty: a French Mac shows "Z" for the key the file stores as "W". The file,
the log and the bug report keep the scancode name. `ValueWidth` for a Key
option measures the name of its stored value and "Press a key".

**Starting a capture:** A, Enter, or a click (on release, like Save Bug
Report) on the row. A menu-level `s_capturing` holds the option's id
(`Id::Count`: none). The row's value shows "Press a key" in the accent
colour, and the help line reads "Press the key for <label>. Esc cancels."
Only one row captures at a time. If focus leaves the capturing row (the
D-pad moved), the capture ends quietly.

**Taking the key:** a new hook declared in `ui.h` beside the other screen
functions:

```cpp
// Main thread, from ProcessEvent on a key-down while the menu is open, after
// the F1 toggle: true when a Controls row was waiting for a key and took (or
// cancelled on Esc) this one, so ImGui must not see it.
bool SettingsMenuTakesKey(const SDL_KeyboardEvent& key);
```

`ui::ProcessEvent`'s `SDL_EVENT_KEY_DOWN` case calls it after the F1 / Cmd+,
check and before forwarding to ImGui, and returns when it is true. In the
hook: no capture armed, or a key repeat: false. Escape: cancel, true. Any
other scancode: `settings::Set(id, scancode)`, the existing `LogChange` line
("[settings] menu: keys.right_trigger W -> Up (applied now)"), capture ends,
true. The key-up still reaches ImGui, which ignores an up without a down.
Because the down never reaches ImGui, Enter, Space, the arrows and Q or E
can be bound without the menu also acting on them. The first key down wins:
Shift alone binds Shift; the Shift of a Shift+X binds Shift. Gamepad B while
capturing cancels the capture instead of closing the menu: the menu's
"B closes" check at the bottom of `DrawSettings` is skipped while
`s_capturing` is set, the same way it is skipped under a dialog. (The Esc
key never reaches that check: the hook consumed its key-down.)

**Shared keys:** allowed. When the focused row's key also drives other
controls, the help line's second line says "Also: D-pad Up, Left Stick Up."
in the dim style (listing the other controls' labels, in page order). No
marker on the row: the defaults share four arrows.

**Legend** on this page: "D-pad: choose   A: change key   LB RB: page   B:
close". Other pages keep theirs.

**Scrolling:** 24 rows outrun the window; the rows child already scrolls,
and ImGui scrolls the focused row into view as the D-pad moves.

**ResetSettingsMenu** (on open) clears `s_capturing`.

## Section 5: errors and edge cases

| Case | Behaviour |
|---|---|
| A key SDL can't name (Set returns Invalid) | The capture ends, the row keeps its key, the change log line says "invalid". Nothing shown to the player. |
| Menu closed mid-capture (F1, Back + Start, three-finger tap) | The close saves as usual; the capture is cleared on the next open. |
| Bad name in the file | Skipped with the existing log line; the default stays. |
| No `[keys]` table (an older file) | Defaults. |
| Save fails | The existing toast. |
| The same key on Back and A | Both fire; the help line shows it. The player's choice. |
| iOS without a keyboard | The page shows; a capture waits until B cancels it. Not hidden: an iPad with a keyboard uses it. |

## Section 6: testing

- **Unit test, new: `tests/key_map_test.cpp`** (header-only unit, added to
  `tests/CMakeLists.txt` with `sb_test`). Covers: every `Control` appears
  exactly once in `kEntries` and in Control order, with a distinct settings
  id; a button key sets its bit and nothing else; a trigger key sets that
  trigger; an axis is +1, -1 or 0 from its two keys and is "set" when either
  is down; nothing pressed sets nothing; the button literals match the
  `XAMINPUT_*` values (the test includes `xbox.h` the way the write-watch
  test includes XenonUtils, or repeats the four face-button literals).
- **Scripted menu run** with `NFSMW_UI_KEYS`: open the menu, turn to Controls
  (E, four times from Display), Enter on the first row, Up, close. Pass: the
  log has `menu: keys.right_trigger W -> Up (applied now)` and settings.toml
  holds `right_trigger = "Up"`. A second launch: the log's "N values from the
  file" count includes it and the row shows Up.
- **Hands-on:** rebind the right trigger to Up, close, drive. Rebind Back,
  confirm Esc no longer reaches the game. Reset page, confirm the defaults
  table comes back. A French or German layout if one is at hand: the row
  shows the keycap, the file the scancode name.

## Section 7: files and docs

| File | Change |
|---|---|
| `runtime/user/settings.h` | `Type::Key`, `Category::Controls`, 24 ids, nullable `env` comment. |
| `runtime/user/settings.cpp` | The 24 table rows, Key cases, nullable env, SDL includes, header comment wording. |
| `runtime/hid/key_map.h` | New: controls, entries, `Resolve`. |
| `runtime/hid/input.h`, `input.cpp` | Comment; one pass over the bindings; static asserts. |
| `runtime/ui/ui.h` | `SettingsMenuTakesKey` declaration. |
| `runtime/ui/ui.cpp` | One call in the key-down case. |
| `runtime/ui/settings_menu.cpp` | `KeyRow`, capture state and hook, help and legend text, `ValueWidth`. |
| `tests/key_map_test.cpp`, `tests/CMakeLists.txt` | The unit test. |
| `README.md` | Controls table: the new keys and A/D-only steering; a sentence pointing at Settings > Controls; the "There is no button remapping" line goes; the Settings section mentions the Controls page. |
| `CHANGELOG.md` | A new unreleased heading above v0.1.1 with the feature and the arrow-steering change. |
| `docs/TESTING.md` | One line: the Controls page, and that Reset page restores the keys. |
