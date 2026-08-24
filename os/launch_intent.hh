#pragma once
#include <android_native_app_glue.h>

#include <cstdint>
#include <string>
#include <vector>

// activity.getIntent().getStringExtra(key).
//
// This is how a pure-NativeActivity app receives an argument at all: it cannot
// get SAF's ACTION_OPEN_DOCUMENT_TREE result back, because onActivityResult
// needs Java (see storage_permission.hh's header comment). Deliverable from a
// terminal with `adb shell am start ... --es <key> <value>`.
//
// Returns `fallback` when the extra is absent or empty. The KEY used to be
// hardcoded to "scan_root" here, which is a music player's word in a file that
// only knows about intents; AndroidHost is told the key now.
std::string read_string_extra(android_app* app, const char* key,
                              const std::string& fallback = {});

// The BYTES the activity was launched to open — activity.readIntentData(),
// which resolves the intent's data URI through a ContentResolver.
//
// The other half of the same question read_string_extra() answers. That one is
// how a NativeActivity app receives an ARGUMENT; this is how it receives a
// DOCUMENT. A file manager opening a viewer sends neither an extra nor a path:
// it sends ACTION_VIEW with a content:// URI naming a row in another app's
// ContentProvider. Nothing native can open that — there is no file behind it,
// and the read permission was granted to the Intent rather than to us — so the
// work happens in AppShellActivity and only the bytes come back.
//
// Returns EMPTY when there was no data URI, when it could not be read, or when
// the consumer's activity does not extend AppShellActivity (the JNI lookup
// fails and the call becomes a no-op, the same quiet degradation every other
// service in activity_bridge.cc has). Empty is a normal state meaning "nothing
// to open", not an error code: what that MEANS is the app's to decide, exactly
// as it is for launchArgument().
std::vector<uint8_t> read_intent_data_bytes(android_app* app);
