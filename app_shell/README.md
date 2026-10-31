# app_shell

The application shell: the `Host`/`AppView` seam and its three real
implementations (Win32, Wayland, Android), plus the small portable modules
every desktop-and-Android application needs — `app_paths`, `ui_metrics`,
`ui_orientation`, `layout_rect`, `color`.

Framework direction and current phase: [Vk_Canvas roadmaps](https://github.com/minervarr/Vk_Canvas_Lb_LAW/blob/main/docs/ROADMAP.md).

`vk_canvas` makes *drawing* portable. This makes the *program* portable.

Read `CLAUDE.md` for the guided tour, or `docs/app_shell.tex` (compile with
`pdflatex app_shell.tex`, twice, for the table of contents) for the full
reference manual.

First extracted from, and still primarily developed against, Matrix Player.

## Committing

Use `git_wrapper` (`./git_wrapper` on Linux, `git_wrapper.exe` on Windows) —
never plain `git commit`/`git push`. Forces author/committer identity to
`nava <nava@noreply.com>` and strips stray `Co-Authored-By:`/"Generated with"
trailers. See `USAGE_gitWrapper.md`.
