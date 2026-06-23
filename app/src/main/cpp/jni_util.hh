#pragma once
#include <jni.h>
#include <android_native_app_glue.h>
#include <android/log.h>

// ---------------------------------------------------------------------------
// vce = vulkan_canvas_engine. Generic JNI substrate for a pure-NativeActivity
// app (no Java UI). Reusable by any NativeActivity that needs to up-call into
// the framework from native code.
//
// IMPORTANT JNI rules honoured here:
//  * The android_main glue thread is attached to the JVM ONCE and left
//    attached for the lifetime of the thread. Attaching/detaching on every
//    call is unnecessary and detaching a thread that later does more JNI work
//    is a common source of crashes.
//  * Every JNI call that can raise is followed by an exception check. A
//    pending (uncleared) exception makes the *next* JNI call abort the whole
//    process -- which manifests as an instant "black screen".
// ---------------------------------------------------------------------------

namespace vce {
namespace platform {
namespace jni {

// Attach the calling (glue) thread once and reuse the env. Never detached.
inline JNIEnv* env_for(android_app* app) {
    JNIEnv* env = nullptr;
    JavaVM* vm  = app->activity->vm;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "vce.jni", "AttachCurrentThread failed");
        return nullptr;
    }
    return env;
}

// Clears any pending exception so it cannot poison subsequent JNI calls.
// Returns true if an exception was pending (and was cleared).
inline bool check_exc(JNIEnv* env, const char* where) {
    if (env->ExceptionCheck()) {
        __android_log_print(ANDROID_LOG_ERROR, "vce.jni", "JNI exception at %s", where);
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }
    return false;
}

}  // namespace jni
}  // namespace platform
}  // namespace vce
