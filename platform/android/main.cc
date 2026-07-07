#include <android_native_app_glue.h>
#include "app.hh"

void android_main(android_app* state) {
    App app(state);
    app.run();
}
