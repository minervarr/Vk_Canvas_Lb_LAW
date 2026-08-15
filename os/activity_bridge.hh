#pragma once
#include <android_native_app_glue.h>

#include <cstddef>
#include <string>

// ── Talking to AppShellActivity ──────────────────────────────────────────────
//
// The C++ end of platform/android/java/io/nava/appshell/AppShellActivity.java.
// Two directions, and they are asymmetric on purpose:
//
//   UP   (here -> Java)  plain blocking JNI calls. The Java side marshals to
//                        the UI thread itself wherever Android demands it, so
//                        callers here need not care which thread they are on.
//   DOWN (Java -> here)  never a direct call into the app. Java hands us a
//                        snapshot under a lock and the host collects it from
//                        inside pump(), on the app thread.
//
// That second rule is the important one. Android's UI thread and the glue's
// app thread are different threads; an IME callback that reached into the app's
// widget state directly would be a data race against the frame being drawn.
// Everything therefore lands in one small slot, and drain() empties it at a
// point in the frame where touching app state is safe.
namespace activity {

// Must be called once, before any JNI entry point can fire — the down-calls
// need the android_app to find a JNIEnv, and they arrive on a thread that has
// no other way to reach it.
void set_app(android_app* app);

// Rings the host's doorbell after something has been stored below.
//
// Storing is not enough, and the difference is the whole reason this exists: a
// blocked pump() is asleep in ALooper_pollOnce with no timeout, and Android's
// UI thread dropping text into a mutex does not wake it. Measured on a phone —
// every character typed was received, decoded and parked correctly, and the
// screen did not change until the user touched it, because a touch is an event
// the LOOPER knows about and an IME callback is not.
//
// AndroidHost installs one that writes its eventfd. Nothing is lost if none is
// installed: the next event of any kind drains the slot as well.
void set_waker(void (*wake)());

// ── Up-calls ────────────────────────────────────────────────────────────────

// Raise the IME, seeding its buffer. `cursorByte` is a BYTE offset into `text`
// (what a UTF-8 app has); it is converted to the UTF-16 code-unit index Java
// wants on the way through, since the two indices have no fixed relationship.
void show_keyboard(const std::string& text, size_t cursorByte);
void hide_keyboard();

void set_clipboard(const std::string& utf8);
std::string get_clipboard();

// Caller must already have validated the scheme — see Host::openUrl.
bool open_url(const std::string& url);

// Root of SHARED storage as a real path, e.g. "/storage/emulated/0". Empty if
// the activity does not answer (which is what a consumer whose Activity does
// not extend AppShellActivity gets); callers should fall back rather than
// treat "" as a directory.
//
// NOT app_paths::stateDir(). That one is private to the app and is deleted on
// uninstall — the right place for a cache and the wrong place for a library
// the user believes is theirs.
std::string external_storage_root();

// ── Down-calls, collected ───────────────────────────────────────────────────

// Everything Java has reported since the last drain, coalesced.
//
// Text is a SLOT rather than a queue: an input method fires one of these per
// keystroke of a composition and each supersedes the last, so keeping the
// older ones would only replay a word being typed. The flags and the insets
// are latched, so nothing is lost if two arrive between frames.
struct Update {
    bool        hasText    = false;
    std::string text;
    size_t      cursorByte = 0;

    bool committed = false;   // user accepted (IME action / Enter)
    bool dismissed = false;   // IME went away on its own (back gesture)

    // How much of the bottom edge the keyboard covers, 0 when it is down.
    // The display CUTOUT is not here — that is read natively in safe_area.cc,
    // and the two are kept apart on purpose (see Host::keyboardInset).
    bool imeInsetChanged = false;
    int  imeBottom       = 0;
};

// Takes and clears whatever is pending. Returns false when nothing was
// waiting, so a caller can skip the work without comparing fields.
bool drain(Update& out);

} // namespace activity
