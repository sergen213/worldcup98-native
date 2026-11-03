// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Maps a Qt key press to the SDL scancode name stored in settings.ini (physical key position, as
// SDL uses). xcb and Wayland report XKB keycodes (= Linux evdev code + 8) as the native scan code.
#include <QKeyEvent>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include <string>
namespace wc98::launcher {

inline SDL_Scancode evdevToSdl(quint32 code) {
    static constexpr SDL_Scancode t[] = {
        SDL_SCANCODE_UNKNOWN,
        SDL_SCANCODE_ESCAPE,
        SDL_SCANCODE_1,
        SDL_SCANCODE_2,
        SDL_SCANCODE_3,
        SDL_SCANCODE_4,
        SDL_SCANCODE_5,
        SDL_SCANCODE_6,
        SDL_SCANCODE_7,
        SDL_SCANCODE_8,
        SDL_SCANCODE_9,
        SDL_SCANCODE_0,
        SDL_SCANCODE_MINUS,
        SDL_SCANCODE_EQUALS,
        SDL_SCANCODE_BACKSPACE,
        SDL_SCANCODE_TAB,
        SDL_SCANCODE_Q,
        SDL_SCANCODE_W,
        SDL_SCANCODE_E,
        SDL_SCANCODE_R,
        SDL_SCANCODE_T,
        SDL_SCANCODE_Y,
        SDL_SCANCODE_U,
        SDL_SCANCODE_I,
        SDL_SCANCODE_O,
        SDL_SCANCODE_P,
        SDL_SCANCODE_LEFTBRACKET,
        SDL_SCANCODE_RIGHTBRACKET,
        SDL_SCANCODE_RETURN,
        SDL_SCANCODE_LCTRL,
        SDL_SCANCODE_A,
        SDL_SCANCODE_S,
        SDL_SCANCODE_D,
        SDL_SCANCODE_F,
        SDL_SCANCODE_G,
        SDL_SCANCODE_H,
        SDL_SCANCODE_J,
        SDL_SCANCODE_K,
        SDL_SCANCODE_L,
        SDL_SCANCODE_SEMICOLON,
        SDL_SCANCODE_APOSTROPHE,
        SDL_SCANCODE_GRAVE,
        SDL_SCANCODE_LSHIFT,
        SDL_SCANCODE_BACKSLASH,
        SDL_SCANCODE_Z,
        SDL_SCANCODE_X,
        SDL_SCANCODE_C,
        SDL_SCANCODE_V,
        SDL_SCANCODE_B,
        SDL_SCANCODE_N,
        SDL_SCANCODE_M,
        SDL_SCANCODE_COMMA,
        SDL_SCANCODE_PERIOD,
        SDL_SCANCODE_SLASH,
        SDL_SCANCODE_RSHIFT,
        SDL_SCANCODE_KP_MULTIPLY,
        SDL_SCANCODE_LALT,
        SDL_SCANCODE_SPACE,
        SDL_SCANCODE_CAPSLOCK,
        SDL_SCANCODE_F1,
        SDL_SCANCODE_F2,
        SDL_SCANCODE_F3,
        SDL_SCANCODE_F4,
        SDL_SCANCODE_F5,
        SDL_SCANCODE_F6,
        SDL_SCANCODE_F7,
        SDL_SCANCODE_F8,
        SDL_SCANCODE_F9,
        SDL_SCANCODE_F10,
        SDL_SCANCODE_NUMLOCKCLEAR,
        SDL_SCANCODE_SCROLLLOCK,
        SDL_SCANCODE_KP_7,
        SDL_SCANCODE_KP_8,
        SDL_SCANCODE_KP_9,
        SDL_SCANCODE_KP_MINUS,
        SDL_SCANCODE_KP_4,
        SDL_SCANCODE_KP_5,
        SDL_SCANCODE_KP_6,
        SDL_SCANCODE_KP_PLUS,
        SDL_SCANCODE_KP_1,
        SDL_SCANCODE_KP_2,
        SDL_SCANCODE_KP_3,
        SDL_SCANCODE_KP_0,
        SDL_SCANCODE_KP_PERIOD,
        SDL_SCANCODE_UNKNOWN,
        SDL_SCANCODE_UNKNOWN,
        SDL_SCANCODE_NONUSBACKSLASH,
        SDL_SCANCODE_F11,
        SDL_SCANCODE_F12, // 84..88
    };
    static_assert(std::size(t) == 89);
    if (code < std::size(t)) return t[code];
    switch (code) {
    case 96:
        return SDL_SCANCODE_KP_ENTER;
    case 97:
        return SDL_SCANCODE_RCTRL;
    case 98:
        return SDL_SCANCODE_KP_DIVIDE;
    case 99:
        return SDL_SCANCODE_PRINTSCREEN;
    case 100:
        return SDL_SCANCODE_RALT;
    case 102:
        return SDL_SCANCODE_HOME;
    case 103:
        return SDL_SCANCODE_UP;
    case 104:
        return SDL_SCANCODE_PAGEUP;
    case 105:
        return SDL_SCANCODE_LEFT;
    case 106:
        return SDL_SCANCODE_RIGHT;
    case 107:
        return SDL_SCANCODE_END;
    case 108:
        return SDL_SCANCODE_DOWN;
    case 109:
        return SDL_SCANCODE_PAGEDOWN;
    case 110:
        return SDL_SCANCODE_INSERT;
    case 111:
        return SDL_SCANCODE_DELETE;
    case 119:
        return SDL_SCANCODE_PAUSE;
    case 125:
        return SDL_SCANCODE_LGUI;
    case 126:
        return SDL_SCANCODE_RGUI;
    case 127:
        return SDL_SCANCODE_APPLICATION;
    default:
        return SDL_SCANCODE_UNKNOWN;
    }
}

// Fallback for platforms without XKB scan codes (e.g. QT_QPA_PLATFORM=offscreen): key symbol.
inline SDL_Scancode qtKeyToSdl(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return SDL_Scancode(SDL_SCANCODE_A + (key - Qt::Key_A));
    if (key >= Qt::Key_1 && key <= Qt::Key_9) return SDL_Scancode(SDL_SCANCODE_1 + (key - Qt::Key_1));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return SDL_Scancode(SDL_SCANCODE_F1 + (key - Qt::Key_F1));
    switch (key) {
    case Qt::Key_0:
        return SDL_SCANCODE_0;
    case Qt::Key_Escape:
        return SDL_SCANCODE_ESCAPE;
    case Qt::Key_Space:
        return SDL_SCANCODE_SPACE;
    case Qt::Key_Return:
        return SDL_SCANCODE_RETURN;
    case Qt::Key_Enter:
        return SDL_SCANCODE_KP_ENTER;
    case Qt::Key_Tab:
        return SDL_SCANCODE_TAB;
    case Qt::Key_Backspace:
        return SDL_SCANCODE_BACKSPACE;
    case Qt::Key_Up:
        return SDL_SCANCODE_UP;
    case Qt::Key_Down:
        return SDL_SCANCODE_DOWN;
    case Qt::Key_Left:
        return SDL_SCANCODE_LEFT;
    case Qt::Key_Right:
        return SDL_SCANCODE_RIGHT;
    case Qt::Key_Control:
        return SDL_SCANCODE_LCTRL;
    case Qt::Key_Shift:
        return SDL_SCANCODE_LSHIFT;
    case Qt::Key_Alt:
        return SDL_SCANCODE_LALT;
    case Qt::Key_Insert:
        return SDL_SCANCODE_INSERT;
    case Qt::Key_Delete:
        return SDL_SCANCODE_DELETE;
    case Qt::Key_Home:
        return SDL_SCANCODE_HOME;
    case Qt::Key_End:
        return SDL_SCANCODE_END;
    case Qt::Key_PageUp:
        return SDL_SCANCODE_PAGEUP;
    case Qt::Key_PageDown:
        return SDL_SCANCODE_PAGEDOWN;
    default:
        return SDL_SCANCODE_UNKNOWN;
    }
}

// "" when the key has no SDL equivalent.
inline std::string sdlKeyName(const QKeyEvent& e) {
    SDL_Scancode sc = e.nativeScanCode() > 8 ? evdevToSdl(e.nativeScanCode() - 8) : SDL_SCANCODE_UNKNOWN;
    if (sc == SDL_SCANCODE_UNKNOWN) sc = qtKeyToSdl(e.key());
    return sc == SDL_SCANCODE_UNKNOWN ? std::string() : std::string(SDL_GetScancodeName(sc));
}

} // namespace wc98::launcher
