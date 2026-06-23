#pragma once
#include <jni.h>
#include <android_native_app_glue.h>

#include "jni_util.hh"  // reuse vce::platform::jni::env_for() + check_exc()

// ---------------------------------------------------------------------------
// vce = vulkan_canvas_engine. Fullscreen / immersive helper for a pure-
// NativeActivity app (no Java UI).
//
// Hides the status bar and navigation bar by calling
//   getWindow().getDecorView().setSystemUiVisibility(flags)
// via JNI. Targets the legacy setSystemUiVisibility flags (min API 26;
// WindowInsetsController is API 30+).
//
// Behavior: non-sticky immersive (SYSTEM_UI_FLAG_IMMERSIVE) — both bars hidden;
// an edge swipe brings them back until re-hidden. The system clears these flags
// on focus changes (e.g. a permission dialog), so this must be re-applied when
// focus returns (e.g. on APP_CMD_GAINED_FOCUS).
//
// Follows the same JNI rules as jni_util.hh: every call that can raise is
// followed by an exception check, and local refs are released.
// ---------------------------------------------------------------------------

namespace vce {
namespace platform {

// Combined View.SYSTEM_UI_FLAG_* values (stable public API constants):
//   LAYOUT_STABLE          0x100
//   LAYOUT_HIDE_NAVIGATION 0x200
// We only hide the status bar (FULLSCREEN) and leave the navigation bar visible.
// This ensures the window bounds are sized to stop exactly at the navigation bar,
// preventing the UI from overlapping underneath it.
static constexpr jint kImmersiveFlags = 0x100 | 0x4;

inline void enable_immersive(android_app* app) {
    JNIEnv* env = jni::env_for(app);
    if (!env) return;

    jobject activity = app->activity->clazz;
    jclass  act_cls  = env->GetObjectClass(activity);

    jmethodID get_window = env->GetMethodID(act_cls, "getWindow",
                                            "()Landroid/view/Window;");
    if (jni::check_exc(env, "GetMethodID(getWindow)") || !get_window) {
        env->DeleteLocalRef(act_cls);
        return;
    }
    jobject window = env->CallObjectMethod(activity, get_window);
    if (jni::check_exc(env, "getWindow") || !window) {
        env->DeleteLocalRef(act_cls);
        return;
    }

    jclass    win_cls    = env->GetObjectClass(window);
    jmethodID get_decor  = env->GetMethodID(win_cls, "getDecorView",
                                            "()Landroid/view/View;");
    if (jni::check_exc(env, "GetMethodID(getDecorView)") || !get_decor) {
        env->DeleteLocalRef(win_cls);
        env->DeleteLocalRef(window);
        env->DeleteLocalRef(act_cls);
        return;
    }
    jobject decor = env->CallObjectMethod(window, get_decor);
    if (jni::check_exc(env, "getDecorView") || !decor) {
        env->DeleteLocalRef(win_cls);
        env->DeleteLocalRef(window);
        env->DeleteLocalRef(act_cls);
        return;
    }

    jclass    view_cls = env->GetObjectClass(decor);
    jmethodID set_vis  = env->GetMethodID(view_cls, "setSystemUiVisibility", "(I)V");
    if (jni::check_exc(env, "GetMethodID(setSystemUiVisibility)") || !set_vis) {
        env->DeleteLocalRef(view_cls);
        env->DeleteLocalRef(decor);
        env->DeleteLocalRef(win_cls);
        env->DeleteLocalRef(window);
        env->DeleteLocalRef(act_cls);
        return;
    }

    env->CallVoidMethod(decor, set_vis, kImmersiveFlags);
    jni::check_exc(env, "setSystemUiVisibility");

    env->DeleteLocalRef(view_cls);
    env->DeleteLocalRef(decor);
    env->DeleteLocalRef(win_cls);
    env->DeleteLocalRef(window);
    env->DeleteLocalRef(act_cls);
}

}  // namespace platform
}  // namespace vce
