#pragma once
// Android implementations of the platform seam (platform.hh):
// assets from the APK, surface from ANativeWindow, wake via ALooper.

#include "platform.hh"

#include <android/asset_manager.h>
#include <android/native_window.h>
#include <android/looper.h>

class AndroidAssetReader : public AssetReader {
public:
    explicit AndroidAssetReader(AAssetManager* mgr) : mgr_(mgr) {}
    bool read(const char* path, std::vector<uint8_t>& out) override;

private:
    AAssetManager* mgr_;
};

class AndroidSurfaceProvider : public SurfaceProvider {
public:
    explicit AndroidSurfaceProvider(ANativeWindow* window) : window_(window) {}
    std::vector<const char*> instance_extensions() const override;
    VkSurfaceKHR create(VkInstance instance) override;
    VkExtent2D   extent() const override;

    // Point this provider at the window the system just handed us.
    //
    // Android destroys the ANativeWindow when the app is backgrounded and
    // gives a DIFFERENT one back on return. Renderer holds the provider by
    // reference and asks it for a surface, so re-pointing the provider is what
    // lets Renderer::recreate_surface() rebuild only the surface and keep the
    // device, the pipelines and every texture — instead of the whole Renderer
    // being destroyed and rebuilt because the pointer inside here was fixed at
    // construction.
    void set_window(ANativeWindow* window) { window_ = window; }

private:
    ANativeWindow* window_;
};

class AndroidFrameWaker : public FrameWaker {
public:
    explicit AndroidFrameWaker(ALooper* looper) : looper_(looper) {}
    void wake() override { ALooper_wake(looper_); }

private:
    ALooper* looper_;
};
