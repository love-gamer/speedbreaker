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
        CHECK(o.defaultValue >= 1 && o.defaultValue < double(SDL_SCANCODE_COUNT), "%s: the default is a scancode", o.key);
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

    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
