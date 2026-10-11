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

    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
