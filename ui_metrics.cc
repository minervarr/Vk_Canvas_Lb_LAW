// The type-scale maths moved to the engine's base layer
// (Vk_Canvas_Lb_LAW/core/layout/type_scale.cc) so App_shell depends on base
// headers only and no longer needs a build-generated legibility header.
//
// This translation unit remains in APP_SHELL_SOURCES deliberately: it keeps
// the target's source list stable for consumers and is a place to put any
// future app-specific metrics that need a .cc rather than a header-only
// function. It intentionally contains no definitions of its own.
#include "ui_metrics.hh"
