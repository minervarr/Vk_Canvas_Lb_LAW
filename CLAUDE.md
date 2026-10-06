# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in `app_shell`.

## What this is

**The application shell.** `vk_canvas` makes *drawing* portable — a Renderer, a
Canvas, MSDF/raster text, and three real backends. This library makes the
*application* portable: the window, the message pump, the input, where the files
live, how big the text is, and which way round the screen is.

Together they are the answer to one question: **can a new app be written once
and run on Windows, Linux and Android?** vk_canvas alone gets you a picture.
This gets you a program.

An app that builds on it writes:

```cpp
class MyApp : public AppView {
public:
    bool create() {
        host_ = make_host();               // or an injected one
        return host_ && host_->init(this);
    }
    void run() {                           // the loop is the APP's, see below
        while (running_) {
            host_->pump(/*haveWork=*/dirty_);
            if (host_->quitRequested()) break;
            if (dirty_) { draw(); dirty_ = false; }
        }
    }
    void onHostResized() override { dirty_ = true; }
    void shutdown()      override { running_ = false; }
    std::unique_ptr<Host> host_;
    bool running_ = true, dirty_ = true;
};

int app_shell_main() { MyApp app; return app.create() ? (app.run(), 0) : 1; }
```

…and gets `main()`, `WinMain()`, the Wayland and Win32 event loops, the crash
handler, the log file, and the Android `Host` for free. Android needs one extra
file — an `android_main()` that constructs the app and an `AndroidHost` — and
that file is six lines long.

**`create()` and `run()` are the APP's, not this library's.** app_shell owns the
platform BOOTSTRAP and hands control over; it does not own the frame loop,
because what counts as "work to do" is the app's question — an editor idles at
zero frames, a game never idles. `docs/app_shell.tex` is the full reference
manual; read it before extending the seam.

Extracted from Matrix Player, which is still its first and, for now, only
consumer. **API-shaping decisions should favour the NEXT consumer, not that
one** — that is the whole reason this is a separate library rather than a folder
in `gui/`.

It is its own repository (github.com/minervarr/App_shell), added as a git
submodule at `framework/app_shell` by every consumer — the same shape
`audio_engine` and `vk_canvas` already use. A second app adds it the same way:
`git submodule add https://github.com/minervarr/App_shell.git framework/app_shell`,
then `add_subdirectory()`s it and links `app_shell` plus the one platform host it
needs. The split that matters is still the one in the SOURCE, never in version
control: nothing in here may know what a track is, regardless of how it is
cloned. This repository is committed and pushed with its own `git_wrapper` —
never plain `git commit`/`git push` — from inside `framework/app_shell/`; the
consumer's own `git_wrapper push` pushes this submodule first, then itself.

---

## Layout

```
app_shell/
  app_view.hh        — what a Host calls. The app implements it.
  host.hh            — what the app calls. A platform implements it.
  app_main.hh        — app_shell_main(), plus the three names an app supplies
  app_paths.hh/.cc   — exeDir() / stateDir(), and the rule about the split
  frame_input_view.hh— an AppView for an IMMEDIATE-MODE app (see below)
  ui_metrics.hh/.cc  — one scale factor, five type roles, space()/stroke()
  ui_orientation.*   — Horizontal or Vertical, derived from the window's shape
  utf16_utf8.hh/.cc  — real UTF-8 <-> UTF-16, because JNI's converters are not
  layout_rect.hh     — a portable rectangle (replaces Win32's RECT)
  color.hh           — a portable colour (replaces COLORREF)
  os/
    win32_host.cc          — real Win32 window + message pump + minidump
    wayland_host.cc        — real Wayland, via vk_canvas's WaylandDisplay/Window
    android_host.hh/.cc    — real ALooper/ANativeWindow host
    activity_bridge.*      — the C++ end of AppShellActivity (IME, clipboard, URL)
    app_paths_android.*    — Android's answer to exeDir()/stateDir()
    launch_intent.*        — read a string extra off the launch intent
    safe_area.*            — the display CUTOUT (never a system bar, never the IME)
    storage_permission.*   — all-files access, asked at most once
  platform/android/java/io/nava/appshell/AppShellActivity.java
                     — the ONLY Java here. Text input needs it; nothing else does.
  tests/             — plain assert(), Debug-only, no framework
  cmake/AppShellMinTextSize.cmake — the build-time text-size floor generator
```

`app_shell` (portable) links nothing. `app_shell_win32` / `app_shell_wayland` /
`app_shell_android` are one host each, and a consumer links exactly one.

### Two input styles, one Host

`Host` speaks in callbacks — "the left button went down at 40,12". An
**immediate-mode** UI asks the opposite question while it draws: "is the pointer
in this rect AND did it go down this frame?" Both are legitimate, and which one
an app wants is not a platform question — so it is not baked into `Host`.

`frame_input_view.hh` is the adapter: an `AppView` that pours the callbacks into
vk_canvas's `FrameInput` (which is itself an `InputSink`, so the conversion is
the one the engine already ships). An app that inherits from it reads
`input().pointerWentDown` and gets every widget in vk_canvas's `widgets.hh` for
free. Matrix Player does **not** use it — it hit-tests on the callbacks directly
and contains no `FrameInput` anywhere.

That adapter is why `AppView` has `onLButtonUp` and `onKeyUpPortable`. Every host
already saw both — the up is where `onDragEnd` is computed from, and `KeyEvent`
carries a `down` flag — but none forwarded them, because a retained UI that
hit-tests on press never needs them. A slider, a scrollbar and a held Ctrl all do.

### The Java half, and why there is exactly one file of it

Everything else on Android is C++ reaching the system through JNI. Text input
cannot be: `NativeActivity` delivers keycodes, and an input method is not a
sequence of keystrokes — Korean assembles jamo into syllables, Chinese picks
candidates over a reading, dictation produces no keys at all. Reconstructing
that natively means writing an IME.

So `AppShellActivity` holds an off-screen `EditText`, lets Android's IME edit it,
and mirrors the whole buffer down through `AppView::onTextEditPortable(text,
cursorByte)` — the field's authoritative contents, not a delta, because the text
on screen a moment ago is not a prefix of what follows.

A consumer **subclasses it and must add its own `System.loadLibrary` static
block** — that cannot live here, since only the consumer knows the library name,
and without it every `native` method throws `UnsatisfiedLinkError` even though
the library is already mapped (NativeActivity `dlopen()`s it from native code,
which never registers it with the JVM). Gradle needs
`sourceSets.main.java.srcDirs += '<app_shell>/platform/android/java'`. The JNI
symbols encode the DECLARING class, so they resolve for any subclass.

### Keyboard inset is not a safe inset

`safeInsets()` is the display cutout. `keyboardInset()` is the on-screen
keyboard. They are never summed and never share a field, because a cutout is
glass — permanent, hardware, unremovable — and a keyboard is software that comes
and goes and only ever eats the bottom edge. Merging them gives a choice of two
bugs: a permanent dead strip once the keyboard has been up, or a camera notch
that stops being avoided the moment it goes down.

The cutout is read natively (`os/safe_area.cc`); only the keyboard height comes
through Java. Reporting the cutout through both would give one number two
sources that can disagree.

---

## The seam, and the three things that were wrong with it

`Host` and `AppView` are two halves of one interface. The rule is simply which
way the call goes: an app calls `Host`, a platform calls `AppView`.

This was lifted out of an application, and the lifting was mostly about
**removing that application's vocabulary from the platform half**. Three things
had to go, and the shapes they left behind are load-bearing:

1. **`Host::init()` took the concrete app class**, and each backend then called
   its methods by name. Measured across the three, they called nineteen methods
   any app would have and two only a music player could. Hence `AppView`, with
   almost everything defaulted to empty: an app that takes no keyboard should
   not have to write an empty override to say so.

2. **Cross-thread events and timer ids were ENUMS DECLARED IN `host.hh`** —
   `AppEvent{TrackChange, ScanDone, …}`, `TimerId{SeekUpdate}` — and every
   backend switched on them. That is one app's words in the one file that must
   know nothing about it, and it meant the SAME dispatch was written out four
   times over (three hosts plus a headless capture tool). Both are plain `int`
   now, carried and never read: `postAppEvent(id, p1, p2)` comes back out of
   `AppView::onAppEvent(id, p1, p2)` unchanged.

3. **`snapToEdge()` took the app's hotkey id**, so the Win32 host held a table
   mapping one player's Alt+F/J/C/U/G/H to window geometry. It takes a
   `SnapEdge` now — an edge is what a window system deals in — and hotkeys are
   *registered* by the app (`registerHotkey(id, keyCode)`) instead of being
   hardcoded per backend.

The same principle covers `Host::launchArgument()`: Android is launched with an
intent extra, and the host used to read a key named `"scan_root"` and hand it to
the app's own `commitAddFolder()`. Now `AndroidHost` is TOLD which key to read
(a constructor argument) and states the string; deciding what a launch argument
MEANS happens in `AppView::onHostReady()`, which is the app's.

**The test for anything new here: could a drawing program use it?** If the
answer needs the word "track", "album" or "playlist", it belongs in the app.

### Two rules learned by crashing on a phone

1. **Nothing decided before `pump()` may be trusted after it.** `pump()` is
   where a platform delivers "your surface is gone". A `bool canDraw` computed
   before the pump and used after it segfaults on the first launch that hides
   the system bars, because hiding them recreates the window.
2. **`safeInsets()` is the CUTOUT and never a system bar.** A bar is software
   and gets hidden; insetting for one that is not on screen leaves a permanent
   empty strip. A cutout is glass.

### What genuinely differs between platforms, stated rather than faked

- `snapToEdge` and `adaptToCurrentMonitor` are real no-ops on Wayland: a client
  cannot position itself or ask which monitor it is on. They are no-ops on
  Android for the more obvious reason.
- `registerHotkey` is SYSTEM-WIDE on Windows (`RegisterHotKey`) and
  focused-window-only on Wayland. A documented narrowing, not a silent drop.
- `onSurfaceLost()` / `onSurfaceRecreated()` are Android-only by construction —
  dead code on both desktops, and the split they enforce (CPU state survives,
  GPU state does not) is invisible until the second visit to the app.

---

## `ui_metrics` and the header it includes

`ui_metrics.hh` includes `ui_min_text_size.gen.h`, generated at build time from
**the consuming app's own fonts**: the emitted floor is the worst case across
them, so it is a per-application number and not a library constant.

The recipe is `cmake/AppShellMinTextSize.cmake`; the consumer calls
`app_shell_generate_min_text_size(<its faces>)` and then
`add_dependencies(app_shell generate_ui_min_text_size)`. It cannot run under a
cross-compiler, so an Android build copies the header out of a desktop build
tree — see any consumer's `android/CMakeLists.txt`.

`ui_metrics` takes the window's **short side**, not its height. Identical for
any window wider than tall, but a 1080x1920 monitor would otherwise scale 1.78x
because the screen is TALL, not big.

---

## The three names an app supplies

Cache variables, set before `add_subdirectory()`, all with working defaults so
this library configures and builds on its own:

| | |
|---|---|
| `APP_SHELL_APP_NAME` | the log file's base name, and the Android logcat tag |
| `APP_SHELL_WIN_CLASS` | the Win32 window class (only has to be unique per process) |
| `APP_SHELL_STATE_HOME` | directory under `$HOME` for what the app WRITES; empty means beside the executable |

Android also needs `APP_SHELL_NATIVE_APP_GLUE_DIR` — native_app_glue ships
inside the NDK, so only the consumer knows where it is.

`app_paths`'s split is the rule worth keeping: READ-ONLY shipped data is
exe-relative and stays that way; everything WRITTEN goes through `stateDir()`.
They are the same directory by default, so a build tree stays one self-contained
folder you can move anywhere; naming `APP_SHELL_STATE_HOME` splits them, which
is what a system package needs since `/opt` is root-owned.

---

## Tests

Debug-only, plain `assert()`, no framework — the convention this whole family of
repositories uses. They **compile** the sources rather than linking `app_shell`,
so a test can never quietly start depending on Vulkan.

```bash
./build/<tree>/app_shell_build/ui_metrics_test
./build/<tree>/app_shell_build/ui_orientation_test
./build/<tree>/app_shell_build/utf16_utf8_test
./build/<tree>/app_shell_build/touch_hover_test
```

Keep them pure. `utf16_utf8` is deliberately written over `uint16_t` rather than
`jchar` so this last one compiles on a desktop with no NDK in sight — which
matters more than it sounds, because the surrogate-pair cases it covers are
unreachable from any desktop run and would otherwise never be exercised at all.

---

## Committing

Use `./git_wrapper` from inside `framework/app_shell/`, never plain
`git commit`/`git push` — this is its own repository. Commit and push here
FIRST, then commit and push the consumer (its own `git_wrapper push` already
pushes submodules before itself, but doing it by hand in the right order is
the same rule vk_canvas and audio_engine follow).
