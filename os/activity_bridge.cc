#include "activity_bridge.hh"

#include <jni.h>
#include <mutex>
#include <vector>

#include "jni_util.hh"      // framework/vk_canvas/platform/android/jni_util.hh
#include "utf16_utf8.hh"

using vce::platform::jni::env_for;
using vce::platform::jni::check_exc;

namespace {

android_app* g_app = nullptr;
void (*g_wake)() = nullptr;

// Called at the END of every JNI entry point, after the lock is released.
// Outside the lock deliberately: the waker writes a file descriptor, and doing
// that while holding a mutex the app thread wants is how a wake-up turns into
// a stall.
void ring() { if (g_wake) g_wake(); }

// The one slot every down-call writes and drain() empties. See the header for
// why text is a slot and the rest are latches.
struct Pending {
    std::mutex  mu;
    bool        hasText = false;
    std::string text;
    size_t      cursorByte = 0;
    bool        committed = false;
    bool        dismissed = false;

    // Under the SAME lock, not in a bare int beside it: written by the UI
    // thread and read by the app thread, which is a data race whatever the
    // type, and a stale read here misplaces the focused field for a frame.
    bool imeInsetChanged = false;
    int  imeBottom       = 0;
};
Pending g_pending;

// ── Calling methods on the activity object ──────────────────────────────────
//
// GetObjectClass returns the RUNTIME class, i.e. the consumer's subclass, and
// GetMethodID walks up from there — so methods declared on AppShellActivity
// resolve without this file knowing the subclass exists.
//
// Every helper clears a pending exception before returning. That is not
// tidiness: a pending exception makes the NEXT JNI call abort the process, so
// swallowing it here is what turns "this method was missing" into a failed
// call rather than a crash somewhere unrelated.

bool call_void(const char* name, const char* sig, jvalue* args) {
    if (!g_app) return false;
    JNIEnv* env = env_for(g_app);
    if (!env) return false;
    jobject act = g_app->activity->clazz;
    jclass  cls = env->GetObjectClass(act);
    jmethodID m = env->GetMethodID(cls, name, sig);
    if (!m) {
        check_exc(env, name);
        env->DeleteLocalRef(cls);
        return false;
    }
    env->CallVoidMethodA(act, m, args);
    bool bad = check_exc(env, name);
    env->DeleteLocalRef(cls);
    return !bad;
}

jstring to_jstring(JNIEnv* env, const std::string& utf8) {
    // NOT NewStringUTF: it speaks modified UTF-8 and would mangle anything
    // outside the BMP. See utf16_utf8.hh.
    std::vector<uint16_t> u16 = utf16::to_utf16(utf8);
    return env->NewString(reinterpret_cast<const jchar*>(u16.data()),
                          (jsize)u16.size());
}

std::string call_string(const char* name) {
    std::string out;
    if (!g_app) return out;
    JNIEnv* env = env_for(g_app);
    if (!env) return out;
    jobject act = g_app->activity->clazz;
    jclass  cls = env->GetObjectClass(act);
    jmethodID m = env->GetMethodID(cls, name, "()Ljava/lang/String;");
    if (!m) {
        check_exc(env, name);
        env->DeleteLocalRef(cls);
        return out;
    }
    jstring s = (jstring)env->CallObjectMethod(act, m);
    if (!check_exc(env, name) && s) {
        const jchar* u = env->GetStringChars(s, nullptr);   // NOT GetStringUTFChars
        jsize len = env->GetStringLength(s);
        out = utf16::to_utf8(reinterpret_cast<const uint16_t*>(u), (size_t)len);
        env->ReleaseStringChars(s, u);
    }
    if (s) env->DeleteLocalRef(s);
    env->DeleteLocalRef(cls);
    return out;
}

float call_float(const char* name, float fallback) {
    if (!g_app) return fallback;
    JNIEnv* env = env_for(g_app);
    if (!env) return fallback;
    jobject act = g_app->activity->clazz;
    jclass  cls = env->GetObjectClass(act);
    jmethodID m = env->GetMethodID(cls, name, "()F");
    if (!m) {
        // A consumer whose Activity does not extend AppShellActivity gets here.
        // Not an error: it means "this app never opted in", and the fallback is
        // the answer for that.
        check_exc(env, name);
        env->DeleteLocalRef(cls);
        return fallback;
    }
    jfloat v = env->CallFloatMethod(act, m);
    bool bad = check_exc(env, name);
    env->DeleteLocalRef(cls);
    return bad ? fallback : (float)v;
}

bool call_with_string(const char* name, const std::string& arg) {
    if (!g_app) return false;
    JNIEnv* env = env_for(g_app);
    if (!env) return false;
    jstring js = to_jstring(env, arg);
    jvalue v;
    v.l = js;
    bool ok = call_void(name, "(Ljava/lang/String;)V", &v);
    env->DeleteLocalRef(js);
    return ok;
}

} // namespace

namespace activity {

void set_app(android_app* app) { g_app = app; }
void set_waker(void (*wake)()) { g_wake = wake; }

void show_keyboard(const std::string& text, size_t cursorByte) {
    if (!g_app) return;
    JNIEnv* env = env_for(g_app);
    if (!env) return;

    // Byte offset -> UTF-16 code-unit index, which is what setSelection() takes.
    // Converting the prefix is the whole calculation: its unit count IS the
    // index. Clamped first so a stale cursor from a shorter buffer cannot make
    // substr throw.
    if (cursorByte > text.size()) cursorByte = text.size();
    const int cursorUnits = (int)utf16::to_utf16(text.substr(0, cursorByte)).size();

    jstring js = to_jstring(env, text);
    jvalue args[2];
    args[0].l = js;
    args[1].i = cursorUnits;
    call_void("showKeyboard", "(Ljava/lang/String;I)V", args);
    env->DeleteLocalRef(js);
}

void hide_keyboard() { call_void("hideKeyboard", "()V", nullptr); }

void set_clipboard(const std::string& utf8) { call_with_string("setClipboard", utf8); }
std::string get_clipboard() { return call_string("getClipboard"); }

bool open_url(const std::string& url) { return call_with_string("openUrl", url); }

std::string external_storage_root() { return call_string("externalStorageRoot"); }

std::string publish_image(const std::string& display_name,
                          const std::string& mime_type,
                          const std::string& relative_dir,
                          const uint8_t* data, size_t bytes) {
    if (!g_app || !data || bytes == 0) return {};
    JNIEnv* env = env_for(g_app);
    if (!env) return {};

    jbyteArray arr = env->NewByteArray((jsize)bytes);
    if (!arr) {
        // A failed allocation leaves a pending exception that would abort the
        // next JNI call made anywhere, not just here.
        env->ExceptionClear();
        return {};
    }
    env->SetByteArrayRegion(arr, 0, (jsize)bytes,
                            reinterpret_cast<const jbyte*>(data));

    jstring jname = to_jstring(env, display_name);
    jstring jmime = to_jstring(env, mime_type);
    jstring jdir  = to_jstring(env, relative_dir);

    std::string out;
    jobject act = g_app->activity->clazz;
    {
        jclass cls = env->GetObjectClass(act);
        jmethodID mid = env->GetMethodID(
            cls, "publishImage",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[B)"
            "Ljava/lang/String;");
        if (mid) {
            jstring res = (jstring)env->CallObjectMethod(act, mid, jname, jmime,
                                                         jdir, arr);
            if (!check_exc(env, "publishImage") && res) {
                // GetStringChars, NOT GetStringUTFChars -- the latter is
                // modified UTF-8 and mangles anything outside the BMP.
                const jchar* u = env->GetStringChars(res, nullptr);
                jsize len = env->GetStringLength(res);
                out = utf16::to_utf8(reinterpret_cast<const uint16_t*>(u),
                                     (size_t)len);
                env->ReleaseStringChars(res, u);
            }
            if (res) env->DeleteLocalRef(res);
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(cls);
    }

    env->DeleteLocalRef(jdir);
    env->DeleteLocalRef(jmime);
    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(arr);
    return out;
}

float display_hdr_headroom() { return call_float("displayHdrHeadroom", 1.0f); }

bool drain(Update& out) {
    std::lock_guard<std::mutex> lock(g_pending.mu);
    if (!g_pending.hasText && !g_pending.committed &&
        !g_pending.dismissed && !g_pending.imeInsetChanged)
        return false;

    out.hasText = g_pending.hasText;
    if (out.hasText) {
        out.text       = std::move(g_pending.text);
        out.cursorByte = g_pending.cursorByte;
        g_pending.text.clear();
    }
    out.committed       = g_pending.committed;
    out.dismissed       = g_pending.dismissed;
    out.imeInsetChanged = g_pending.imeInsetChanged;
    if (out.imeInsetChanged) out.imeBottom = g_pending.imeBottom;

    g_pending.hasText         = false;
    g_pending.committed       = false;
    g_pending.dismissed       = false;
    g_pending.imeInsetChanged = false;
    return true;
}

} // namespace activity

// ── JNI entry points ────────────────────────────────────────────────────────
//
// The symbol names encode io.nava.appshell.AppShellActivity, which is the class
// that DECLARES these natives — so they resolve for any subclass a consumer
// writes, and no consumer ever writes JNI.
//
// All of these run on the Android UI thread. They touch nothing but the locked
// slot above; the app thread collects it from pump().

extern "C" {

JNIEXPORT void JNICALL
Java_io_nava_appshell_AppShellActivity_nativeOnTextChanged(
    JNIEnv* env, jclass, jstring text, jint cursorUnits) {
    if (!text) return;
    const jchar* u = env->GetStringChars(text, nullptr);
    jsize len = env->GetStringLength(text);
    size_t cursorByte = 0;
    std::string utf8 = utf16::to_utf8(reinterpret_cast<const uint16_t*>(u),
                                      (size_t)len, (size_t)cursorUnits, &cursorByte);
    env->ReleaseStringChars(text, u);

    {
        std::lock_guard<std::mutex> lock(g_pending.mu);
        g_pending.hasText    = true;
        g_pending.text       = std::move(utf8);
        g_pending.cursorByte = cursorByte;
    }
    ring();
}

JNIEXPORT void JNICALL
Java_io_nava_appshell_AppShellActivity_nativeOnTextCommitted(JNIEnv*, jclass) {
    {
        std::lock_guard<std::mutex> lock(g_pending.mu);
        g_pending.committed = true;
    }
    ring();
}

JNIEXPORT void JNICALL
Java_io_nava_appshell_AppShellActivity_nativeOnKeyboardHidden(JNIEnv*, jclass) {
    {
        std::lock_guard<std::mutex> lock(g_pending.mu);
        g_pending.dismissed = true;
    }
    ring();
}

JNIEXPORT void JNICALL
Java_io_nava_appshell_AppShellActivity_nativeOnImeInset(
    JNIEnv*, jclass, jint bottom) {
    {
        std::lock_guard<std::mutex> lock(g_pending.mu);
        g_pending.imeInsetChanged = true;
        g_pending.imeBottom       = bottom;
    }
    ring();
}

} // extern "C"
