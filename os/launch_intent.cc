#include "launch_intent.hh"

#include <jni.h>

#include "jni_util.hh"  // framework/vk_canvas/platform/android/jni_util.hh

using vce::platform::jni::env_for;
using vce::platform::jni::check_exc;

std::string read_string_extra(android_app* app, const char* key,
                              const std::string& fallback) {
    JNIEnv* env = env_for(app);
    if (!env) return fallback;

    jobject activity = app->activity->clazz;
    jclass  act_cls  = env->GetObjectClass(activity);

    // activity.getIntent()
    jmethodID get_intent = env->GetMethodID(act_cls, "getIntent", "()Landroid/content/Intent;");
    if (check_exc(env, "GetMethodID(getIntent)") || !get_intent) {
        env->DeleteLocalRef(act_cls);
        return fallback;
    }
    jobject intent = env->CallObjectMethod(activity, get_intent);
    if (check_exc(env, "getIntent") || !intent) {
        env->DeleteLocalRef(act_cls);
        return fallback;
    }

    // intent.getStringExtra(key)
    jclass    intent_cls = env->GetObjectClass(intent);
    jmethodID get_extra  = env->GetMethodID(intent_cls, "getStringExtra",
                                            "(Ljava/lang/String;)Ljava/lang/String;");
    if (check_exc(env, "GetMethodID(getStringExtra)") || !get_extra) {
        env->DeleteLocalRef(intent_cls);
        env->DeleteLocalRef(intent);
        env->DeleteLocalRef(act_cls);
        return fallback;
    }
    jstring jkey  = env->NewStringUTF(key);
    jstring value = static_cast<jstring>(env->CallObjectMethod(intent, get_extra, jkey));
    env->DeleteLocalRef(jkey);
    bool exc = check_exc(env, "getStringExtra");

    std::string result = fallback;
    if (!exc && value) {
        const char* chars = env->GetStringUTFChars(value, nullptr);
        if (chars && chars[0] != '\0') result = chars;
        env->ReleaseStringUTFChars(value, chars);
        env->DeleteLocalRef(value);
    }

    env->DeleteLocalRef(intent_cls);
    env->DeleteLocalRef(intent);
    env->DeleteLocalRef(act_cls);
    return result;
}

std::vector<uint8_t> read_intent_data_bytes(android_app* app) {
    JNIEnv* env = env_for(app);
    if (!env) return {};

    jobject activity = app->activity->clazz;
    jclass  act_cls  = env->GetObjectClass(activity);

    // Declared on AppShellActivity. A consumer whose activity does not extend
    // it simply has no such method: the lookup fails, the exception is
    // cleared, and this returns empty — see the header.
    jmethodID read = env->GetMethodID(act_cls, "readIntentData", "()[B");
    if (check_exc(env, "GetMethodID(readIntentData)") || !read) {
        env->DeleteLocalRef(act_cls);
        return {};
    }

    auto bytes = static_cast<jbyteArray>(env->CallObjectMethod(activity, read));
    bool exc = check_exc(env, "readIntentData");
    env->DeleteLocalRef(act_cls);
    if (exc || !bytes) return {};

    const jsize n = env->GetArrayLength(bytes);
    std::vector<uint8_t> out((size_t)(n < 0 ? 0 : n));
    if (n > 0) {
        // GetByteArrayRegion copies into our storage directly; no Get/Release
        // pair to leak if something between them throws.
        env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(out.data()));
        if (check_exc(env, "GetByteArrayRegion")) out.clear();
    }
    env->DeleteLocalRef(bytes);
    return out;
}
