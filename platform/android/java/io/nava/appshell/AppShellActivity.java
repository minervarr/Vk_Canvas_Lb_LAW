package io.nava.appshell;

import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
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
