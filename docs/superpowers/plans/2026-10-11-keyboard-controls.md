# Keyboard Controls Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Controls page in the F1 settings menu where each of the 24 keyboard-driven Xbox 360 controls has a row, and the player presses a key to reassign it; the keys persist in settings.toml.

**Architecture:** A key binding is a setting: a new `Type::Key` option type (value = SDL scancode) with 24 ids in a `[keys]` table and a `Controls` category, so save, load, defaults, Reset page, dirty tracking and the change log line come from the existing settings machinery. A header-only `hid/key_map.h` lists the controls and turns held keys into buttons, triggers and stick axes; `hid::Update()` reads the 24 bindings per frame through it. The menu adds a key row with a "press a key" capture that consumes the SDL key-down before ImGui sees it.

**Tech Stack:** C++20, SDL3 (scancodes and key names), Dear ImGui (the menu), toml++ (settings.toml), CMake + Ninja, the repo's own `CHECK`-macro unit tests under `tests/`.

**Spec:** `docs/superpowers/specs/2026-10-11-keyboard-controls-design.md`

## Global Constraints

- C++20 (`CMAKE_CXX_STANDARD 20`); clang (Homebrew LLVM on the Mac). No new dependencies.
- Every new source file starts with `// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).` Comments are prose in the style of the files around them (what and why, not restating the code).
- A key binding's stored value is the **SDL scancode** (physical position), 1 to 511, and the file holds its **SDL scancode name** (`SDL_GetScancodeName`), quoted: `right_trigger = "W"`.
- One key per control. A key may be bound to several controls. No "no key" state: `Normalize` rejects 0.
- Key options have **no environment variable** (`env = nullptr`); only Key options may.
- The 24 ids follow `Vibration` and precede `AsyncShaders` in `settings::Id`, in the page order of the spec's Section 2 table, all in table `"keys"`, category `Controls`, `Apply::Live`, not hidden. `TableIsConsistent()` needs a table's rows contiguous.
- `Category` order: `Display, Graphics, Audio, Input, Controls, Advanced, Count`.
- Key names in the file, the log line and the bug report are scancode names; only the menu row shows the current layout's label.
- Build: `ninja -C build/main SpeedBreaker` (the binary is `build/main/runtime/SpeedBreaker`). Tests: configure once with `cmake -S . -B build/main -DSB_TESTS=ON` (keeps the rest of the cache), then `ninja -C build/main <test>` and run `build/main/tests/<test>`.
- The runtime globs `runtime/**/*.cpp`, so a new .cpp needs no CMake change; a new header-only unit needs none either.
- Commit after each task with a message in the repo's style (a one-line summary, then prose), ending with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.

## Review Focus

Inputs the spec implies but no spec section tests; each gets a test in the task that owns the code:

1. **A key name in the file in another case (`"w"`, `"left shift"`)**: SDL's name lookup is case-insensitive, so it must load as that key, not be "ignored" (Task 2's test).
2. **A modifier key as a binding** (the defaults have Left Shift and Left Ctrl): a capture must take the modifier's own key-down, not wait for a "real" key (Task 5's scripted run binds Left Shift).
3. **One key on a button and an axis at once** (the default Up is D-pad Up and Left Stick Up): both must fire (Task 3's test).
4. **A scancode in range but without a name** (1, 2, 3 and the gaps up to 511): `Set` must refuse it, else the file would get `""` and the key would vanish on the next load (Task 2's test).
5. **A page turn or a focus move while a row is waiting for a key**: the capture must end, or the next key anywhere would rebind a row the player can't see (Task 5's code clears it when the capturing row's category isn't the page shown or the row isn't focused; the hands-on task checks it with LB/RB).

---

### Task 1: The Key option type, the Controls category and the 24 options

**Files:**
- Modify: `runtime/user/settings.h:38-70` (the `Id` enum, `Type`, `Category`), `:98` (`Option::env` comment)
- Modify: `runtime/user/settings.cpp:1-45` (includes), `:172-186` (the table rows after Vibration), `:199-226` (`TableIsConsistent`), `:555-574` (`ReadEnvLocked`), `:712-741` (`Serialize`), `:914-925` (`CategoryName`)
- Create: `tests/settings_keys_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `settings::Type::Key`; `settings::Category::Controls`; `settings::Id::KeyRightTrigger` ... `settings::Id::KeyRightStickClick` (24, in the spec's order); `Option::env` may be `nullptr`.
- Later tasks rely on: `settings::GetInt(Id::Key*)` returning the scancode (Task 4), `settings::Options()` iterating the 24 as `Type::Key` in `Category::Controls` (Task 5).

- [ ] **Step 1: Write the failing test**

Create `tests/settings_keys_test.cpp`:

```cpp
// Tests for the Key options (Settings > Controls, user/settings.h): the
// table, the [keys] names in settings.toml, and what the menu's Set accepts.
// Links user/settings.cpp and SDL3 (the scancode names). From the repo root,
// with the deps in build/deps:
//   clang++ -std=c++20 -O2 -Iruntime -Ibuild/deps/include -Itools/XenonRecomp/thirdparty/tomlplusplus/include \
//     tests/settings_keys_test.cpp runtime/user/settings.cpp -Lbuild/deps/lib -lSDL3 -Wl,-rpath,build/deps/lib \
//     -o build/settings_keys_test && build/settings_keys_test
#include <user/settings.h>

#include <SDL3/SDL_keyboard.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main()
{
    using namespace settings;

    // The table: 24 Key options, every one in [keys], on the Controls page,
    // live, shown, with no environment variable and a named default.
    int keys = 0;
    std::set<std::string> names;
    for (const Option& o : Options())
    {
        if (o.type != Type::Key)
        {
            CHECK(std::string_view(o.table) != "keys", "%s.%s: only Key options live in [keys]", o.table, o.key);
            CHECK(o.env != nullptr, "%s.%s: every other option has an environment variable", o.table, o.key);
            continue;
        }
        keys++;
        CHECK(std::string_view(o.table) == "keys", "%s.%s: a Key option lives in [keys]", o.table, o.key);
        CHECK(o.category == Category::Controls, "%s: on the Controls page", o.key);
        CHECK(o.apply == Apply::Live && !o.hidden, "%s: live and shown", o.key);
        CHECK(o.env == nullptr, "%s: no environment variable", o.key);
        CHECK(names.insert(o.key).second, "%s: one row per key", o.key);
        CHECK(o.defaultValue >= 1 && o.defaultValue < SDL_SCANCODE_COUNT, "%s: the default is a scancode", o.key);
        CHECK(SDL_GetScancodeName(SDL_Scancode(int(o.defaultValue)))[0] != '\0', "%s: the default has a name", o.key);
        CHECK(o.min == 0 && o.max == 0 && o.step == 0 && o.entries.empty(), "%s: no range", o.key);
    }
    CHECK(keys == 24, "%d Key options", keys);
    CHECK(std::string_view(CategoryName(Category::Controls)) == "Controls", "the category's name");

    // The defaults of the README's table, in page order.
    struct Default { Id id; const char* key; SDL_Scancode scancode; };
    const Default defaults[] = {
        { Id::KeyRightTrigger, "right_trigger", SDL_SCANCODE_W },
        { Id::KeyLeftTrigger, "left_trigger", SDL_SCANCODE_S },
        { Id::KeyLeftStickLeft, "left_stick_left", SDL_SCANCODE_A },
        { Id::KeyLeftStickRight, "left_stick_right", SDL_SCANCODE_D },
        { Id::KeyLeftStickUp, "left_stick_up", SDL_SCANCODE_UP },
        { Id::KeyLeftStickDown, "left_stick_down", SDL_SCANCODE_DOWN },
        { Id::KeyA, "a", SDL_SCANCODE_SPACE },
        { Id::KeyB, "b", SDL_SCANCODE_BACKSPACE },
        { Id::KeyX, "x", SDL_SCANCODE_LSHIFT },
        { Id::KeyY, "y", SDL_SCANCODE_LCTRL },
        { Id::KeyLB, "lb", SDL_SCANCODE_Q },
        { Id::KeyRB, "rb", SDL_SCANCODE_E },
        { Id::KeyDpadUp, "dpad_up", SDL_SCANCODE_UP },
        { Id::KeyDpadDown, "dpad_down", SDL_SCANCODE_DOWN },
        { Id::KeyDpadLeft, "dpad_left", SDL_SCANCODE_LEFT },
        { Id::KeyDpadRight, "dpad_right", SDL_SCANCODE_RIGHT },
        { Id::KeyStart, "start", SDL_SCANCODE_RETURN },
        { Id::KeyBack, "back", SDL_SCANCODE_ESCAPE },
        { Id::KeyRightStickUp, "right_stick_up", SDL_SCANCODE_O },
        { Id::KeyRightStickDown, "right_stick_down", SDL_SCANCODE_L },
        { Id::KeyRightStickLeft, "right_stick_left", SDL_SCANCODE_K },
        { Id::KeyRightStickRight, "right_stick_right", SDL_SCANCODE_SEMICOLON },
        { Id::KeyLeftStickClick, "left_stick_click", SDL_SCANCODE_Z },
        { Id::KeyRightStickClick, "right_stick_click", SDL_SCANCODE_C },
    };
    size_t previous = size_t(Id::Vibration);
    for (const Default& d : defaults)
    {
        const Option& o = Info(d.id);
        CHECK(std::string_view(o.key) == d.key, "%s is %s", o.key, d.key);
        CHECK(o.defaultValue == double(d.scancode), "%s defaults to %s, not %s", o.key, SDL_GetScancodeName(d.scancode),
            SDL_GetScancodeName(SDL_Scancode(int(o.defaultValue))));
        CHECK(size_t(d.id) == previous + 1, "%s follows the row before it", o.key);
        previous = size_t(d.id);
    }
    CHECK(size_t(Id::AsyncShaders) == previous + 1, "Advanced follows the keys");
    CHECK(EnvOverride(Id::KeyA) == nullptr, "nothing overrides a key");

    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
```

Add to `tests/CMakeLists.txt`, after the `version_check_test` line:

```cmake
# The Key options (Settings > Controls): the table, the [keys] names in the
# file, and what Set accepts (user/settings.cpp, which needs SDL3 for the
# scancode names and the toml++ XenonRecomp vendors).
find_package(SDL3 REQUIRED CONFIG)
sb_test(settings_keys_test SOURCES ${RT}/user/settings.cpp)
target_link_libraries(settings_keys_test PRIVATE SDL3::SDL3)
target_include_directories(settings_keys_test SYSTEM PRIVATE ${ROOT}/tools/XenonRecomp/thirdparty/tomlplusplus/include)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake -S . -B build/main -DSB_TESTS=ON && ninja -C build/main settings_keys_test
```
Expected: a compile error, `no member named 'Key' in 'settings::Type'` (and `Controls`, `KeyRightTrigger`).

- [ ] **Step 3: Add the type, the category and the ids to settings.h**

In `runtime/user/settings.h`, in the `Id` enum, after `Vibration,` and before `// Advanced`:

```cpp
        // Controls (the [keys] table): the keyboard key that drives each
        // Xbox 360 control, as an SDL scancode (hid/key_map.h). Page order.
        KeyRightTrigger,
        KeyLeftTrigger,
        KeyLeftStickLeft,
        KeyLeftStickRight,
        KeyLeftStickUp,
        KeyLeftStickDown,
        KeyA,
        KeyB,
        KeyX,
        KeyY,
        KeyLB,
        KeyRB,
        KeyDpadUp,
        KeyDpadDown,
        KeyDpadLeft,
        KeyDpadRight,
        KeyStart,
        KeyBack,
        KeyRightStickUp,
        KeyRightStickDown,
        KeyRightStickLeft,
        KeyRightStickRight,
        KeyLeftStickClick,
        KeyRightStickClick,
```

Replace the `Type` and `Category` lines:

```cpp
    // Key: an SDL scancode (the key's position, 1 to 511), saved as SDL's
    // name of it ("W", "Left Shift"); the menu takes it from a key press.
    enum class Type : uint8_t { Bool, Int, Float, Enum, Key };
    enum class Category : uint8_t { Display, Graphics, Audio, Input, Controls, Advanced, Count };
```

Change the `env` member's comment in `struct Option`:

```cpp
        const char* env;          // the environment variable that overrides it; nullptr: none (Key options)
```

In the file's header comment, change the line `//   - An environment variable (NFSMW_AA=0, ...) wins over the file for that` paragraph to start: `//   - An environment variable (NFSMW_AA=0, ...) wins over the file for that run and is never written back; EnvOverride() tells the menu. Key options have none.` (keep the wrapping at the file's width).

- [ ] **Step 4: Add the rows, the names and the nullable env to settings.cpp**

Includes, before the toml++ pragma block in `runtime/user/settings.cpp`:

```cpp
#include <SDL3/SDL_keyboard.h>  // the Key options: scancodes and their names
```

After the `kAnisotropyEntries` array and before `// Defaults are what the runtime did before settings existed`, add:

```cpp
        // A Controls row: the key (as an SDL scancode) that drives one Xbox
        // 360 control. No environment variable: 24 of them would clutter
        // the file's comments for nobody.
        constexpr Option KeyOption(Id id, const char* key, const char* label, const char* description, SDL_Scancode scancode)
        {
            return { .id = id, .category = Category::Controls, .type = Type::Key, .apply = Apply::Live,
                     .table = "keys", .key = key, .label = label, .description = description, .env = nullptr,
                     .defaultValue = double(scancode) };
        }
```

In `kOptions`, after the `Vibration` row and before `// Advanced`:

```cpp
            // Controls (Settings > Controls; hid/key_map.h reads them)
            KeyOption(Id::KeyRightTrigger, "right_trigger", "Right Trigger", "Gas in the game's default layout.", SDL_SCANCODE_W),
            KeyOption(Id::KeyLeftTrigger, "left_trigger", "Left Trigger", "Brake and reverse in the game's default layout.", SDL_SCANCODE_S),
            KeyOption(Id::KeyLeftStickLeft, "left_stick_left", "Left Stick Left", "Steer left.", SDL_SCANCODE_A),
            KeyOption(Id::KeyLeftStickRight, "left_stick_right", "Left Stick Right", "Steer right.", SDL_SCANCODE_D),
            KeyOption(Id::KeyLeftStickUp, "left_stick_up", "Left Stick Up", "The left stick pushed up (menus; the camera in some views).", SDL_SCANCODE_UP),
            KeyOption(Id::KeyLeftStickDown, "left_stick_down", "Left Stick Down", "The left stick pulled down.", SDL_SCANCODE_DOWN),
            KeyOption(Id::KeyA, "a", "A", "The A button.", SDL_SCANCODE_SPACE),
            KeyOption(Id::KeyB, "b", "B", "The B button.", SDL_SCANCODE_BACKSPACE),
            KeyOption(Id::KeyX, "x", "X", "The X button.", SDL_SCANCODE_LSHIFT),
            KeyOption(Id::KeyY, "y", "Y", "The Y button.", SDL_SCANCODE_LCTRL),
            KeyOption(Id::KeyLB, "lb", "LB", "The left shoulder button.", SDL_SCANCODE_Q),
            KeyOption(Id::KeyRB, "rb", "RB", "The right shoulder button.", SDL_SCANCODE_E),
            KeyOption(Id::KeyDpadUp, "dpad_up", "D-pad Up", "The D-pad, up.", SDL_SCANCODE_UP),
            KeyOption(Id::KeyDpadDown, "dpad_down", "D-pad Down", "The D-pad, down.", SDL_SCANCODE_DOWN),
            KeyOption(Id::KeyDpadLeft, "dpad_left", "D-pad Left", "The D-pad, left.", SDL_SCANCODE_LEFT),
            KeyOption(Id::KeyDpadRight, "dpad_right", "D-pad Right", "The D-pad, right.", SDL_SCANCODE_RIGHT),
            KeyOption(Id::KeyStart, "start", "Start", "Start: pause.", SDL_SCANCODE_RETURN),
            KeyOption(Id::KeyBack, "back", "Back", "Back.", SDL_SCANCODE_ESCAPE),
            KeyOption(Id::KeyRightStickUp, "right_stick_up", "Right Stick Up", "The right stick pushed up.", SDL_SCANCODE_O),
            KeyOption(Id::KeyRightStickDown, "right_stick_down", "Right Stick Down", "The right stick pulled down.", SDL_SCANCODE_L),
            KeyOption(Id::KeyRightStickLeft, "right_stick_left", "Right Stick Left", "The right stick pushed left.", SDL_SCANCODE_K),
            KeyOption(Id::KeyRightStickRight, "right_stick_right", "Right Stick Right", "The right stick pushed right.", SDL_SCANCODE_SEMICOLON),
            KeyOption(Id::KeyLeftStickClick, "left_stick_click", "Left Stick Click", "Pressing the left stick in.", SDL_SCANCODE_Z),
            KeyOption(Id::KeyRightStickClick, "right_stick_click", "Right Stick Click", "Pressing the right stick in.", SDL_SCANCODE_C),
```

In `TableIsConsistent()`, after the `if (o.type == Type::Enum) { ... }` block, add a Key check and relax the env check:

```cpp
                if (o.type == Type::Key && !(o.defaultValue >= 1 && o.defaultValue < SDL_SCANCODE_COUNT && o.min == 0 && o.max == 0 &&
                                             o.step == 0 && o.entries.empty() && !o.hidden))
                    return false;
                if ((o.env == nullptr) != (o.type == Type::Key) || o.label == nullptr || o.description == nullptr)
                    return false;
```
(replacing the existing `if (o.env == nullptr || o.label == nullptr || o.description == nullptr)` line).

In `ReadEnvLocked`, after `s_envSet[i].store(false, std::memory_order_relaxed);`:

```cpp
            if (!o.env)
                return;  // a Key option: the file alone
```

In `Serialize`, change the header's third and fourth lines and the per-option comment:

```cpp
            std::string out =
                "# SpeedBreaker settings, written by the game's settings menu. Hand edits\n"
                "# are fine while the game is closed; unknown keys are dropped on the next\n"
                "# save. The environment variable after an option overrides it for one run\n"
                "# without changing this file.\n";
```
and
```cpp
                std::string range = RangeText(o);
                range[0] = char(std::toupper(uint8_t(range[0])));
                std::string override = o.env ? std::format(" ({}{})", o.env, o.apply == Apply::Restart ? "; applies at the next launch" : "") : "";
                out += std::format("# {}: {}\n# {}.{}\n", o.label, o.description, range, override);
```

In `CategoryName`, add `case Category::Controls: return "Controls";` after the `Input` case.

- [ ] **Step 5: Build and run the test**

Run:
```bash
ninja -C build/main settings_keys_test && build/main/tests/settings_keys_test
```
Expected: `passed (0 failures)`. (The `[settings] ... before settings::Load()` lines on stderr are normal: the test never loads a file in this task.) Also `ninja -C build/main SpeedBreaker` builds: the new type falls through the existing switches' trailing `return {}` / `return ""` for now, with `-Wswitch` warnings (`enumeration value 'Key' not handled`) that Task 2 removes.

- [ ] **Step 6: Commit**

```bash
git add runtime/user/settings.h runtime/user/settings.cpp tests/settings_keys_test.cpp tests/CMakeLists.txt
git commit -m "Settings: a Key option type and the 24 Controls rows

A key binding is a setting: Type::Key holds an SDL scancode, the 24
controls a keyboard can drive get options in a [keys] table and a
Controls category, with no environment variable. The file's names and
the menu come next.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Reading, writing, formatting and validating a Key

**Files:**
- Modify: `runtime/user/settings.cpp:403-420` (`Normalize`), `:424-447` (`RangeText`), `:452-467` (`RawFromText`), `:472-498` (`RawFromToml`), `:687-710` (`TomlValue`), `:1029-1061` (`StepValue`), `:1063-1093` (`FormatValue`)
- Modify: `tests/settings_keys_test.cpp`

**Interfaces:**
- Consumes: Task 1's `Type::Key`, ids and `KeyOption` rows.
- Produces: `settings::Load` accepts `right_trigger = "W"` (case-insensitive) and skips anything else; `settings::Save` writes `right_trigger = "W"`; `settings::Set(Id::Key*, scancode)` returns `Applied` for a named scancode and `Invalid` otherwise; `settings::FormatValue(Id::Key*, v)` is the scancode name or `"?"`; `settings::StepValue` leaves a Key unchanged.

- [ ] **Step 1: Extend the test**

In `tests/settings_keys_test.cpp`, before the final `printf`, add:

```cpp
    // The file: names in, in any case; anything else keeps the default.
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "speedbreaker-settings-keys-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::filesystem::path file = dir / "settings.toml";
    {
        std::ofstream f(file);
        f << "[keys]\n"
             "right_trigger = \"Up\"\n"
             "left_trigger = \"Keypad 1\"\n"
             "right_stick_left = \"k\"\n"
             "right_stick_up = \"left shift\"\n"
             "a = \"Not A Key\"\n"
             "b = 42\n"
             "x = true\n"
             "y = \"\"\n";
    }
    CHECK(Load(file), "loads: %s", LastError().c_str());
    CHECK(GetInt(Id::KeyRightTrigger) == SDL_SCANCODE_UP, "\"Up\": %d", GetInt(Id::KeyRightTrigger));
    CHECK(GetInt(Id::KeyLeftTrigger) == SDL_SCANCODE_KP_1, "a two-word name: %d", GetInt(Id::KeyLeftTrigger));
    CHECK(GetInt(Id::KeyRightStickLeft) == SDL_SCANCODE_K, "\"k\" is K: %d", GetInt(Id::KeyRightStickLeft));
    CHECK(GetInt(Id::KeyRightStickUp) == SDL_SCANCODE_LSHIFT, "\"left shift\" is Left Shift: %d", GetInt(Id::KeyRightStickUp));
    CHECK(GetInt(Id::KeyA) == SDL_SCANCODE_SPACE, "an unknown name keeps the default");
    CHECK(GetInt(Id::KeyB) == SDL_SCANCODE_BACKSPACE, "a number keeps the default");
    CHECK(GetInt(Id::KeyX) == SDL_SCANCODE_LSHIFT, "a boolean keeps the default");
    CHECK(GetInt(Id::KeyY) == SDL_SCANCODE_LCTRL, "an empty name keeps the default");
    CHECK(GetInt(Id::KeyLB) == SDL_SCANCODE_Q, "a missing key keeps the default");
    CHECK(IsDefault(Id::KeyLB) && !IsDefault(Id::KeyRightTrigger), "IsDefault follows the file");

    // Shown and logged as SDL's name of the position.
    CHECK(FormatValue(Id::KeyRightTrigger, GetValue(Id::KeyRightTrigger)) == "Up", "FormatValue: %s",
        FormatValue(Id::KeyRightTrigger, GetValue(Id::KeyRightTrigger)).c_str());
    CHECK(FormatValue(Id::KeyX, double(SDL_SCANCODE_LSHIFT)) == "Left Shift", "a two-word name");
    CHECK(FormatValue(Id::KeyA, 0) == "?" && FormatValue(Id::KeyA, 1e9) == "?" && FormatValue(Id::KeyA, NAN) == "?" &&
        FormatValue(Id::KeyA, 1) == "?", "not a key: ?");

    // Set: a scancode with a name, or nothing happens.
    CHECK(Set(Id::KeyA, SDL_SCANCODE_RETURN) == SetResult::Applied, "a key");
    CHECK(Set(Id::KeyA, SDL_SCANCODE_RETURN) == SetResult::Unchanged, "the same key again");
    CHECK(Set(Id::KeyA, 0) == SetResult::Invalid, "0 (SDL_SCANCODE_UNKNOWN) isn't a key");
    CHECK(Set(Id::KeyA, SDL_SCANCODE_COUNT) == SetResult::Invalid, "past the end");
    CHECK(Set(Id::KeyA, SDL_SCANCODE_RETURN + 0.5) == SetResult::Invalid, "not a whole number");
    CHECK(Set(Id::KeyA, 1) == SetResult::Invalid, "scancode 1 has no name");
    CHECK(Set(Id::KeyA, NAN) == SetResult::Invalid, "NaN");
    CHECK(GetInt(Id::KeyA) == SDL_SCANCODE_RETURN, "invalid sets change nothing: %d", GetInt(Id::KeyA));
    CHECK(StepValue(Id::KeyA, SDL_SCANCODE_RETURN, 1) == SDL_SCANCODE_RETURN &&
        StepValue(Id::KeyA, SDL_SCANCODE_RETURN, -1) == SDL_SCANCODE_RETURN, "Left and Right do nothing to a key");
    CHECK(!IsDefault(Id::KeyA) && Reset(Id::KeyA) == SetResult::Applied && IsDefault(Id::KeyA) && GetInt(Id::KeyA) == SDL_SCANCODE_SPACE, "Reset");

    // Save: every key by name, the comment, no environment variable; and
    // what it wrote loads back.
    CHECK(Save(file), "saves: %s", LastError().c_str());
    std::string text;
    {
        std::ifstream f(file);
        std::stringstream s;
        s << f.rdbuf();
        text = s.str();
    }
    size_t at = text.find("\n[keys]\n"), advanced = text.find("\n[advanced]\n");
    CHECK(at != std::string::npos && advanced != std::string::npos && at < advanced, "[keys] before [advanced]");
    std::string section = at == std::string::npos ? "" : text.substr(at, advanced == std::string::npos ? std::string::npos : advanced - at);
    CHECK(section.find("right_trigger = \"Up\"\n") != std::string::npos, "saved as its name");
    CHECK(section.find("left_trigger = \"Keypad 1\"\n") != std::string::npos, "a two-word name, quoted");
    CHECK(section.find("right_stick_left = \"K\"\n") != std::string::npos, "SDL's spelling, not the file's");
    CHECK(section.find("right_stick_right = \";\"\n") != std::string::npos, "punctuation in a name is quoted as it is");
    CHECK(section.find("right_stick_up = \"Left Shift\"\n") != std::string::npos, "SDL's case, not the file's");
    CHECK(section.find("left_stick_click = \"Z\"\n") != std::string::npos, "a default is written too");
    CHECK(section.find("# A key name as SDL spells it") != std::string::npos, "the comment says what goes here");
    CHECK(section.find("NFSMW_") == std::string::npos, "no environment variable on a key");
    CHECK(text.find("(NFSMW_VIBRATION)") != std::string::npos, "the other options keep theirs");
    CHECK(Load(file), "loads what it saved: %s", LastError().c_str());
    CHECK(GetInt(Id::KeyRightTrigger) == SDL_SCANCODE_UP && GetInt(Id::KeyLeftTrigger) == SDL_SCANCODE_KP_1 &&
        GetInt(Id::KeyRightStickUp) == SDL_SCANCODE_LSHIFT, "round trip");
    CHECK(!Dirty(), "nothing changed since the save");
    std::filesystem::remove_all(dir);
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
ninja -C build/main settings_keys_test && build/main/tests/settings_keys_test
```
Expected: `FAILED` with lines such as `"Up": 26` (the default W stayed: strings aren't parsed yet), `a key` (Set returned Invalid), `saved as its name`.

- [ ] **Step 3: Add the Key cases**

In `runtime/user/settings.cpp`:

`Normalize`, a new case before the function's trailing `return {};`:
```cpp
            case Type::Key:
                // A scancode SDL has a name for: the file holds the name.
                if (!(v >= 1 && v < SDL_SCANCODE_COUNT) || v != std::floor(v))
                    return {};
                if (SDL_GetScancodeName(SDL_Scancode(int(v)))[0] == '\0')
                    return {};
                return v;
```

`RangeText`:
```cpp
            case Type::Key:
                return "a key name as SDL spells it (\"W\", \"Left Shift\", \"Return\", \"Keypad 1\")";
```

`RawFromText`:
```cpp
            case Type::Key:
                if (SDL_Scancode sc = SDL_GetScancodeFromName(std::string(text).c_str()); sc != SDL_SCANCODE_UNKNOWN)
                    return double(sc);
                return {};
```

`RawFromToml`, in the switch on numbers (strings already go through `RawFromText`, booleans already fall to `std::nullopt` for anything but Bool):
```cpp
            case Type::Key:
                return {};  // names only
```

`TomlValue`:
```cpp
            case Type::Key:
            {
                const char* name = v >= 1 && v < SDL_SCANCODE_COUNT ? SDL_GetScancodeName(SDL_Scancode(int(v))) : "";
                if (name[0] == '\0')
                    name = SDL_GetScancodeName(SDL_Scancode(int(o.defaultValue)));
                return std::format("\"{}\"", name);
            }
```

`StepValue`:
```cpp
        case Type::Key:
            return value;  // a key has no neighbour: the menu takes a key press instead
```

`FormatValue`, in the first switch (beside Bool and Enum, which return):
```cpp
        case Type::Key:
        {
            if (!(value >= 1 && value < SDL_SCANCODE_COUNT))
                return "?";
            const char* name = SDL_GetScancodeName(SDL_Scancode(int(value)));
            return name[0] ? name : "?";
        }
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
ninja -C build/main settings_keys_test && build/main/tests/settings_keys_test
```
Expected: `passed (0 failures)`. The stderr shows the skip lines for the bad values, e.g. `settings.toml: keys.a = "Not A Key": not a key name as SDL spells it (...); ignored`.

- [ ] **Step 5: Build the game and commit**

Run: `ninja -C build/main SpeedBreaker` — expected: builds with no new warnings.

```bash
git add runtime/user/settings.cpp tests/settings_keys_test.cpp
git commit -m "Settings: a Key is read and written as SDL's name of it

right_trigger = \"W\" in settings.toml, in any case; a number, a
boolean or an unknown name keeps the default with a log line, as a bad
enum token does. Set takes only a scancode SDL can name, so the file
never gets an empty name; Left and Right leave a key alone.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: `hid/key_map.h`: the controls and the resolution rules

**Files:**
- Create: `runtime/hid/key_map.h`
- Create: `tests/key_map_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's `settings::Id::Key*`.
- Produces (namespace `hid::keymap`): `enum class Control` (24 + `Count`), `kControls`, `enum class Role`, `enum class Axis { LX, LY, RX, RY, Count }`, `namespace button` (the 14 XInput bits), `struct Entry { Control control; settings::Id id; Role role; uint16_t button; Axis axis; }`, `constexpr Entry kEntries[kControls]`, `struct Resolved { uint16_t buttons; bool leftTrigger, rightTrigger; bool axisSet[4]; float axis[4]; }`, `constexpr Resolved Resolve(std::span<const bool, kControls> pressed)`.

- [ ] **Step 1: Write the failing test**

Create `tests/key_map_test.cpp`:

```cpp
// Tests for hid/key_map.h: the 24 keyboard-driven controls and how held keys
// become buttons, triggers and stick axes. Header only; from the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/key_map_test.cpp -o build/key_map_test && build/key_map_test
#include <hid/key_map.h>

#include <cstdio>
#include <initializer_list>
#include <set>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

using namespace hid::keymap;

static Resolved Press(std::initializer_list<Control> held)
{
    bool pressed[kControls] = {};
    for (Control c : held)
        pressed[size_t(c)] = true;
    return Resolve(pressed);
}

static bool NoAxis(const Resolved& r)
{
    for (size_t a = 0; a < size_t(Axis::Count); a++)
        if (r.axisSet[a] || r.axis[a] != 0.0f)
            return false;
    return true;
}

int main()
{
    // The table: every control once, in order, each with its own setting
    // among the Key options.
    CHECK(kControls == 24, "%zu controls", kControls);
    std::set<settings::Id> ids;
    for (size_t i = 0; i < kControls; i++)
    {
        CHECK(size_t(kEntries[i].control) == i, "entry %zu is its control", i);
        CHECK(ids.insert(kEntries[i].id).second, "entry %zu: its own setting", i);
        CHECK(kEntries[i].id >= settings::Id::KeyRightTrigger && kEntries[i].id <= settings::Id::KeyRightStickClick,
            "entry %zu: a Key option", i);
        if (kEntries[i].role == Role::Button)
            CHECK(kEntries[i].button != 0 && (kEntries[i].button & (kEntries[i].button - 1)) == 0, "entry %zu: one bit", i);
        else
            CHECK(kEntries[i].button == 0, "entry %zu: no bit", i);
    }
    CHECK(kEntries[size_t(Control::RightTrigger)].id == settings::Id::KeyRightTrigger &&
        kEntries[size_t(Control::RightStickClick)].id == settings::Id::KeyRightStickClick, "first and last");
    // The bits are XInput's (xbox.h's XAMINPUT_GAMEPAD_*; input.cpp asserts the same).
    CHECK(button::DpadUp == 0x0001 && button::DpadDown == 0x0002 && button::DpadLeft == 0x0004 && button::DpadRight == 0x0008 &&
        button::Start == 0x0010 && button::Back == 0x0020 && button::LeftThumb == 0x0040 && button::RightThumb == 0x0080 &&
        button::LeftShoulder == 0x0100 && button::RightShoulder == 0x0200 &&
        button::A == 0x1000 && button::B == 0x2000 && button::X == 0x4000 && button::Y == 0x8000, "XInput's bits");

    // Nothing held: nothing.
    Resolved r = Press({});
    CHECK(r.buttons == 0 && !r.leftTrigger && !r.rightTrigger && NoAxis(r), "idle");

    // A button key sets its bit and nothing else.
    r = Press({ Control::A });
    CHECK(r.buttons == button::A && !r.leftTrigger && !r.rightTrigger && NoAxis(r), "A: 0x%04x", r.buttons);
    CHECK(Press({ Control::B }).buttons == button::B && Press({ Control::X }).buttons == button::X && Press({ Control::Y }).buttons == button::Y, "B X Y");
    CHECK(Press({ Control::LB }).buttons == button::LeftShoulder && Press({ Control::RB }).buttons == button::RightShoulder, "LB RB");
    CHECK(Press({ Control::DpadUp }).buttons == button::DpadUp && Press({ Control::DpadDown }).buttons == button::DpadDown &&
        Press({ Control::DpadLeft }).buttons == button::DpadLeft && Press({ Control::DpadRight }).buttons == button::DpadRight, "D-pad");
    CHECK(Press({ Control::Start }).buttons == button::Start && Press({ Control::Back }).buttons == button::Back, "Start Back");
    CHECK(Press({ Control::LeftStickClick }).buttons == button::LeftThumb && Press({ Control::RightStickClick }).buttons == button::RightThumb, "clicks");
    r = Press({ Control::DpadLeft, Control::Back, Control::X });
    CHECK(r.buttons == (button::DpadLeft | button::Back | button::X), "several: 0x%04x", r.buttons);

    // A trigger key is that trigger at full.
    r = Press({ Control::RightTrigger });
    CHECK(r.rightTrigger && !r.leftTrigger && r.buttons == 0 && NoAxis(r), "right trigger");
    r = Press({ Control::LeftTrigger });
    CHECK(r.leftTrigger && !r.rightTrigger, "left trigger");

    // An axis is the difference of its keys, set while either is down, so
    // both keys centre the stick and no key leaves the controller's value.
    const size_t LX = size_t(Axis::LX), LY = size_t(Axis::LY), RX = size_t(Axis::RX), RY = size_t(Axis::RY);
    r = Press({ Control::LeftStickRight });
    CHECK(r.axisSet[LX] && r.axis[LX] == 1.0f && !r.axisSet[LY] && !r.axisSet[RX] && !r.axisSet[RY] && r.buttons == 0, "right: +1");
    r = Press({ Control::LeftStickLeft });
    CHECK(r.axisSet[LX] && r.axis[LX] == -1.0f, "left: -1");
    r = Press({ Control::LeftStickLeft, Control::LeftStickRight });
    CHECK(r.axisSet[LX] && r.axis[LX] == 0.0f, "both: set, centred");
    r = Press({ Control::LeftStickUp });
    CHECK(r.axisSet[LY] && r.axis[LY] == 1.0f && !r.axisSet[LX], "up is positive");
    r = Press({ Control::LeftStickDown });
    CHECK(r.axisSet[LY] && r.axis[LY] == -1.0f, "down is negative");
    r = Press({ Control::RightStickUp });
    CHECK(r.axisSet[RY] && r.axis[RY] == 1.0f && !r.axisSet[RX] && !r.axisSet[LY], "right stick up");
    r = Press({ Control::RightStickDown });
    CHECK(r.axisSet[RY] && r.axis[RY] == -1.0f, "right stick down");
    r = Press({ Control::RightStickLeft });
    CHECK(r.axisSet[RX] && r.axis[RX] == -1.0f && !r.axisSet[RY], "right stick left");
    r = Press({ Control::RightStickRight });
    CHECK(r.axisSet[RX] && r.axis[RX] == 1.0f, "right stick right");

    // One key on a button and an axis (the default Up): both fire.
    r = Press({ Control::DpadUp, Control::LeftStickUp });
    CHECK(r.buttons == button::DpadUp && r.axisSet[LY] && r.axis[LY] == 1.0f, "Up: D-pad and stick");

    // Everything at once is still each thing.
    bool all[kControls];
    for (bool& b : all)
        b = true;
    r = Resolve(all);
    CHECK(r.buttons == 0xF3FF && r.leftTrigger && r.rightTrigger, "all buttons: 0x%04x", r.buttons);
    for (size_t a = 0; a < size_t(Axis::Count); a++)
        CHECK(r.axisSet[a] && r.axis[a] == 0.0f, "axis %zu: both keys, centred", a);

    // Usable at compile time (constexpr), so the table is checked when it's built.
    constexpr bool kNone[kControls] = {};
    static_assert(Resolve(kNone).buttons == 0, "constexpr");

    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
```

Add to `tests/CMakeLists.txt`, after the `settings_keys_test` lines:

```cmake
# The keyboard's controls and how held keys become a pad (hid/key_map.h), header only.
sb_test(key_map_test)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake -S . -B build/main -DSB_TESTS=ON && ninja -C build/main key_map_test
```
Expected: `'hid/key_map.h' file not found`.

- [ ] **Step 3: Write the header**

Create `runtime/hid/key_map.h`:

```cpp
// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The keyboard as an Xbox 360 controller (Settings > Controls): the 24
// controls a key can drive, in the page's order, each with the setting that
// holds its key (user/settings.h, the [keys] table of settings.toml), and
// how a set of held keys becomes buttons, triggers and stick axes. Header
// only and SDL-free: hid/input.cpp samples the keys and feeds the result to
// the pad; tests/key_map_test.cpp checks the rules.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

#include <user/settings.h>

namespace hid::keymap
{
    enum class Control : uint8_t
    {
        RightTrigger, LeftTrigger,
        LeftStickLeft, LeftStickRight, LeftStickUp, LeftStickDown,
        A, B, X, Y, LB, RB,
        DpadUp, DpadDown, DpadLeft, DpadRight,
        Start, Back,
        RightStickUp, RightStickDown, RightStickLeft, RightStickRight,
        LeftStickClick, RightStickClick,
        Count
    };
    constexpr size_t kControls = size_t(Control::Count);

    enum class Role : uint8_t { Button, LeftTrigger, RightTrigger, AxisNeg, AxisPos };
    enum class Axis : uint8_t { LX, LY, RX, RY, Count };

    // XInput's button bits, as the game reads them (xbox.h's
    // XAMINPUT_GAMEPAD_*; input.cpp asserts they agree). Literals here so
    // this header needs no kernel header.
    namespace button
    {
        constexpr uint16_t DpadUp = 0x0001, DpadDown = 0x0002, DpadLeft = 0x0004, DpadRight = 0x0008;
        constexpr uint16_t Start = 0x0010, Back = 0x0020, LeftThumb = 0x0040, RightThumb = 0x0080;
        constexpr uint16_t LeftShoulder = 0x0100, RightShoulder = 0x0200;
        constexpr uint16_t A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;
    }

    struct Entry
    {
        Control control;
        settings::Id id;       // the option holding its scancode
        Role role;
        uint16_t button = 0;   // Role::Button
        Axis axis = Axis::LX;  // Role::AxisNeg, AxisPos
    };

    constexpr Entry kEntries[kControls] = {
        { Control::RightTrigger, settings::Id::KeyRightTrigger, Role::RightTrigger },
        { Control::LeftTrigger, settings::Id::KeyLeftTrigger, Role::LeftTrigger },
        { Control::LeftStickLeft, settings::Id::KeyLeftStickLeft, Role::AxisNeg, 0, Axis::LX },
        { Control::LeftStickRight, settings::Id::KeyLeftStickRight, Role::AxisPos, 0, Axis::LX },
        { Control::LeftStickUp, settings::Id::KeyLeftStickUp, Role::AxisPos, 0, Axis::LY },
        { Control::LeftStickDown, settings::Id::KeyLeftStickDown, Role::AxisNeg, 0, Axis::LY },
        { Control::A, settings::Id::KeyA, Role::Button, button::A },
        { Control::B, settings::Id::KeyB, Role::Button, button::B },
        { Control::X, settings::Id::KeyX, Role::Button, button::X },
        { Control::Y, settings::Id::KeyY, Role::Button, button::Y },
        { Control::LB, settings::Id::KeyLB, Role::Button, button::LeftShoulder },
        { Control::RB, settings::Id::KeyRB, Role::Button, button::RightShoulder },
        { Control::DpadUp, settings::Id::KeyDpadUp, Role::Button, button::DpadUp },
        { Control::DpadDown, settings::Id::KeyDpadDown, Role::Button, button::DpadDown },
        { Control::DpadLeft, settings::Id::KeyDpadLeft, Role::Button, button::DpadLeft },
        { Control::DpadRight, settings::Id::KeyDpadRight, Role::Button, button::DpadRight },
        { Control::Start, settings::Id::KeyStart, Role::Button, button::Start },
        { Control::Back, settings::Id::KeyBack, Role::Button, button::Back },
        { Control::RightStickUp, settings::Id::KeyRightStickUp, Role::AxisPos, 0, Axis::RY },
        { Control::RightStickDown, settings::Id::KeyRightStickDown, Role::AxisNeg, 0, Axis::RY },
        { Control::RightStickLeft, settings::Id::KeyRightStickLeft, Role::AxisNeg, 0, Axis::RX },
        { Control::RightStickRight, settings::Id::KeyRightStickRight, Role::AxisPos, 0, Axis::RX },
        { Control::LeftStickClick, settings::Id::KeyLeftStickClick, Role::Button, button::LeftThumb },
        { Control::RightStickClick, settings::Id::KeyRightStickClick, Role::Button, button::RightThumb },
    };

    consteval bool EntriesInOrder()
    {
        for (size_t i = 0; i < kControls; i++)
            if (size_t(kEntries[i].control) != i)
                return false;
        return true;
    }
    static_assert(EntriesInOrder(), "one Entry per Control, in Control order");

    // What the held keys amount to.
    struct Resolved
    {
        uint16_t buttons = 0;
        bool leftTrigger = false, rightTrigger = false;  // a trigger key is down: that trigger at full
        bool axisSet[size_t(Axis::Count)] = {};          // either key of the axis is down: the keyboard has the axis
        float axis[size_t(Axis::Count)] = {};            // +1, 0 (both keys) or -1 while set; up is positive, as XInput has it
    };

    // pressed[i]: the key bound to Control(i) is down. Buttons OR in; an axis
    // is its positive key minus its negative one, and belongs to the
    // keyboard while either is held (both held: centred).
    constexpr Resolved Resolve(std::span<const bool, kControls> pressed)
    {
        Resolved r;
        for (size_t i = 0; i < kControls; i++)
        {
            if (!pressed[i])
                continue;
            const Entry& e = kEntries[i];
            switch (e.role)
            {
            case Role::Button:
                r.buttons |= e.button;
                break;
            case Role::LeftTrigger:
                r.leftTrigger = true;
                break;
            case Role::RightTrigger:
                r.rightTrigger = true;
                break;
            case Role::AxisNeg:
                r.axisSet[size_t(e.axis)] = true;
                r.axis[size_t(e.axis)] -= 1.0f;
                break;
            case Role::AxisPos:
                r.axisSet[size_t(e.axis)] = true;
                r.axis[size_t(e.axis)] += 1.0f;
                break;
            }
        }
        return r;
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
ninja -C build/main key_map_test && build/main/tests/key_map_test
```
Expected: `passed (0 failures)`.

- [ ] **Step 5: Commit**

```bash
git add runtime/hid/key_map.h tests/key_map_test.cpp tests/CMakeLists.txt
git commit -m "hid: key_map.h, the keyboard's controls and their rules

The 24 controls a key can drive, each with the setting that holds its
key, and how held keys become buttons, triggers and stick axes: an
axis is the difference of its two keys and belongs to the keyboard
while either is down. Header only, with a unit test.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: The input path reads the bindings

**Files:**
- Modify: `runtime/hid/input.cpp:1-10` (includes), `:611-640` (the keyboard block in `Update()`)
- Modify: `runtime/hid/input.h:9-12` (the keyboard line of the header comment)

**Interfaces:**
- Consumes: Task 3's `hid::keymap::kEntries`, `kControls`, `Resolve`, `Resolved`, `Axis`, `button::*`; Task 1's `settings::GetInt(Id::Key*)`.
- Produces: the game sees the bound keys as the pad. No new interface.

- [ ] **Step 1: Discard the working-tree O/L edit**

The uncommitted change in `runtime/hid/input.cpp` (three lines reading `SDL_SCANCODE_O` and `SDL_SCANCODE_L`) is subsumed by the defaults of Task 1. Run:
```bash
git diff --stat runtime/hid/input.cpp
```
Expected: `3 insertions(+)` in the keyboard block only. If so, the block is replaced wholesale in Step 3; nothing to keep.

- [ ] **Step 2: Add the include and the bit asserts**

In `runtime/hid/input.cpp`, after `#include "input.h"`:
```cpp
#include "key_map.h"
```
After the includes, before `namespace {`:
```cpp
// key_map.h names XInput's bits as literals (it has no kernel header): keep
// them the bits the game reads.
static_assert(hid::keymap::button::DpadUp == XAMINPUT_GAMEPAD_DPAD_UP && hid::keymap::button::DpadDown == XAMINPUT_GAMEPAD_DPAD_DOWN &&
    hid::keymap::button::DpadLeft == XAMINPUT_GAMEPAD_DPAD_LEFT && hid::keymap::button::DpadRight == XAMINPUT_GAMEPAD_DPAD_RIGHT &&
    hid::keymap::button::Start == XAMINPUT_GAMEPAD_START && hid::keymap::button::Back == XAMINPUT_GAMEPAD_BACK &&
    hid::keymap::button::LeftThumb == XAMINPUT_GAMEPAD_LEFT_THUMB && hid::keymap::button::RightThumb == XAMINPUT_GAMEPAD_RIGHT_THUMB &&
    hid::keymap::button::LeftShoulder == XAMINPUT_GAMEPAD_LEFT_SHOULDER && hid::keymap::button::RightShoulder == XAMINPUT_GAMEPAD_RIGHT_SHOULDER &&
    hid::keymap::button::A == XAMINPUT_GAMEPAD_A && hid::keymap::button::B == XAMINPUT_GAMEPAD_B &&
    hid::keymap::button::X == XAMINPUT_GAMEPAD_X && hid::keymap::button::Y == XAMINPUT_GAMEPAD_Y,
    "key_map.h's button bits are XInput's");
```

- [ ] **Step 3: Replace the keyboard block**

In `Update()`, replace everything from `// Keyboard (digital; overrides the analog values it touches).` through the closing brace of `if (k)` (the block that ends with the `ry = keyboardRY;` lines) with:

```cpp
        // Keyboard (digital; replaces the controller's value for what it
        // touches): each control's key is Settings > Controls (the Key
        // options), resolved by key_map.h. A key is a scancode SDL's state
        // array is indexed by; an out-of-range value (a hand edit beat
        // Load's checks) counts as not held.
        if (const bool* k = SDL_GetKeyboardState(nullptr))
        {
            bool pressed[keymap::kControls] = {};
            for (size_t i = 0; i < keymap::kControls; i++)
            {
                int scancode = settings::GetInt(keymap::kEntries[i].id);
                pressed[i] = scancode > 0 && scancode < SDL_SCANCODE_COUNT && k[scancode];
            }
            keymap::Resolved r = keymap::Resolve(pressed);
            p.buttons |= r.buttons;
            if (r.rightTrigger)
                rt = 1.0f;
            if (r.leftTrigger)
                lt = 1.0f;
            float* axes[size_t(keymap::Axis::Count)] = { &lx, &ly, &rx, &ry };  // Axis order: LX, LY, RX, RY
            for (size_t a = 0; a < size_t(keymap::Axis::Count); a++)
                if (r.axisSet[a])
                    *axes[a] = r.axis[a];
        }
```

- [ ] **Step 4: Update the header comment**

In `runtime/hid/input.h`, replace the three `//   - Keyboard: ...` lines with:

```cpp
//   - Keyboard: each Xbox 360 control has a key (Settings > Controls; the
//     [keys] table of settings.toml; hid/key_map.h lists them). Defaults:
//     arrows = D-pad, A/D = steer, Up/Down = left stick too, W/S = gas/brake
//     (RT/LT), Space = A, Backspace = B, Left Shift = X, Left Ctrl = Y,
//     Q/E = LB/RB, Enter = START, Esc = BACK, O/L/K/; = right stick,
//     Z/C = the stick clicks. One key per control; a key may drive several.
```

- [ ] **Step 5: Build**

Run:
```bash
ninja -C build/main SpeedBreaker && git diff --stat
```
Expected: builds with no new warnings; the diff touches only `input.cpp` and `input.h`; no `SDL_SCANCODE_` literal remains in input.cpp's `Update()` (`grep -n "SDL_SCANCODE_" runtime/hid/input.cpp` shows only `SDL_SCANCODE_COUNT`).

- [ ] **Step 6: Commit**

```bash
git add runtime/hid/input.cpp runtime/hid/input.h
git commit -m "hid: the keyboard follows Settings > Controls

One pass over the 24 bindings through key_map.h replaces the fixed
scancode table and the stick, trigger and O/L special cases. An axis
belongs to the keyboard while either of its keys is down, which the
newer right-stick code already did and the older arrow code didn't.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: The Controls page: key rows and the capture

**Files:**
- Modify: `runtime/ui/settings_menu.cpp` (state near line 28; `ValueWidth` at 54-75; a new `KeyLabel`, `SharedWith`, `KeyRow` after `OptionRow`; `HelpLine` at 299-342; the page loop at 414-421; the legend at 479-484; the B check at 516-518; `ResetSettingsMenu` at 522-531; a new `SettingsMenuTakesKey`)
- Modify: `runtime/ui/ui.h:118-123` (declare the hook beside `DrawSettings`)
- Modify: `runtime/ui/ui.cpp:561-573` (the key-down case)

**Interfaces:**
- Consumes: Task 1's `Type::Key`, `Category::Controls`; Task 2's `Set`, `FormatValue`, `GetStoredValue`.
- Produces: `bool ui::SettingsMenuTakesKey(const SDL_KeyboardEvent& key)`.

- [ ] **Step 1: The hook's declaration and call**

In `runtime/ui/ui.h`, after `void ResetSettingsMenu();  // on opening: ...`:

```cpp
    // Main thread, from ProcessEvent on a key-down while the menu is open,
    // after the F1 toggle: true when a Controls row was waiting for a key
    // and took this one (or Esc cancelled it), so ImGui must not see it.
    bool SettingsMenuTakesKey(const SDL_KeyboardEvent& key);
```

In `runtime/ui/ui.cpp`, in `ProcessEvent`'s `SDL_EVENT_KEY_DOWN` case, after the `if (toggle && !e.key.repeat && !s_modal) { ...; return; }` block and before its `break;`:

```cpp
            if (s_settingsOpen && SettingsMenuTakesKey(e.key))
                return;  // a Controls row took it: ImGui doesn't act on it too
```

- [ ] **Step 2: State and helpers in settings_menu.cpp**

Add the include after `#include <imgui_internal.h>`:
```cpp
#include <SDL3/SDL_keyboard.h>  // a key row's label on this layout
```

After `bool s_opened = true;        // just opened: take focus`:
```cpp
        Id s_capturing = Id::Count;  // a Controls row waiting for a key (KeyRow; SettingsMenuTakesKey takes it)
        constexpr const char* kPressAKey = "Press a key";
```

After `int PageDelta()` and before `ValueWidth`:

```cpp
        // A key row's label: the key's name on this keyboard's layout ("Z"
        // on a French Mac for the position the file calls "W"), or SDL's
        // name of the position when the layout has none. Main thread.
        std::string KeyLabel(double stored)
        {
            if (!(stored >= 1 && stored < SDL_SCANCODE_COUNT))
                return "?";
            SDL_Scancode sc = SDL_Scancode(int(stored));
            const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false));
            if (!name || !name[0])
                name = SDL_GetScancodeName(sc);
            return name && name[0] ? name : "?";
        }

        // The other controls this row's key also drives ("D-pad Up, Left
        // Stick Up"), in page order; "" when it is this row's alone.
        std::string SharedWith(const settings::Option& o)
        {
            double key = settings::GetStoredValue(o.id);
            std::string text;
            for (const auto& other : settings::Options())
                if (other.type == settings::Type::Key && other.id != o.id && settings::GetStoredValue(other.id) == key)
                    text += (text.empty() ? "" : ", ") + std::string(other.label);
            return text;
        }
```

In `ValueWidth`, add a case before `default:`:
```cpp
            case settings::Type::Key:
                width = std::max(width, ImGui::CalcTextSize(KeyLabel(settings::GetStoredValue(o.id)).c_str()).x);
                width = std::max(width, ImGui::CalcTextSize(kPressAKey).x);
                break;
```

- [ ] **Step 3: The key row**

After `OptionRow` (before `const char* BugReportValue()`), add:

```cpp
        // A Controls row: the key that drives the control. A, Enter or a
        // click waits for the next key, which SettingsMenuTakesKey takes
        // from the event loop before ImGui sees it; the help line says so.
        // No arrows: Left and Right do nothing to a key. The wait follows
        // the focus (the D-pad moving on ends it) and B ends it (below, in
        // DrawSettings, where B would otherwise close the menu).
        void KeyRow(const settings::Option& o, float valueWidth, bool focus)
        {
            const float rowHeight = RowHeight();
            const double stored = settings::GetStoredValue(o.id);
            ImGui::PushID(int(o.id));
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableNextColumn();
            if (focus)
                ImGui::SetKeyboardFocusHere();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelInset());
            ImVec2 labelPos = ImGui::GetCursorScreenPos();
            bool activated = ImGui::Selectable("##row", false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowHeight));
            bool focused = ImGui::IsItemFocused();
            if (focused || (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem) && s_described == Id::Count))
                s_described = o.id;
            const float mid = RowLabel(labelPos, o.label);
            if (s_capturing == o.id && !focused)
                s_capturing = Id::Count;
            if (activated)
                s_capturing = o.id;
            const bool waiting = s_capturing == o.id;

            // The value where other pages put theirs (the arrows' room kept
            // so the columns line up from page to page).
            ImGui::TableNextColumn();
            float arrow = ImGui::GetFrameHeight();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowHeight - arrow) * 0.5f);
            ImVec2 min = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(valueWidth + arrow * 2, arrow));
            std::string text = waiting ? kPressAKey : KeyLabel(stored);
            ImVec2 size = ImGui::CalcTextSize(text.c_str());
            ImU32 color;
            if (waiting)
            {
                PushAccentText();
                color = ImGui::GetColorU32(ImGuiCol_Text);
                PopAccentText();
            }
            else
                color = ImGui::GetColorU32(focused ? ImGuiCol_PlotLines : ImGuiCol_Text);
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + arrow + (valueWidth - size.x) * 0.5f, CentredTextTop(mid)), color, text.c_str());
            ImGui::PopID();
        }
```

- [ ] **Step 4: The help line, the page loop, the legend, B and the reset**

In `HelpLine`, after `const settings::Option& o = settings::Info(s_described);` and before `ImGui::TextWrapped("%s", o.description);`:

```cpp
            if (o.type == settings::Type::Key && s_capturing == o.id)
            {
                PushAccentText();
                ImGui::Text("Press the key for %s. Esc cancels.", o.label);
                PopAccentText();
                ImGui::TextDisabled(" ");
                return;
            }
```
and right after `ImGui::TextWrapped("%s", o.description);`:
```cpp
            if (o.type == settings::Type::Key)
            {
                // A key on several controls is fine (the defaults share the
                // arrows); say which, in case it wasn't meant.
                std::string also = SharedWith(o);
                if (also.empty())
                    ImGui::TextDisabled(" ");
                else
                    ImGui::TextDisabled("Also: %s.", also.c_str());
                return;
            }
```

In `DrawSettings`, right after `s_described = Id::Count;` (before the tab bar), end a capture the page turned away from:
```cpp
        if (s_capturing != Id::Count && settings::Info(s_capturing).category != s_category)
            s_capturing = Id::Count;
```

In the page loop, replace
```cpp
                                OptionRow(o, valueWidth, focusFirst);
```
with
```cpp
                                if (o.type == settings::Type::Key)
                                    KeyRow(o, valueWidth, focusFirst);
                                else
                                    OptionRow(o, valueWidth, focusFirst);
```

The legend: replace `const char* legend = "D-pad: choose, change   LB RB: page   B: close";` with
```cpp
            const char* legend = s_category == Category::Controls ? "D-pad: choose   A: change key   LB RB: page   B: close"
                                                                 : "D-pad: choose, change   LB RB: page   B: close";
```

The B check at the end: replace
```cpp
        if (!popup && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
            (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            CloseSettings();
```
with
```cpp
        // B (or Esc) closes the menu; while a Controls row waits for a key
        // it ends the wait instead. (The Esc key itself never gets here
        // then: SettingsMenuTakesKey took its press.)
        if (!popup && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
            (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        {
            if (s_capturing != Id::Count)
                s_capturing = Id::Count;
            else
                CloseSettings();
        }
```

In `ResetSettingsMenu`, after `s_opened = true;`:
```cpp
        s_capturing = Id::Count;  // a wait the menu was closed under is over
```

After `ResetSettingsMenu` (still inside `namespace ui`), add the hook:

```cpp
    bool SettingsMenuTakesKey(const SDL_KeyboardEvent& key)
    {
        if (s_capturing == Id::Count || !key.down || key.repeat)
            return false;
        const settings::Option& o = settings::Info(s_capturing);
        s_capturing = Id::Count;
        if (key.scancode == SDL_SCANCODE_ESCAPE)
        {
            fprintf(stderr, "[settings] menu: %s.%s: no new key (Esc)\n", o.table, o.key);
            return true;
        }
        double from = settings::GetStoredValue(o.id);
        LogChange(o, from, settings::Set(o.id, double(key.scancode)));
        return true;
    }
```

- [ ] **Step 5: Build**

Run: `ninja -C build/main SpeedBreaker`
Expected: builds with no new warnings.

- [ ] **Step 6: The scripted menu run**

A scratch profile, the installed game by path, the menu driven by `NFSMW_UI_KEYS`: open (F1), four pages right to Controls (E x4), Enter on the first row (Right Trigger), Up; Down to the second row (Left Trigger), Enter, Left Shift; Esc closes and saves. Then a second launch, no keys, to see the file load. Run from the repo root (any scratch folder for `SCRATCH`):

```bash
SCRATCH="${TMPDIR:-/tmp}/speedbreaker-keys-run"
rm -rf "$SCRATCH" && mkdir -p "$SCRATCH/home"
GAME="$HOME/Library/Application Support/SpeedBreaker/game"
( HOME="$SCRATCH/home" NFSMW_GAME_DIR="$GAME" NFSMW_WINDOW=1376x576 SDL_AUDIO_DRIVER=dummy \
  NFSMW_UI_KEYS="8:F1,9:E,9.5:E,10:E,10.5:E,11.5:Return,12.5:Up,13.5:Down,14.5:Return,15.5:Left Shift,17:Escape" \
  build/main/runtime/SpeedBreaker > "$SCRATCH/run1.log" 2>&1 & pid=$!; sleep 24; kill $pid; wait $pid 2>/dev/null )
grep -n "\[ui\] ready\|menu: keys\.\|\[settings\] saved\|unknown key\|ignored" "$SCRATCH/run1.log"
sed -n '/^\[keys\]/,/^\[advanced\]/p' "$SCRATCH/home/Library/Application Support/SpeedBreaker/settings.toml" | grep -v '^#' | grep -v '^$'
```
Expected in `run1.log`, in this order:
```
[ui] ready ...
[settings] menu: keys.right_trigger W -> Up (applied now)
[settings] menu: keys.left_trigger S -> Left Shift (applied now)
[settings] saved .../settings.toml
```
and no `unknown key` or `ignored` line. Expected from the file: 24 lines, among them `right_trigger = "Up"`, `left_trigger = "Left Shift"`, `left_stick_click = "Z"`.

If `[ui] ready` comes later than 8 s in the log (a cold shader cache), raise every time in `NFSMW_UI_KEYS` by the difference and run again.

Second launch:
```bash
( HOME="$SCRATCH/home" NFSMW_GAME_DIR="$GAME" NFSMW_WINDOW=1376x576 SDL_AUDIO_DRIVER=dummy \
  build/main/runtime/SpeedBreaker > "$SCRATCH/run2.log" 2>&1 & pid=$!; sleep 12; kill $pid; wait $pid 2>/dev/null )
grep -n "values from the file\|unknown key\|ignored\|keys\." "$SCRATCH/run2.log"
```
Expected: one line `[settings] .../settings.toml: 45 values from the file` and nothing ignored.

- [ ] **Step 7: Commit**

```bash
git add runtime/ui/settings_menu.cpp runtime/ui/ui.h runtime/ui/ui.cpp
git commit -m "Settings menu: the Controls page

A row per keyboard-driven control. A, Enter or a click waits for a
key, which the event loop hands to the menu before ImGui sees it, so
Enter, Space, the arrows and Q or E can be bound too; Esc or B ends
the wait. The row shows the key on this layout; the help line lists
the other controls that share it. Reset page restores the defaults
through the option table as on every page.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: README, CHANGELOG and TESTING.md

**Files:**
- Modify: `README.md:77-78` (the keyboard bullet), `:316-333` (the Controls table and the notes under it), `:376-377` (the Input line of the settings list)
- Modify: `CHANGELOG.md:6` (a new heading above `## v0.1.1`)
- Modify: `docs/TESTING.md:21-25` (the menu section)

**Interfaces:** none.

- [ ] **Step 1: README**

The keyboard bullet (line 77-78) becomes:
```markdown
- **Your controller, or the keyboard.** Controllers work as an Xbox 360 pad, with hot-plugging
  and rumble support. Every keyboard key can be reassigned in the settings.
```

The Controls table becomes:
```markdown
| Xbox 360 control | Controller | Keyboard |
|---|---|---|
| Left stick | Left stick | A / D steer; Up / Down push it up and down |
| Right trigger | Right trigger | W |
| Left trigger | Left trigger | S |
| A / B / X / Y | Bottom / right / left / top face button | Space / Backspace / Left Shift / Left Ctrl |
| LB / RB | Shoulder buttons | Q / E |
| D-pad | D-pad | Arrow keys |
| Start | Start / Menu / Options | Enter |
| Back | Back / View / Create | Esc |
| Right stick | Right stick | O / L up and down, K / ; left and right |
| Stick clicks | Press a stick in | Z (left), C (right) |
```

Under it, replace `- There is no button remapping and no mouse control in game.` with:
```markdown
- Every key can be changed: **Settings > Controls** (F1), choose a row, press A or Enter, then
  the key you want (Esc cancels). One key per control, and a key may drive several controls, as
  the arrows do. *Reset page* brings this table back. There is no mouse control in game.
```

In the settings list, after the `**Input:** ...` paragraph, add:
```markdown
**Controls:** the keyboard key for each Xbox 360 control, as in the table under
[Controls](#controls). The keys are saved by name in `settings.toml`'s `[keys]` table.
```

- [ ] **Step 2: CHANGELOG**

Insert above `## v0.1.1`:
```markdown
## Unreleased

### What's new

- Settings > Controls: every keyboard key can be reassigned. Choose a row, press A or Enter,
  then the new key. One key per control, and a key may drive several controls; *Reset page*
  brings the defaults back. The keys are saved by name in settings.toml's `[keys]` table.
- The right stick has keys: O / L up and down, K / ; left and right. The left stick click is Z.
- Left and Right arrows no longer steer; A and D do. The arrows are still the D-pad, and Up and
  Down still move the left stick. Settings > Controls brings arrow steering back if you prefer it.

```

- [ ] **Step 3: TESTING.md**

In the `## The menu` section, after the D-pad line, add:
```markdown
- The **Controls** page: choose a row, press A (or Enter), then the key you want; Esc cancels.
  *Reset page* restores the default keys.
```

- [ ] **Step 4: Check the build still configures**

Run:
```bash
cmake -S . -B build/main 2>&1 | grep -c "has no entry" ; grep -c "Unreleased" build/main/runtime/generated/whats_new.inc
```
Expected: both counts are `0` (each `grep -c` prints `0` and exits 1, which is fine here): the configure gives no `CHANGELOG.md has no entry for v0.1.1` warning, and the v0.1.1 entry is still what is compiled in. The new heading sits above it and isn't picked up until the version is bumped.

- [ ] **Step 5: Commit**

```bash
git add README.md CHANGELOG.md docs/TESTING.md
git commit -m "Docs: Settings > Controls

The README's controls table has the new keys and points at the page;
the CHANGELOG's unreleased entry notes the feature and that A/D steer
now, not the arrows.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Hands-on verification (the human partner, with the game)

**Files:** none. This task has no code; it is the check the scripted runs can't do, since `NFSMW_UI_KEYS` events don't reach SDL's held-key array that the game's input reads.

- [ ] **Step 1: Driving on a rebound key**

Launch `build/main/runtime/SpeedBreaker` (the real profile is fine: Reset page undoes everything). Start or load into a race. F1, LB/RB to Controls, A or Enter on Right Trigger, press Up. Close with Esc.
Expected: holding Up accelerates; W no longer does. The log (`~/Library/Application Support/SpeedBreaker/logs/`, newest) has `menu: keys.right_trigger W -> Up (applied now)` and `saved`.

- [ ] **Step 2: Back on another key**

F1, Controls, Back row, Enter, press F2 (any unbound key). Close with Esc (the menu's own Esc, not the game's).
Expected: Esc no longer reaches the game (no pause from Esc in the race); F2 does.

- [ ] **Step 3: A page turn and a focus move end the wait**

F1, Controls, Enter on a row ("Press a key" shows), press RB (or E on a second keyboard isn't possible: use a controller's RB, or click the Advanced tab).
Expected: the page turns, and the next key pressed anywhere binds nothing (the log stays silent). Back on Controls: Enter on a row, then D-pad down. Expected: the row shows its key again, not "Press a key".

- [ ] **Step 4: Relaunch and Reset page**

Quit and launch again. F1, Controls.
Expected: Right Trigger shows Up and Back shows F2. Press Reset page, confirm.
Expected: every row shows the README table's key; the log has one `menu: keys.<x> ... (applied now)` line per row that changed.

- [ ] **Step 5: Shared keys in the help line**

With the defaults, focus D-pad Up.
Expected: the help's second line reads `Also: Left Stick Up.`; on Right Trigger it is blank.

- [ ] **Step 6: Record the result**

No commit. Tell the implementer which steps passed; a failure goes back to the task that owns the code (Task 4 for driving, Task 5 for the menu).
