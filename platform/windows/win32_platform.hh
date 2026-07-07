#pragma once
// Win32 implementations of the platform seam (platform.hh):
// assets from an exe-relative directory, surface from an HWND.

#include "platform.hh"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>

// Reads assets from <exe dir>/assets/, e.g. "shaders/tiling.spv".
class FileAssetReader : public AssetReader {
public:
    FileAssetReader();  // roots itself at the running executable's directory
    bool read(const char* path, std::vector<uint8_t>& out) override;

private:
    std::string root_;
};

class Win32SurfaceProvider : public SurfaceProvider {
public:
    explicit Win32SurfaceProvider(HWND hwnd) : hwnd_(hwnd) {}
    std::vector<const char*> instance_extensions() const override;
    VkSurfaceKHR create(VkInstance instance) override;
    VkExtent2D   extent() const override;

private:
    HWND hwnd_;
};
