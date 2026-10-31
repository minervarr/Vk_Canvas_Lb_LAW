#pragma once

// The application's entry point, as app_shell's platform bootstraps see it.
//
// Each desktop host owns the real one — main() on Wayland, WinMain() on Win32 —
// because what has to happen before an app exists is the platform's business:
// DPI awareness, the crash handler, the log file, COM, the timer resolution.
// When that is done, the bootstrap calls this, and the application takes over.
//
// The app DEFINES it; app_shell only declares and calls it. Deliberately a
// plain free function rather than a class to derive from — the app already has
// its own shape, and inheritance here would only dictate one.
//
// It TAKES argc/argv, which it did not at first. The Wayland bootstrap was a
// bare `int main()`, so the command line was simply unreachable from an app
// built on this — and the first consumer worked around it by reading
// environment variables instead, which is a worse command line that no `--help`
// can describe. On Windows they come from __argc/__argv, since WinMain is
// handed an unparsed LPSTR.
//
// An app that wants neither ignores both parameters; nothing here requires an
// app to have a command line.
//
// Android passes argc=0 and argv=nullptr: an app started by an Intent has no
// command line at all, and what it was launched WITH arrives through
// Host::launchArgument() instead. android_main() is the entry point there, and
// it constructs the app and the AndroidHost directly.
int app_shell_main(int argc, char** argv);

// Undo the desktop bootstrap's log-file redirection, restoring the process's
// original stdout/stderr. A bootstrap redirects only when there is no terminal
// (a desktop launch), but a CLI mode (`--print-timing`, `--check`, `--help`) is
// still talking to a pipe and must be allowed to answer there.
void app_shell_restore_console();

// ── The two strings an application gives its shell ──────────────────────────
//
// A log file and a window class have to be CALLED something, and only the app
// knows what. Both are compile definitions with working defaults, so app_shell
// builds and runs on its own; Matrix Player sets them (see gui/CMakeLists.txt)
// to exactly the names it has always used, which is why its matrix_player.log
// and its "MatrixPlayerMain" window class did not change when this moved.
#ifndef APP_SHELL_LOG_NAME
#define APP_SHELL_LOG_NAME "app"
#endif
#ifndef APP_SHELL_LOG_NAME_W
#define APP_SHELL_LOG_NAME_W L"app"
#endif
#ifndef APP_SHELL_WINDOW_CLASS
#define APP_SHELL_WINDOW_CLASS L"AppShellMain"
#endif
