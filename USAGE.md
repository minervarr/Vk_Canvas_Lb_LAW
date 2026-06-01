# vulkan_canvas_engine — usage

GPU-rendered 2D canvas for Android NDK. You describe a frame as a flat list of
curve records; a Vulkan compute pipeline rasterises text + filled rects. Font
glyphs come from the `vulkan_font_engine` submodule.

## What's in the box

| File | Role |
|------|------|
| `canvas.{hh,cc}` | Immediate-mode draw API: `clear/rect/text/button`, clipping, `textWidth`. Emits curve records. |
| `textarea.{hh,cc}` | Reusable **scrollable, word-wrapped** text view. Layout + scroll only (no editing yet). |
| `renderer.{hh,cc}` | Vulkan device/swapchain; rasterises a curve buffer each frame. |
| `app.{hh,cc}` / `main.cc` | Minimal `android_main` host (event loop + window lifecycle). |
| `font.*`, `glyphs.*` | From `vulkan_font_engine`: FreeType glyphs → curve records. |

The engine has **no input system**. The host (your `android_main`) reads touch
events and calls into widgets (e.g. `TextArea::scrollBy`).

## Drawing a frame

```cpp
std::vector<float> curves;                       // reused buffer
Canvas c(curves, screenW, screenH, font,         // font may be nullptr
         insetTop, insetBottom, insetLeft, insetRight);
c.clear(col::bg);
c.rect(x, y, w, h, col::panel);
c.text("hello", x, y, size, col::text);
c.button(bx, by, bw, bh, "OK", col::btnIdle, col::text);
// upload `curves` to the renderer and dispatch (see renderer.hh)
```

Coordinates are screen pixels. `Canvas` exposes the safe content box via
`left()/top()/right()/bottom()/w()/h()/pad()` (screen minus system-bar insets).

## TextArea (scrollable text)

```cpp
TextArea ta;
ta.setRect(x, y, w, h);          // where it lives
ta.setColors(col::text, col::panel);
ta.setText(bigString);           // replaces content, keeps scroll
ta.scrollToBottom();             // pin to end when appending live text

// per-frame:
ta.render(canvas, font);         // re-wraps + clamps scroll, draws thumb

// from host input:
ta.onDragStart / scrollBy(dy);   // dy>0 reveals later text
```

`render()` clamps scroll to the content automatically — the host never tracks
content height. Editing (caret/keyboard/selection) is intentionally not here;
`TextArea` is the layout+scroll foundation it would build on.

## ⚠️ NativeActivity + JNI: load the library yourself

A `NativeActivity` loads the native lib through the manifest
`android.app.lib_name` meta-data. That load path does **not** register the
library for resolving explicitly-declared `native` methods on your Java
subclass. Any `private static native ...` you add will throw
`UnsatisfiedLinkError` ("No implementation found ... is the library loaded?")
unless the Java class loads it itself:

```java
public class MainActivity extends NativeActivity {
    static { System.loadLibrary("your_lib_name"); }  // matches add_library() name
    private static native void nativeCallback(String s);
}
```

Symptom if you forget: the JNI symbol exists in the `.so` (verify with
`llvm-nm -D libyour_lib.so`) but the upcall still fails at runtime. Don't wrap
the native call in a `catch (UnsatisfiedLinkError) {}` that swallows it — log it.

## Build notes

- C++17, `-O2`, ABIs `arm64-v8a` / `x86_64`.
- Shaders are Slang → SPIR-V (`slangc`); `assets/shaders/*.spv` are committed.
- Init submodules first: `git submodule update --init --recursive`.
- When reusing in another app, add `canvas.cc` **and** `textarea.cc` to that
  app's CMake source list (see whisper's `app/src/main/CMakeLists.txt`).


IDK:
The NativeActivity + JNI loadLibrary gotcha, in one line: a NativeActivity loads your .so via the manifest's android.app.lib_name, but that does not make the JVM resolve the custom native methods you declare on your Java subclass — so they throw UnsatisfiedLinkError even though the symbol is in the .so. The fix is a static { System.loadLibrary("whisper_android"); } block in MainActivity. That was the exact cause of your "stuck on LOADING until restart" bug (the failing call was swallowed by a silent catch).

What I created:
- vulkan_canvas_engine/USAGE.md — concise manual: what each file does (Canvas, TextArea, Renderer, font), how to draw a frame, how to use the scrollable TextArea, the NativeActivity/JNI warning, and build notes.
- Memory: nativeactivity-jni-loadlibrary.md (the gotcha + how it was diagnosed) and vulkan-canvas-engine-widgets.md (the reusable-widgets architecture), both added to MEMORY.md.

One note: USAGE.md and textarea.{hh,cc} live inside the vulkan_canvas_engine submodule, so they're changes in that separate repo — you'll want to commit them there (not just in the whisper superproject) for the reuse to persist.