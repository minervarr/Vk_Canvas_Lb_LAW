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
