#pragma once
#include <jni.h>
#include <android_native_app_glue.h>

#include "jni_util.hh"  // reuse vce::platform::jni::env_for() + check_exc()

// ---------------------------------------------------------------------------
// vce = vulkan_canvas_engine. Fullscreen / immersive helper for a pure-
// NativeActivity app (no Java UI).
//
// Controls the status bar and navigation bar by calling
//   getWindow().getDecorView().setSystemUiVisibility(flags)
// via JNI. Targets the legacy setSystemUiVisibility flags (min API 16,
// deprecated in 30 but still functional; WindowInsetsController is API 30+).
//
// The system clears these flags on focus changes (e.g. a permission dialog),
// so callers must re-apply on every APP_CMD_GAINED_FOCUS.
//
// Follows the same JNI rules as jni_util.hh: every call that can raise is
// followed by an exception check, and local refs are released.
// ---------------------------------------------------------------------------

namespace vce {
namespace platform {

// ── Immersive mode selection ───────────────────────────────────────────────
//
// Choose how much screen real-estate the app claims.
//
//   kNone              System default — both bars visible.
//
//   kStatusBar         Status bar hidden. Nav bar stays. Window sized to
//                      stop at the nav bar — no inset math needed. Status
//                      bar re-appears on any touch (no IMMERSIVE flag).
//
//   kStatusBarSticky   Same as kStatusBar, but a top-edge swipe shows a
//                      translucent status bar momentarily and it auto-hides
//                      (IMMERSIVE_STICKY).
//
//   kFullImmersive     Both bars hidden with IMMERSIVE_STICKY. Edge swipe
//                      reveals translucent bars momentarily. Window covers
//                      the full display; callers must inset their UI by the
//                      nav-bar height (use query_nav_bar_height).
//
//   kFullImmersiveNonSticky
//                      Both bars hidden with IMMERSIVE (non-sticky). An edge
//                      swipe brings bars back and they stay until re-hidden
//                      on the next APP_CMD_GAINED_FOCUS.
//
enum class ImmersiveMode {
    kNone,
    kStatusBar,
    kStatusBarSticky,
    kFullImmersive,
    kFullImmersiveNonSticky,
};

// View.SYSTEM_UI_FLAG_* constants (stable public API, unchanged since API 19).
namespace flags {
    constexpr jint HIDE_NAVIGATION          = 0x0002;
    constexpr jint FULLSCREEN               = 0x0004;
    constexpr jint LAYOUT_STABLE            = 0x0100;
    constexpr jint LAYOUT_HIDE_NAVIGATION   = 0x0200;
    constexpr jint LAYOUT_FULLSCREEN        = 0x0400;
    constexpr jint IMMERSIVE                = 0x0800;
    constexpr jint IMMERSIVE_STICKY         = 0x1000;
}

inline jint flags_for(ImmersiveMode mode) {
    using namespace flags;
    switch (mode) {
        case ImmersiveMode::kNone:
            return 0;
        case ImmersiveMode::kStatusBar:
            return LAYOUT_STABLE | FULLSCREEN;
        case ImmersiveMode::kStatusBarSticky:
            return LAYOUT_STABLE | FULLSCREEN | IMMERSIVE_STICKY;
        case ImmersiveMode::kFullImmersive:
            return LAYOUT_STABLE | LAYOUT_FULLSCREEN | LAYOUT_HIDE_NAVIGATION
                 | FULLSCREEN    | HIDE_NAVIGATION   | IMMERSIVE_STICKY;
        case ImmersiveMode::kFullImmersiveNonSticky:
            return LAYOUT_STABLE | LAYOUT_FULLSCREEN | LAYOUT_HIDE_NAVIGATION
                 | FULLSCREEN    | HIDE_NAVIGATION   | IMMERSIVE;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Apply the given immersive mode. Must be called:
//   (1) at startup (e.g. in APP_CMD_INIT_WINDOW), and
//   (2) on every APP_CMD_GAINED_FOCUS (system clears flags on focus loss).
// Passing kNone restores the system default.
// ---------------------------------------------------------------------------
inline void enable_immersive(android_app* app, ImmersiveMode mode) {
    jint flag_bits = flags_for(mode);

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

    env->CallVoidMethod(decor, set_vis, flag_bits);
    jni::check_exc(env, "setSystemUiVisibility");

    env->DeleteLocalRef(view_cls);
    env->DeleteLocalRef(decor);
    env->DeleteLocalRef(win_cls);
    env->DeleteLocalRef(window);
    env->DeleteLocalRef(act_cls);
}

// ---------------------------------------------------------------------------
// Query the navigation-bar height in physical pixels via
//   Resources.getIdentifier("navigation_bar_height", "dimen", "android")
//   Resources.getDimensionPixelSize(id)
// Returns 0 on failure (gesture-nav devices, or JNI error).
// Useful when using kFullImmersive/kFullImmersiveNonSticky to inset the UI.
// ---------------------------------------------------------------------------
inline int query_nav_bar_height(android_app* app) {
    JNIEnv* env = jni::env_for(app);
    if (!env) return 0;

    jobject activity = app->activity->clazz;
    jclass  act_cls  = env->GetObjectClass(activity);

    // activity.getResources()
    jmethodID get_res = env->GetMethodID(act_cls, "getResources",
                                         "()Landroid/content/res/Resources;");
    if (jni::check_exc(env, "GetMethodID(getResources)") || !get_res) {
        env->DeleteLocalRef(act_cls);
        return 0;
    }
    jobject resources = env->CallObjectMethod(activity, get_res);
    if (jni::check_exc(env, "getResources") || !resources) {
        env->DeleteLocalRef(act_cls);
        return 0;
    }

    jclass res_cls = env->GetObjectClass(resources);

    // resources.getIdentifier("navigation_bar_height", "dimen", "android")
    jmethodID get_id = env->GetMethodID(res_cls, "getIdentifier",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I");
    if (jni::check_exc(env, "GetMethodID(getIdentifier)") || !get_id) {
        env->DeleteLocalRef(res_cls);
        env->DeleteLocalRef(resources);
        env->DeleteLocalRef(act_cls);
        return 0;
    }

    jstring name = env->NewStringUTF("navigation_bar_height");
    jstring type = env->NewStringUTF("dimen");
    jstring pkg  = env->NewStringUTF("android");
    jint    rid  = env->CallIntMethod(resources, get_id, name, type, pkg);
    jni::check_exc(env, "getIdentifier");
    env->DeleteLocalRef(pkg);
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(name);

    int result = 0;
    if (rid > 0) {
        // resources.getDimensionPixelSize(rid)
        jmethodID get_dim = env->GetMethodID(res_cls, "getDimensionPixelSize", "(I)I");
        if (!jni::check_exc(env, "GetMethodID(getDimensionPixelSize)") && get_dim) {
            result = static_cast<int>(env->CallIntMethod(resources, get_dim, rid));
            jni::check_exc(env, "getDimensionPixelSize");
        }
    }

    env->DeleteLocalRef(res_cls);
    env->DeleteLocalRef(resources);
    env->DeleteLocalRef(act_cls);
    return result;
}

}  // namespace platform
}  // namespace vce
