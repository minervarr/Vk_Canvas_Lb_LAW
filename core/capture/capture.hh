#pragma once
// Generic headless UI-capture runner. An app supplies a list of Scenarios —
// each a name-path plus a function that draws one UI state into a Renderer —
// and capture_main() renders every one off-screen (VK_EXT_headless_surface,
// no window, no compositor) and writes a lossless PNG per scenario, under a
// directory tree that mirrors the scenario paths.
//
// The nomenclature is the caller's: use "NN-label" segments and '/' for
// parent/child (e.g. "20-recording/10-warn") — capture_main creates the
// directories and appends ".png". Deterministic: same states -> same pixels.
//
// This is dev tooling; gate its target on a Debug build. See CLAUDE.md.

#include <functional>
#include <string>
#include <vector>

class Renderer;

namespace vkc {

struct Scenario {
    // Path with no extension, '/'-separated for hierarchy, e.g.
    // "40-result/00-transcript". Becomes "<out>/40-result/00-transcript.png".
    std::string path;
    // Draws exactly one frame of this state: build a Canvas, then call
    // renderer.draw(...). Called once, after CaptureConfig::init.
    std::function<void(Renderer&)> render;
};

struct CaptureConfig {
    // Runs once on the freshly-constructed Renderer before any scenario —
    // e.g. upload the MSDF atlas. May be empty.
    std::function<void(Renderer&)> init;
    // Default output directory when --out is not given.
    std::string default_out = "ui-flow";
};

// Parses argv, resolves the capture frame, renders every scenario headlessly
// and writes the PNG tree. Returns a process exit code (0 = success).
//
// Flags:
//   --out DIR       output root (default cfg.default_out)
//   --frame WxH     logical frame size; default = primary monitor's mode
//   --scale N       supersample multiplier 1..8 (output = frame*N), default 1
//   --only SUBSTR   only scenarios whose path contains SUBSTR
//   --list          print scenario paths and exit
int capture_main(int argc, char** argv,
                 const std::vector<Scenario>& scenarios,
                 const CaptureConfig& cfg);

} // namespace vkc
