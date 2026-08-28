#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

// ── What a Host talks to ─────────────────────────────────────────────────────
//
// The other half of host.hh. A Host owns the real OS window and drives the
// message pump; everything it needs to TELL the application goes through here.
//
// This interface exists so the seam is symmetric. Host was already portable —
// no application type appears in any of its own methods — but its init() took a
// concrete PlayerWindow*, and each backend then called that class's methods by
// name. That is what tied three otherwise generic message pumps to one music
// player, and it is the only thing that did: measured across the three
// backends, they call NINETEEN methods that any application would have
// (a pointer moved, a key went down, the surface died) and TWO that only a
// music player could (see onAppEvent and onHostReady below, which is where
// those two went).
//
// Almost everything here has an empty default rather than being pure. An
// application that draws but takes no keyboard, or runs on a platform with no
// mouse wheel, should not have to write empty overrides to say so — and a
// backend that learns to report something new must not break every existing
// consumer on the day it does. The two exceptions are the two an application
// cannot meaningfully lack: it must be able to lay itself out, and it must be
// able to shut down.
class AppView {
public:
    virtual ~AppView() = default;

    // An explicit size change the app should treat as a new layout pass.
    virtual void onHostResized() = 0;
    // Teardown before the window and the renderer die.
    virtual void shutdown() = 0;

    // A routine resize notification: relayout, but do NOT tell the renderer
    // its surface changed. The distinction is real on Wayland, where a
    // configure arrives for reasons that are not a new drawable size.
    virtual void onHostLayoutInvalidated() {}
    // The window became visible or was uncovered; nothing changed but the
    // frame on screen is stale.
    virtual void onHostExposed() {}

    // The window now HAS INPUT FOCUS — a stronger statement than being visible,
    // and the two are not the same moment: Android hands an app its surface,
    // lets it draw a frame, and only then gives the window focus.
    //
    // It exists because anything an app asks of the input system before that
    // point is discarded. A launcher that raises the IME from onHostReady()
    // gets "Ignoring showSoftInput() ... is not served" from
    // InputMethodManager and a home screen with no keyboard, which is not a
    // bug the app can fix on its own side: the moment it needs to wait for is
    // one only the host can see. Fires on every focus gain, so an app doing
    // one-time work here must guard it itself.
    //
    // Wayland and Win32 do not report it today; an app that only runs there
    // never sees it and must not depend on it as its sole trigger.
    virtual void onHostFocusGained() {}

    // Keyboard, in the portable key::* space (vk_canvas's keys.hh), never in
    // the platform's own keycodes.
    virtual void onKeyDownPortable(int keyCode) {}
    // The release, for the same reason onLButtonUp exists: a HELD modifier is
    // not expressible as a stream of down edges. "Was Ctrl down at the moment
    // C's edge arrived?" needs both ends, and every host already has them —
    // InputSink::onKey carries a `down` flag the hosts currently drop.
    virtual void onKeyUpPortable(int keyCode) {}
    virtual void onCharPortable(uint32_t codepoint) {}

    // A platform INPUT METHOD replaced the focused field's whole contents.
    //
    // Not a variant of onCharPortable, and it cannot be expressed as one:
    // composing Korean jamo into a syllable, or choosing a Chinese candidate
    // over its pinyin reading, REWRITES the pending run — the text on screen a
    // moment ago is not a prefix of the text that follows. A stream of "a
    // character was typed" has nowhere to put that.
    //
    // So the platform states the field's authoritative contents and the app
    // adopts them wholesale, which also leaves composition state owned by the
    // IME — the only place it can correctly live, since reconstructing it here
    // would be writing an IME. `cursorByte` is a byte offset into `text`, not
    // a character index, because that is what a UTF-8 buffer can act on
    // without re-scanning.
    //
    // Desktop backends never emit this: Win32 WM_CHAR and Wayland's xkb path
    // both produce finished characters and keep using onCharPortable.
    virtual void onTextEditPortable(const std::string& text, size_t cursorByte) {}
    // A system-wide or focused-window hotkey the host registered on the app's
    // behalf. The IDs are the application's own vocabulary; the host only
    // carries them back.
    virtual void onHotkey(int hotkeyId) {}

    // The window may have moved to a different monitor. Windows reports this;
    // Wayland cannot, and Android has one screen, so on those it never fires.
    virtual void adaptToCurrentMonitor() {}

    // ── The drawing surface can come and go ─────────────────────────────────
    //
    // On a desktop the window is born once and dies once. On Android the
    // surface dies EVERY time the user leaves the app and is born again on
    // return, taking the swapchain, every texture and the glyph atlas with it.
    // Neither desktop backend calls either of these, by construction.
    //
    // The split they enforce is the one that matters: CPU-side state survives,
    // GPU-side state does not. Getting it wrong is invisible until the second
    // visit, when every string on screen is gone.
    virtual void onSurfaceLost() {}
    // Rebuilds what the above released, against the host's NEW surface.
    // Returning false means the app must not be drawn.
    virtual bool onSurfaceRecreated() { return true; }

    // Pointer. On a touch screen the host synthesises these from taps, which is
    // why hover exists there at all — see the slop note on onDragEnd.
    virtual void onMouseMove(int x, int y) {}
    virtual void onMouseLeave() {}
    virtual void onLButtonDown(int x, int y) {}
    // The release. Every host already sees it — that is where onDragEnd below
    // is computed from — but until now none forwarded it, because a retained
    // UI that hit-tests on press has no use for it.
    //
    // An IMMEDIATE-MODE app does: a slider, a scrollbar and a press-and-cancel
    // button are all "is the button still held, and did it just go up this
    // frame?", and that question cannot be answered from a down edge and a
    // drag summary. See frame_input_view.hh, which turns this back into
    // FrameInput::pointerWentUp.
    //
    // Fires on every release, including a tap that never moved — unlike
    // onDragEnd, which is deliberately silent below the host's slop.
    virtual void onLButtonUp(int x, int y) {}
    virtual void onLButtonDblClk(int x, int y) {}

    // ── Raw multi-pointer touch ─────────────────────────────────────────────
    //
    // The callbacks above are ONE pointer, because a mouse is one pointer and
    // they were shaped by a desktop. A touch screen is not: a pinch is two
    // fingers whose DISTANCE is the whole signal, and no amount of
    // single-pointer history reconstructs it — the midpoint of two fingers can
    // sit perfectly still while they separate.
    //
    // So these carry the pointer's IDENTITY, and are delivered raw: no slop,
    // no tap/drag classification, no synthesis into a wheel. A host reports
    // every pointer it sees; what a second finger MEANS is the app's question,
    // exactly as onAppEvent's integer is.
    //
    // They are additive. Android keeps synthesising the single-pointer events
    // above from pointer 0 as it always did, so an app that overrides nothing
    // here sees no change at all. An app that wants pinch overrides these and
    // ignores those.
    //
    // `pointerId` is stable for the life of one finger's contact and is reused
    // afterwards; it is NOT an index into anything. The desktops report their
    // single pointer as id 0 — a documented narrowing in the same spirit as
    // registerHotkey being system-wide on Win32 and focus-local on Wayland. A
    // mouse genuinely has one pointer; pretending otherwise would be the fake.
    virtual void onPointerDown(int pointerId, int x, int y) {}
    virtual void onPointerMove(int pointerId, int x, int y) {}
    // Fires for a lifted finger AND for a cancelled one (the window losing the
    // gesture to a system edge swipe). An app that tracks a set of live
    // pointers must remove on both or it strands a finger that never came up.
    virtual void onPointerUp(int pointerId, int x, int y) {}
    virtual void onMouseWheel(int x, int y, int delta) {}

    // A drag that has ENDED: the pointer travelled dx,dy with the button held
    // and has just been released. Reported by the host rather than
    // reconstructed here, because every platform already tracks it — Win32 from
    // WM_LBUTTONDOWN/UP, Wayland from PointerAction::Down/Up, Android from its
    // own touch slop. Which is also the rule: the SLOP is a host property and
    // the MEANING is an application one. A tap never reaches this.
    virtual void onDragEnd(int dx, int dy) {}

    // The two extra buttons on the side of a mouse (Win32 XBUTTON1/XBUTTON2,
    // evdev BTN_SIDE/BTN_EXTRA).
    virtual void onNavBack() {}
    virtual void onNavForward() {}

    // A timer the app started through Host::startTimer() has fired. `timerId`
    // is the app's own integer, handed back unread — see host.hh on why the
    // host does not own that vocabulary.
    virtual void onTimer(int timerId) {}

    // ── The two that used to be method calls by name ────────────────────────

    // A cross-thread completion posted from any thread via
    // Host::postAppEvent(), delivered here on the UI thread from inside
    // pump(). `id`, `p1` and `p2` are opaque to every host: the same three
    // integers come back out that went in.
    //
    // This one hook replaced the same switch statement written out FOUR times —
    // once in each backend and once more in the headless capture tool — each
    // naming the application's own methods. They could not drift, being
    // identical; they could only all have to change together, which is worse.
    virtual void onAppEvent(int id, intptr_t p1, intptr_t p2) {}

    // The first moment the application is fully constructed AND the host is
    // running: after create() has returned, from inside the first pump(). May
    // fire again later on platforms that suspend and resume, so an app that
    // does one-time work here must guard it itself.
    //
    // It exists because a platform can arrive already KNOWING something the app
    // would otherwise have to ask a user for — Android is launched with an
    // extra on its intent, and there is no file browser on the path to that
    // answer. The host offers that through Host::launchArgument() and says
    // nothing about what it means; deciding is the app's job, which is exactly
    // the line that was crossed when the host called commitAddFolder() itself.
    virtual void onHostReady() {}
};
