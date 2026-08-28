package io.nava.appshell;

import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.provider.MediaStore;
import android.media.MediaScannerConnection;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Display;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;

/**
 * The Java half of {@code app_shell_android}: {@link NativeActivity} plus the
 * things a purely native Android app cannot do for itself.
 *
 * <h2>Why this exists at all</h2>
 *
 * <p>Everything else in this library is C++, and {@code AndroidHost} reaches
 * the system through JNI without any Java of its own. Text input is the one
 * seam where that is not possible.
 *
 * <p>NativeActivity hands native code {@code AKEY_EVENT}s, which carry
 * keycodes, not characters. That is enough for Latin typing and useless for
 * everything else: Korean assembles jamo into syllables, Chinese and Japanese
 * choose candidates over a pinyin or romaji reading, and dictation produces no
 * key presses at all. None of that is a sequence of keystrokes, and
 * reconstructing it natively means writing an input method.
 *
 * <p>So we do not. An off-screen {@link EditText} holds the real buffer, the
 * IME edits it exactly as it would in any Android app, and a {@link
 * TextWatcher} mirrors the resulting string down to C++ after every change.
 * Composition, candidate windows, autocorrect, dictation, hardware keyboards
 * and paste all work because Android is doing them.
 *
 * <p>The field is positioned off-screen rather than made {@code GONE} or
 * zero-sized: an unfocusable or unlaid-out view is not a valid IME target, and
 * some input methods refuse to attach to one.
 *
 * <h2>Using it</h2>
 *
 * <p>Subclass it, and <strong>add a static initializer that loads your native
 * library</strong>:
 *
 * <pre>{@code
 * public class MyActivity extends AppShellActivity {
 *     static { System.loadLibrary("my_app"); }
 * }
 * }</pre>
 *
 * <p>That block is not optional and cannot live here, because only the
 * consumer knows the library's name. Without it every {@code native} method
 * below throws {@code UnsatisfiedLinkError} at runtime — even though the
 * library is already mapped into the process and the symbols are exported.
 * NativeActivity brings it up with {@code dlopen()} from its own native code,
 * which never tells the Java runtime about it, and the JVM resolves native
 * methods only against libraries it was itself asked to load. The second load
 * costs nothing: the runtime refcounts and simply registers it.
 *
 * <p>Point your Gradle module at this directory to compile it:
 *
 * <pre>{@code
 * sourceSets.main.java.srcDirs += 'path/to/app_shell/platform/android/java'
 * }</pre>
 *
 * <p>The JNI symbol names are derived from the class that DECLARES the native
 * methods, which is this one — so they resolve for any subclass, and a
 * consumer never writes JNI of its own.
 */
public class AppShellActivity extends NativeActivity {

    // ---- down-calls into C++ (see os/ime_bridge.cc) ------------------------

    /** Mirrors the IME's buffer natively. {@code cursor} is in UTF-16 units. */
    private static native void nativeOnTextChanged(String text, int cursor);

    /** The user accepted the text (IME action / Enter). */
    private static native void nativeOnTextCommitted();

    /** The IME went away without the native side asking (back gesture). */
    private static native void nativeOnKeyboardHidden();

    /**
     * How many pixels of the bottom of our window the on-screen keyboard is
     * currently covering. Zero when it is down.
     *
     * <p>The IME inset and ONLY the IME inset. The display cutout is read
     * natively instead ({@code os/safe_area.cc}), and reporting it here as well
     * would give the same number two sources that can disagree.
     *
     * <p>They are also different in kind, which is why {@code Host} keeps them
     * apart rather than summing them into one "unusable edge". A cutout is
     * glass: a hole through the display, there on every frame, that no API can
     * remove. A keyboard is software: it comes, it goes, and it only ever eats
     * the bottom. Merge them and you get a choice of two bugs — a permanent
     * dead strip along the bottom once the keyboard has been up once, or a
     * camera notch that stops being avoided the moment it goes down.
     *
     * <p>System bars contribute to neither: they are hidden rather than avoided
     * (see {@code platform/android/fullscreen.hh}), and insetting for a bar
     * that is not on screen just wastes the strip.
     */
    private static native void nativeOnImeInset(int bottom);

    private EditText input_;
    private boolean  suppress_;   // guards the echo while native sets the text

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);

        maybeRequestHdrColorMode();

        // Keep our own surface from being resized or panned when the IME opens.
        // The app draws its own layout and moves the focused field itself,
        // using the imeBottom inset above; letting the window manager also pan
        // would apply the correction twice.
        getWindow().setSoftInputMode(
                WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);

        input_ = new EditText(this);
        input_.setFocusable(true);
        input_.setFocusableInTouchMode(true);
        // Off-screen, not invisible: see the class comment.
        input_.setLayoutParams(new ViewGroup.LayoutParams(1, 1));
        input_.setX(-100.0f);
        input_.setY(-100.0f);
        input_.setGravity(Gravity.TOP);
        input_.setBackgroundColor(0);
        // NO_FULLSCREEN keeps the IME from replacing the screen with its own
        // extract editor in landscape, which would cover the app entirely.
        input_.setImeOptions(EditorInfo.IME_ACTION_SEARCH
                | EditorInfo.IME_FLAG_NO_FULLSCREEN);
        input_.setSingleLine(true);

        addContentView(input_, input_.getLayoutParams());

        input_.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}

            @Override
            public void afterTextChanged(Editable e) {
                // suppress_ breaks the loop when showKeyboard() seeds the field:
                // setText() fires this watcher, which would report the text
                // back down as if the user had typed it.
                if (suppress_) return;
                nativeOnTextChanged(e.toString(), input_.getSelectionEnd());
            }
        });

        input_.setOnEditorActionListener((v, actionId, ev) -> {
            if (actionId == EditorInfo.IME_ACTION_SEARCH
                    || actionId == EditorInfo.IME_ACTION_DONE
                    || actionId == EditorInfo.IME_ACTION_GO) {
                nativeOnTextCommitted();
                return true;
            }
            return false;
        });

        // Back while the IME is up dismisses the IME rather than the activity,
        // and the native side has to learn that its field lost the keyboard —
        // nothing else tells it.
        input_.setOnKeyListener((v, keyCode, ev) -> {
            if (keyCode == KeyEvent.KEYCODE_BACK && ev.getAction() == KeyEvent.ACTION_UP) {
                nativeOnKeyboardHidden();
            }
            return false;
        });

        // Pushed rather than polled: the native side would otherwise need a JNI
        // round trip every frame to ask whether the keyboard had moved.
        final View root = getWindow().getDecorView();
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                nativeOnImeInset(insets.getInsets(WindowInsets.Type.ime()).bottom);
            }
            // Before API 30 there is no way to ask for the IME inset on its
            // own — getSystemWindowInsets returns one merged rectangle that
            // also contains the bars and the cutout, and subtracting a guess
            // for those would be worse than reporting nothing. The keyboard
            // simply overlaps on those releases, which is what every app did
            // before WindowInsets.Type existed.
            return v.onApplyWindowInsets(insets);
        });
    }

    // ---- up-calls FROM C++ -------------------------------------------------
    //
    // Everything here except getClipboard() is marshalled to the UI thread,
    // because every View and InputMethodManager method below requires it and
    // the caller is the native app thread.

    /** Opens the IME, seeding it with the field's current contents. */
    @SuppressWarnings("unused")
    public void showKeyboard(final String initialText, final int cursorUnits) {
        runOnUiThread(() -> {
            suppress_ = true;
            input_.setText(initialText == null ? "" : initialText);
            int len = input_.getText().length();
            input_.setSelection(cursorUnits < 0 || cursorUnits > len ? len : cursorUnits);
            suppress_ = false;

            input_.requestFocus();
            InputMethodManager imm =
                    (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) imm.showSoftInput(input_, InputMethodManager.SHOW_IMPLICIT);
        });
    }

    @SuppressWarnings("unused")
    public void hideKeyboard() {
        runOnUiThread(() -> {
            InputMethodManager imm =
                    (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) imm.hideSoftInputFromWindow(input_.getWindowToken(), 0);
            input_.clearFocus();
        });
    }

    /**
     * Name of the {@code <meta-data>} an app sets to ask for an HDR window.
     *
     * <pre>{@code
     * <meta-data android:name="io.nava.appshell.HDR" android:value="true" />
     * }</pre>
     */
    private static final String HDR_META = "io.nava.appshell.HDR";

    private static final String TAG = "AppShell";

    /**
     * Asks the window manager for an HDR colour mode, if — and only if — the
     * consumer opted in through the manifest.
     *
     * <p><strong>Opt-in, never inferred.</strong> An HDR window is not free:
     * on many devices it forces the panel into a different, more
     * power-hungry mode for as long as it is up, and an app whose content is
     * ordinary SDR gains nothing for that cost. So the default is off and
     * stays off, and every existing consumer of this library sees no change
     * whatsoever.
     *
     * <p><strong>This must happen in {@code onCreate}</strong>, before the
     * NativeActivity's surface exists. The colour mode is a property of the
     * window, and the set of {@code VkSurfaceFormatKHR} pairs a driver
     * enumerates for a surface can depend on it — asking afterwards means the
     * native side has already chosen a format from a list that did not
     * include the HDR ones.
     *
     * <p>This is a request, not a guarantee. {@code setColorMode} is silent
     * about refusal, and a device may honour it, ignore it, or honour it only
     * while the app is in the foreground. Nothing here reports success;
     * native code must ask the surface what it actually got rather than
     * assume this worked.
     */
    private void maybeRequestHdrColorMode() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        try {
            Bundle meta = getPackageManager().getActivityInfo(
                    getComponentName(), PackageManager.GET_META_DATA).metaData;
            if (meta == null || !meta.getBoolean(HDR_META, false)) {
                Log.i(TAG, "HDR colour mode NOT requested (no " + HDR_META + " meta-data)");
                return;
            }
            getWindow().setColorMode(ActivityInfo.COLOR_MODE_HDR);
            // Logged because it is otherwise UNOBSERVABLE. setColorMode returns
            // nothing and reports no refusal, so without this line there is no
            // way to tell "we never asked" from "we asked and were ignored" --
            // and those want opposite fixes.
            Log.i(TAG, "HDR colour mode requested (COLOR_MODE_HDR)");
        } catch (Exception e) {
            // A missing entry, a renamed component, an OEM that throws from
            // setColorMode: all mean "no HDR window", which is a state the
            // native side already has to handle because the request can be
            // refused silently anyway.
        }
    }

    /**
     * The display this activity is on, without touching the view hierarchy.
     *
     * <p>Deliberately NOT {@code getWindow().getDecorView().getDisplay()}:
     * every caller here arrives on the native app thread, not the UI thread,
     * and {@code getDecorView()} instantiates the decor view if it does not
     * exist yet — a UI-thread operation. It happens to be there by the time
     * native code asks, which makes the bug a race rather than a crash, and
     * that is worse. {@link DisplayManager} is thread-safe by contract.
     */
    private Display activityDisplay() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Display d = getDisplay();          // the display we are actually on
            if (d != null) return d;
        }
        DisplayManager dm = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        return dm == null ? null : dm.getDisplay(Display.DEFAULT_DISPLAY);
    }

    /**
     * How far above SDR white this display can currently go, as a multiplier —
     * {@code 1.0} means no headroom at all (an SDR panel, or one that will not
     * say).
     *
     * <p>Native code needs this and cannot get it: none of this has an NDK
     * equivalent, and the alternative is to hardcode a number and call it
     * headroom. A tone curve rolling highlights toward a guessed peak either
     * crushes detail the panel could have shown or pushes past what it can,
     * and both look like a bad photograph rather than a bad constant.
     *
     * <p><strong>CURRENTLY, not permanently.</strong> On API 34+ this is
     * {@link Display#getHdrSdrRatio()}, which is a live measurement and moves
     * with the brightness slider — SDR white is whatever the system is
     * presently driving it at, so the same panel has a lot of headroom in a
     * dark room and almost none outdoors. A caller that reads this once and
     * keeps it will drift; re-read it when the surface comes back.
     *
     * <p>The pre-34 fallback is {@code desiredMaxLuminance / 203} — the
     * panel's static capability over BT.2408 graphics white. That is an
     * approximation of the same quantity and it is the reason the ratio API
     * is preferred: it assumes SDR white sits at 203 nits, which is exactly
     * the assumption the ratio API exists to stop making. DESIRED rather than
     * maximum luminance, because the maximum is a peak the panel sustains
     * over a small window only, and mapping a whole image to it is how HDR
     * gets a reputation for being painful to look at.
     *
     * <p>Clamped to at least {@code 1.0} so a caller can multiply by it
     * unconditionally, and never throws — an unknown display reports no
     * headroom rather than an error.
     */
    @SuppressWarnings({"unused", "deprecation"})
    public float displayHdrHeadroom() {
        try {
            Display d = activityDisplay();
            if (d == null) return 1.0f;

            // API 34+: the display reports the ratio itself, live.
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE
                    && d.isHdrSdrRatioAvailable()) {
                float r = d.getHdrSdrRatio();
                if (!Float.isNaN(r)) {
                    // A valid answer of exactly 1.0 means "no headroom right
                    // now", which is a real state (SDR mode, or the brightness
                    // already at the top), not a failure to answer.
                    float live = r > 1.0f ? r : 1.0f;
                    Log.i(TAG, "headroom " + live + "x from getHdrSdrRatio (live)");
                    return live;
                }
            }

            Display.HdrCapabilities caps = d.getHdrCapabilities();
            if (caps == null) return 1.0f;
            float desired = caps.getDesiredMaxLuminance();
            if (Float.isNaN(desired) || desired <= 0.0f) return 1.0f;
            float headroom = desired / 203.0f;
            if (headroom < 1.0f) headroom = 1.0f;
            // Which source answered matters when the number looks wrong: the
            // static one cannot track brightness and will read high in daylight.
            Log.i(TAG, "headroom " + headroom + "x from desiredMaxLuminance="
                    + desired + " (static fallback; ratio API unavailable)");
            return headroom;
        } catch (Exception e) {
            return 1.0f;
        }
    }

    /**
     * Root of SHARED storage, e.g. {@code /storage/emulated/0} — a real
     * filesystem path, not a SAF tree URI.
     *
     * <p>Distinct from {@code app_paths::stateDir()}, which is the app's own
     * private directory and is deleted on uninstall. An app that stores
     * documents, music or photos the user considers theirs must not put them
     * there, and must not receive them as {@code content://} URIs either —
     * anything walking a tree with {@code std::filesystem} needs a path.
     *
     * <p>Reaching it requires MANAGE_EXTERNAL_STORAGE (see
     * {@code os/storage_permission.cc}); this only says WHERE, never whether
     * the app may write there.
     */
    @SuppressWarnings("unused")
    public String externalStorageRoot() {
        return android.os.Environment.getExternalStorageDirectory().getAbsolutePath();
    }

    /**
     * Publish an image into the user's shared Pictures collection, and return
     * the {@code content://} URI it landed at (or {@code null} on failure).
     *
     * <p>This exists because under scoped storage there is NO path-based way to
     * do it. From API 29 an app may not create files in a shared collection with
     * ordinary filesystem calls, however correct the path looks: the write fails
     * and the only supported route is a {@link android.content.ContentResolver}
     * insert into {@link MediaStore}. That is Java-only, which is why a native
     * app that merely wants to save a picture has to come up here to do it.
     *
     * <p>The consequence of getting this wrong is not a crash — it is a file
     * written somewhere the user will never look for it.
     *
     * <p>{@code relativeDir} is a sub-path under Pictures, e.g. "ViewMage".
     * IS_PENDING hides the row until the bytes are all written, so a gallery
     * scanning mid-write never shows a torn image.
     *
     * <p>Below API 29 there is no RELATIVE_PATH and no IS_PENDING; the file is
     * written directly (legal there, with WRITE_EXTERNAL_STORAGE) and the media
     * scanner is told about it, which is what makes it appear in the gallery.
     */
    @SuppressWarnings("unused")
    public String publishImage(String displayName, String mimeType,
                               String relativeDir, byte[] data) {
        if (displayName == null || data == null || data.length == 0) return null;
        if (mimeType == null || mimeType.isEmpty()) mimeType = "image/png";
        if (relativeDir == null) relativeDir = "";

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            final ContentResolver cr = getContentResolver();
            final ContentValues values = new ContentValues();
            values.put(MediaStore.Images.Media.DISPLAY_NAME, displayName);
            values.put(MediaStore.Images.Media.MIME_TYPE, mimeType);
            values.put(MediaStore.Images.Media.RELATIVE_PATH,
                       relativeDir.isEmpty() ? "Pictures" : "Pictures/" + relativeDir);
            values.put(MediaStore.Images.Media.IS_PENDING, 1);

            Uri uri = null;
            try {
                uri = cr.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
                if (uri == null) return null;
                try (java.io.OutputStream out = cr.openOutputStream(uri)) {
                    if (out == null) throw new java.io.IOException("no output stream");
                    out.write(data);
                }
                values.clear();
                values.put(MediaStore.Images.Media.IS_PENDING, 0);
                cr.update(uri, values, null, null);
                return uri.toString();
            } catch (Exception e) {
                Log.e(TAG, "publishImage failed", e);
                // Leave no half-written pending row behind: it would be
                // invisible to the gallery and invisible to the user, i.e.
                // unreclaimable space.
                if (uri != null) {
                    try { cr.delete(uri, null, null); } catch (Exception ignored) {}
                }
                return null;
            }
        }

        try {
            final java.io.File dir = new java.io.File(
                android.os.Environment.getExternalStoragePublicDirectory(
                    android.os.Environment.DIRECTORY_PICTURES),
                relativeDir);
            if (!dir.exists() && !dir.mkdirs()) return null;
            final java.io.File file = new java.io.File(dir, displayName);
            try (java.io.FileOutputStream out = new java.io.FileOutputStream(file)) {
                out.write(data);
            }
            MediaScannerConnection.scanFile(this, new String[]{file.getAbsolutePath()},
                                            new String[]{mimeType}, null);
            return Uri.fromFile(file).toString();
        } catch (Exception e) {
            Log.e(TAG, "publishImage (legacy) failed", e);
            return null;
        }
    }

    /**
     * The bytes this activity was launched to open, or {@code null} when it
     * was not launched with any.
     *
     * <p>Native code cannot do this for itself, and the reason is not a
     * missing binding: a file manager launches a viewer with
     * {@code ACTION_VIEW} and a {@code content://} URI, which names a row in
     * some other app's {@link android.content.ContentProvider}. There is no
     * path behind it, so no {@code open()} reaches it, and the permission that
     * lets us read it at all was granted to this Intent rather than to this
     * process. Only a {@link android.content.ContentResolver} can resolve
     * that, and only Java has one.
     *
     * <p>Distinct from {@code externalStorageRoot()} above, which answers the
     * other half of the same problem — that one is for an app given a
     * DIRECTORY to walk with {@code std::filesystem}, this one is for an app
     * handed a single DOCUMENT it may not otherwise reach.
     *
     * <p>Read whole rather than streamed. What arrives this way is one file a
     * user picked in a file manager, and the caller is a native app that wants
     * to decode or parse it — a streaming seam would add a handle to own and a
     * lifetime to get wrong for every consumer, to save nothing on the sizes
     * this is actually used at. A consumer opening multi-gigabyte files should
     * ask for a file descriptor instead, and that is a different method.
     *
     * <p>Returns {@code null}, never throws, for every failure — no data URI,
     * a provider that has gone away, a revoked permission, an I/O error. The
     * native side gets "nothing to show", which is a state an app must handle
     * regardless of the reason.
     */
    @SuppressWarnings("unused")
    public byte[] readIntentData() {
        final Intent intent = getIntent();
        if (intent == null) return null;
        final Uri uri = intent.getData();
        if (uri == null) return null;

        java.io.InputStream in = null;
        try {
            in = getContentResolver().openInputStream(uri);
            if (in == null) return null;
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream(1 << 16);
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        } catch (Throwable t) {
            return null;
        } finally {
            if (in != null) try { in.close(); } catch (java.io.IOException ignored) {}
        }
    }

    /**
     * The intent's data URI as an open file DESCRIPTOR, or -1.
     *
     * <p>The method {@link #readIntentData()}'s documentation points at: a
     * consumer opening a file too large to hold in memory asks for this
     * instead. A video player is exactly that consumer — the recordings that
     * motivated this are hundreds of megabytes to several gigabytes, and
     * reading one into a byte[] to parse its header is not a trade, it is an
     * OutOfMemoryError.
     *
     * <p>The descriptor is DETACHED: ownership passes to the caller, which
     * must close() it. Left attached, the ParcelFileDescriptor's finalizer
     * would close it out from under native code at an unpredictable moment.
     *
     * <p>"r" rather than "rw": a viewer opened on someone's document has no
     * business asking for write access, and many providers refuse it outright.
     *
     * <p>Returns -1, never throws, for every failure — no data URI, a provider
     * that has gone away, a revoked grant, a URI that names no openable file.
     */
    @SuppressWarnings("unused")
    public int openIntentDataFd() {
        final Intent intent = getIntent();
        if (intent == null) return -1;
        final Uri uri = intent.getData();
        if (uri == null) return -1;
        try {
            android.os.ParcelFileDescriptor pfd =
                    getContentResolver().openFileDescriptor(uri, "r");
            if (pfd == null) return -1;
            return pfd.detachFd();
        } catch (Throwable t) {
            return -1;
        }
    }

    /**
     * The intent's data URI as a real filesystem PATH, or null.
     *
     * <p>Only for a {@code file://} URI, which is what a few file managers
     * still send and what {@code adb shell am start -d file://...} produces.
     * A {@code content://} URI has no path behind it and is never guessed at
     * here — {@link #openIntentDataFd()} is the answer for those.
     *
     * <p>Preferred over the descriptor when it is available: a path can be
     * reopened, which a detached fd cannot, and an app that wants to seek
     * around a file across a lifecycle bounce needs that.
     */
    @SuppressWarnings("unused")
    public String getIntentDataPath() {
        final Intent intent = getIntent();
        if (intent == null) return null;
        final Uri uri = intent.getData();
        if (uri == null) return null;
        if (!"file".equals(uri.getScheme())) return null;
        return uri.getPath();
    }

    @SuppressWarnings("unused")
    public void openUrl(final String url) {
        // The native side has already rejected anything that is not plainly an
        // http(s) URL — ACTION_VIEW on an arbitrary scheme can reach any app on
        // the device.
        runOnUiThread(() -> {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
            } catch (Exception ignored) {
            }
        });
    }

    @SuppressWarnings("unused")
    public void setClipboard(final String text) {
        runOnUiThread(() -> {
            ClipboardManager cm =
                    (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            if (cm != null) cm.setPrimaryClip(ClipData.newPlainText("text", text));
        });
    }

    /**
     * Read SYNCHRONOUSLY, unlike everything above: the native caller blocks on
     * the result, because a paste is a user-triggered round trip that has to
     * produce an answer for the frame that asked. ClipboardManager reads are
     * safe off the UI thread.
     */
    @SuppressWarnings("unused")
    public String getClipboard() {
        ClipboardManager cm = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        if (cm == null) return "";
        ClipData clip = cm.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0) return "";
        CharSequence s = clip.getItemAt(0).coerceToText(this);
        return s == null ? "" : s.toString();
    }
}
