# app_shell

The application shell: the `Host`/`AppView` seam and its three real
implementations (Win32, Wayland, Android), plus the small portable modules
every desktop-and-Android application needs — `app_paths`, `ui_metrics`,
`ui_orientation`, `layout_rect`, `color`.

Framework direction and current phase: [`../docs/ROADMAP.md`](../docs/ROADMAP.md)
(or the [GitHub copy](https://github.com/minervarr/Vk_Canvas_Lb_LAW/blob/main/docs/ROADMAP.md)).

`vk_canvas` makes *drawing* portable. This makes the *program* portable.

Read `CLAUDE.md` for the guided tour, or `docs/app_shell.tex` (compile with
`pdflatex app_shell.tex`, twice, for the table of contents) for the full
reference manual.

Part of the `Vk_Canvas_Lb_LAW` repository; first extracted from, and still
primarily developed against, Matrix Player.

## Committing

Use the framework's `./git_wrapper` from the repository root — never plain
`git commit`/`git push`. This is no longer a separate repository, so there is no
app_shell-first ordering rule. See `../USAGE_gitWrapper.md`.
