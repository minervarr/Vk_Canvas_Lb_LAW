# Third-party inventory and provenance

Status: canonical, 2026-10-31. This is an inventory, not legal advice. Verify
the exact obligations with the copyright holders before distributing a binary.

## Repository license

`Vk_Canvas_Lb_LAW` is AGPLv3 (see `../LICENSE`). Static linking into a consumer
application has license consequences; this document does not interpret them.
`App_shell` is a separate repository with its own license file.

## Submodules and vendored code

| Component | Location | Version / commit | License |
|---|---|---|---|
| `vulkan_font_engine` | `first_party/vulkan_font_engine` | `4f5c477d5a19b87dc7a0d55ef2a776b127d8f88a` | AGPLv3 (its `LICENSE`) |
| FreeType | `vulkan_font_engine/third_party/freetype` | `5336c0d4da22a13dab3389eb153b12672fdf841c` (`VER-2-14-3-71-g5336c0d4`) | FreeType License (FTL) **or** GPLv2+; see `LICENSE.TXT` and `docs/FTL.TXT` |
| msdfgen | `vulkan_font_engine/third_party/msdfgen` | `e06c7eaaaa071445ec77a1d73f942889f796b70b` (`v1.13+new-skia-api-3-ge06c7ea`) | MIT (`LICENSE.txt`) |
| `img_decode_kit` | `first_party/img_decode_kit` | checked in (no submodule) | no license file in the directory; verify |
| stb_image | `img_decode_kit/core/stb_image.h` | v2.30 | public domain **or** MIT (dual, in-header) |
| stb_image_write | `core/capture/stb_image_write.h` | v1.16 | public domain **or** MIT (dual, in-header) |
| libjpeg-turbo | optional, supplied by the consumer as `turbojpeg-static` | not vendored | IJG / BSD-style / zlib; check the consumer's copy |

`img_decode_kit` works without libjpeg-turbo (stb_image covers JPEG); when a
consumer provides the target, its license is the consumer's responsibility.

## Generated or build-time material

| Material | Source | Provenance |
|---|---|---|
| Wayland `xdg-shell` and `idle-inhibit` stubs | system `wayland-protocols` XML, `wayland-scanner` | generated at build time, not committed |
| SPIR-V shaders | `shaders_src/*.slang` and the font engine's shared shaders | compiled per platform by the consuming build; `slangc` from the Vulkan SDK |
| `ui_min_text_size.gen.h` | `vulkan_font_engine/tools/min_text_size` + the app's fonts | per-application build artifact; the dev test harness uses a stub |
| `CompositePush` check | `cmake/check_composite_pc.cmake` | compares `renderer.cc` and the shared shader's push block at build time |

## Fonts and test assets

No font files are checked into `Vk_Canvas_Lb_LAW`. The probes expect an
application-supplied face under `assets/fonts/...`; when it is absent they exit
77. The dev test harness uses only a numeric stub for the readable-size floor.
Any font shipped by a consumer has its own license (often OFL); keep it with the
app's asset licenses.

## Workspace-only material

`minervarrRepos.json` at the workspace root is a GitHub API dump used to locate
sibling repositories. The workspace root is not a git repository. It is not part
of the library or its build.

## Updating this file

When a submodule advances, or a new vendored file is added, update the commit and
license row. If a license is unknown, write `verify` rather than guessing.
