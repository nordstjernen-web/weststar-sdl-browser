/* Weststar SDL Browser — supervisor process that restarts the browser on crash or hang.
 * Respawns with backoff and recovery mode; the child runs a main-loop heartbeat hang monitor.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "watchdog.h"
#include "plat/ns_sys.h"
#include "plat/ns_fs.h"
#include "plat/ns_plat.h"
#include "security.h"
#include "plat/ns_loop.h"
#include "plat/ns_plat.h"
#include "plat/ns_env.h"
#include "plat/ns_fs.h"
#include "debuglog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include "plat/ns_sdl.h"

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

/* SDL2 has no process API: an SDL2 build starts the browser with the
 * system's own calls, CreateProcessW or fork and exec. The Amiga family
 * has neither, and runs without the watchdog. */
#if defined(NS_SDL2) && (defined(__amigaos4__) || defined(__MORPHOS__) || \
                         defined(__AROS__))
#define NS_WATCHDOG_NO_PROCESSES 1
#endif
#if defined(NS_SDL2) && !defined(G_OS_WIN32) && !defined(NS_WATCHDOG_NO_PROCESSES)
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#endif

#define NS_WATCHDOG_FLAG           "--watchdog"
#define NS_WATCHDOG_NO_FLAG        "--no-watchdog"
#define NS_WATCHDOG_CHILD_FLAG     "--watchdog-child"
#define NS_WATCHDOG_SESSION_PREFIX "--watchdog-session="
#define NS_WATCHDOG_RECOVER_ENV    "NS_WATCHDOG_RECOVER"

#define NS_WATCHDOG_BEAT_SECS      2
#define NS_WATCHDOG_CHECK_SECS     1
#define NS_WATCHDOG_HANG_MIN_SECS  60
#define NS_WATCHDOG_BACKOFF_MS     1000
#define NS_WATCHDOG_BURST_MAX      5
#define NS_WATCHDOG_BURST_SECS     60
#define NS_WATCHDOG_STABLE_SECS    300
#define NS_WATCHDOG_STOP_GRACE_SECS 3
#define NS_WATCHDOG_HANG_EXIT      70

static gint g_beat;
static int  g_hang_secs;

/* Main-loop heartbeat that the hang thread watches for progress. */
static int
ns_watchdog_beat(void *user_data)
{
    (void)user_data;
    g_atomic_int_inc(&g_beat);
    return NS_SOURCE_CONTINUE;
}

/* Exits the process with the hang code once the heartbeat stalls past the hang limit. */
static gpointer
ns_watchdog_hang_thread(gpointer user_data)
{
    (void)user_data;
    gint   last = g_atomic_int_get(&g_beat);
    gint64 last_change = ns_time_monotonic_us();
    for (;;) {
        ns_sleep_us((gulong)NS_WATCHDOG_CHECK_SECS * G_USEC_PER_SEC);
        gint   cur = g_atomic_int_get(&g_beat);
        gint64 now = ns_time_monotonic_us();
        if (cur != last) {
            last = cur;
            last_change = now;
            continue;
        }
        if (now - last_change > (gint64)g_hang_secs * G_USEC_PER_SEC) {
            fprintf(stderr,
                    "ns_watchdog: main loop unresponsive for %ds — exiting for restart\n",
                    g_hang_secs);
            fflush(stderr);
            _Exit(NS_WATCHDOG_HANG_EXIT);
        }
    }
    return NULL;
}

/* Linux: makes the child get SIGTERM when the supervisor dies; exits if already orphaned. */
void
ns_watchdog_child_guard_parent_death(void)
{
#ifdef __linux__
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() == 1)
        _Exit(0);
#endif
}

/* Starts the heartbeat and hang thread; limit is the JS budget plus a minimum. */
void
ns_watchdog_child_arm_hang_monitor(int js_budget_ms)
{
    g_hang_secs = js_budget_ms / 1000 + NS_WATCHDOG_HANG_MIN_SECS;
    if (g_hang_secs < NS_WATCHDOG_HANG_MIN_SECS)
        g_hang_secs = NS_WATCHDOG_HANG_MIN_SECS;
    g_message("ns_watchdog: hang monitor armed, exit after %ds unresponsive",
              g_hang_secs);
    /* On the main thread's NsLoop: the beat stalls when that thread does. */
    ns_loop_timeout_add_seconds(NULL, NS_WATCHDOG_BEAT_SECS, ns_watchdog_beat,
                                NULL);
    ns_thread_unref(ns_thread_new("nd-watchdog", ns_watchdog_hang_thread, NULL));
}

/* Returns the text after prefix in the first matching argv entry, or NULL. */
static const char *
ns_watchdog_arg_value(int argc, char **argv, const char *prefix)
{
    for (int i = 1; i < argc; i++)
        if (argv[i] && g_str_has_prefix(argv[i], prefix))
            return argv[i] + strlen(prefix);
    return NULL;
}

const char *
ns_watchdog_child_session_arg(int argc, char **argv)
{
    return ns_watchdog_arg_value(argc, argv, NS_WATCHDOG_SESSION_PREFIX);
}

gboolean
ns_watchdog_is_child(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (argv[i] && g_strcmp0(argv[i], NS_WATCHDOG_CHILD_FLAG) == 0)
            return TRUE;
    return FALSE;
}

/* Returns TRUE if the supervisor launched this child to recover a crashed session. */
gboolean
ns_watchdog_child_is_recovery(void)
{
    const char *v = ns_getenv(NS_WATCHDOG_RECOVER_ENV);
    return v && g_strcmp0(v, "1") == 0;
}

/* Returns TRUE for command-line modes that run once and must not be supervised. */
static gboolean
ns_watchdog_is_oneshot(const char *arg)
{
    return g_strcmp0(arg, "--headless")     == 0 ||
           g_strcmp0(arg, "--print-config") == 0 ||
           g_str_has_prefix(arg, "--dump=")       ||
           g_str_has_prefix(arg, "--export=")     ||
           g_str_has_prefix(arg, "--eval=")       ||
           g_str_has_prefix(arg, "--inspect=")    ||
           g_str_has_prefix(arg, "--inspect-at=");
}

/* Decides whether to run under the watchdog from flags, one-shot modes and default. */
gboolean
ns_watchdog_should_supervise(int argc, char **argv, gboolean enabled_by_default)
{
    gboolean forced = FALSE;
#ifdef NS_WATCHDOG_NO_PROCESSES
    (void)argc;
    (void)argv;
    (void)forced;
    (void)enabled_by_default;
    return FALSE;
#endif
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (g_strcmp0(argv[i], NS_WATCHDOG_CHILD_FLAG) == 0) return FALSE;
        if (g_strcmp0(argv[i], NS_WATCHDOG_NO_FLAG) == 0)    return FALSE;
        if (ns_watchdog_is_oneshot(argv[i]))                 return FALSE;
        if (g_strcmp0(argv[i], NS_WATCHDOG_FLAG) == 0)       forced = TRUE;
    }
    return forced || enabled_by_default;
}

typedef struct {
    char       **child_argv;
    char        *session_path;
#ifndef NS_SDL2
    SDL_Process *process;
#elif defined(G_OS_WIN32)
    HANDLE       process;
#else
    long         process; /* the browser's process id, 0 when none */
#endif
    int          burst_count;
    gint64       burst_start_us;
    gint64       spawned_us;
    int          recover_streak;
    int          exit_status;
} ns_watchdog;

/* Windows: shows a startup-failure message box naming the diagnostic log. */
static void
ns_watchdog_report_failure(const char *detail)
{
#ifdef G_OS_WIN32
    const char *log = ns_debug_log_file_path();
    char *body = g_strdup_printf(
        "Weststar SDL Browser could not start.\n\n%s\n\n"
        "A diagnostic log was written to:\n%s",
        detail ? detail : "The browser process exited before opening a window.",
        log ? log : "(log file unavailable)");
    wchar_t *wbody = (wchar_t *)g_utf8_to_utf16(body, -1, NULL, NULL, NULL);
    if (wbody) {
        MessageBoxW(NULL, wbody, L"Weststar SDL Browser",
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        g_free(wbody);
    }
    g_free(body);
#else
    (void)detail;
#endif
}

#ifndef G_OS_WIN32
/* What the signal handlers read and write: whether the supervisor was asked
 * to stop, and the browser's process id while it runs (0 otherwise). */
static volatile sig_atomic_t g_stop_requested;
static volatile sig_atomic_t g_child_pid;

/* SIGINT or SIGTERM: asks the browser to stop, and arms the alarm that
 * kills it if it has not gone after the grace period. The supervisor's
 * wait for the browser then returns. */
static void
ns_watchdog_on_stop_signal(int sig)
{
    (void)sig;
    pid_t pid = (pid_t)g_child_pid;
    if (g_stop_requested)
        return;
    g_stop_requested = 1;
    if (pid > 0) {
        kill(pid, SIGTERM);
        alarm(NS_WATCHDOG_STOP_GRACE_SECS);
    }
}

static void
ns_watchdog_on_alarm(int sig)
{
    (void)sig;
    pid_t pid = (pid_t)g_child_pid;
    if (pid > 0)
        kill(pid, SIGKILL);
}

/* The handlers restart the system call they interrupt, so that the wait
 * for the browser goes on until it has really exited. */
static void
ns_watchdog_install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = ns_watchdog_on_stop_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sa.sa_handler = ns_watchdog_on_alarm;
    sigaction(SIGALRM, &sa, NULL);
}
#endif

static gboolean
ns_watchdog_stopping(void)
{
#ifdef G_OS_WIN32
    return FALSE;
#else
    return g_stop_requested != 0;
#endif
}

#ifndef NS_SDL2

/* How the browser gets standard stream `fd`: the supervisor's own. A
 * Windows program started from the desktop has none, and SDL fails to
 * start a process that is to inherit a stream that is not there; the
 * browser then gets the null device. */
static SDL_ProcessIO
ns_watchdog_stdio(int fd)
{
#ifdef G_OS_WIN32
    static const DWORD ids[] = {
        STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE,
    };
    HANDLE h = GetStdHandle(ids[fd]);
    if (!h || h == INVALID_HANDLE_VALUE)
        return SDL_PROCESS_STDIO_NULL;
#else
    (void)fd;
#endif
    return SDL_PROCESS_STDIO_INHERITED;
}

/* Starts the browser; FALSE with *error set when it could not be. */
static gboolean
ns_watchdog_start_process(ns_watchdog *wd, gboolean recover, char **error)
{
    SDL_Environment *env = SDL_CreateEnvironment(true);
    SDL_PropertiesID props = SDL_CreateProperties();
    if (env) {
        if (recover)
            SDL_SetEnvironmentVariable(env, NS_WATCHDOG_RECOVER_ENV, "1", true);
        else
            SDL_UnsetEnvironmentVariable(env, NS_WATCHDOG_RECOVER_ENV);
    }
    if (env && props) {
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                               wd->child_argv);
        SDL_SetPointerProperty(props,
                               SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,
                               env);
        /* The browser reads and writes where the supervisor does. */
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                              ns_watchdog_stdio(0));
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                              ns_watchdog_stdio(1));
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                              ns_watchdog_stdio(2));
        wd->process = SDL_CreateProcessWithProperties(props);
    }
    if (!wd->process)
        *error = g_strdup(SDL_GetError());
    if (props) SDL_DestroyProperties(props);
    if (env) SDL_DestroyEnvironment(env);

    return wd->process != NULL;
}

#ifndef G_OS_WIN32
static long
ns_watchdog_process_id(ns_watchdog *wd)
{
    return (long)SDL_GetNumberProperty(
        SDL_GetProcessProperties(wd->process), SDL_PROP_PROCESS_PID_NUMBER, 0);
}
#endif

/* Waits for the browser to exit, and forgets it. */
static gboolean
ns_watchdog_wait_process(ns_watchdog *wd, int *code)
{
    bool waited = SDL_WaitProcess(wd->process, true, code);
    if (!waited)
        g_warning("ns_watchdog: lost track of the browser: %s", SDL_GetError());
    SDL_DestroyProcess(wd->process);
    wd->process = NULL;
    return waited;
}

#elif defined(G_OS_WIN32)

/* One argument of a command line, quoted so that the C runtime reads it
 * back as it was. */
static void
ns_watchdog_append_arg(GString *cmd, const char *arg)
{
    if (cmd->len)
        g_string_append_c(cmd, ' ');
    if (*arg && !strpbrk(arg, " \t\n\v\"")) {
        g_string_append(cmd, arg);
        return;
    }
    g_string_append_c(cmd, '"');
    for (const char *p = arg;; p++) {
        gsize slashes = 0;
        while (*p == '\\') {
            p++;
            slashes++;
        }
        if (!*p) {
            for (gsize k = 0; k < slashes * 2; k++)
                g_string_append_c(cmd, '\\');
            break;
        }
        gsize n = *p == '"' ? slashes * 2 + 1 : slashes;
        for (gsize k = 0; k < n; k++)
            g_string_append_c(cmd, '\\');
        g_string_append_c(cmd, *p);
    }
    g_string_append_c(cmd, '"');
}

/* The supervisor's standard handles, for the browser to inherit: none
 * when it has none, as when started from the desktop. */
static gboolean
ns_watchdog_std_handles(STARTUPINFOW *si)
{
    static const DWORD ids[] = {
        STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE,
    };
    HANDLE h[3];
    gboolean any = FALSE;
    for (int i = 0; i < 3; i++) {
        h[i] = GetStdHandle(ids[i]);
        if (h[i] == INVALID_HANDLE_VALUE ||
            (h[i] && !SetHandleInformation(h[i], HANDLE_FLAG_INHERIT,
                                           HANDLE_FLAG_INHERIT)))
            h[i] = NULL;
        any = any || h[i];
    }
    if (!any)
        return FALSE;
    si->dwFlags |= STARTF_USESTDHANDLES;
    si->hStdInput = h[0];
    si->hStdOutput = h[1];
    si->hStdError = h[2];
    return TRUE;
}

static gboolean
ns_watchdog_start_process(ns_watchdog *wd, gboolean recover, char **error)
{
    GString *cmd = g_string_new(NULL);
    for (char **a = wd->child_argv; *a; a++)
        ns_watchdog_append_arg(cmd, *a);
    wchar_t *wcmd = (wchar_t *)g_utf8_to_utf16(cmd->str, -1, NULL, NULL, NULL);
    wchar_t *wexe = (wchar_t *)g_utf8_to_utf16(wd->child_argv[0], -1, NULL,
                                               NULL, NULL);
    g_string_free(cmd, TRUE);
    /* The browser inherits the supervisor's environment; the supervisor
     * itself does not read this variable. */
    SetEnvironmentVariableW(L"" NS_WATCHDOG_RECOVER_ENV, recover ? L"1" : NULL);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    BOOL inherit = ns_watchdog_std_handles(&si);
    BOOL ok = wcmd && wexe &&
              CreateProcessW(wexe, wcmd, NULL, NULL, inherit, 0, NULL, NULL,
                             &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        wd->process = pi.hProcess;
    } else {
        *error = g_win32_error_message((gint)GetLastError());
    }
    g_free(wcmd);
    g_free(wexe);
    return ok;
}

static gboolean
ns_watchdog_wait_process(ns_watchdog *wd, int *code)
{
    DWORD status = 1;
    gboolean waited =
        WaitForSingleObject(wd->process, INFINITE) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(wd->process, &status);
    if (!waited)
        g_warning("ns_watchdog: lost track of the browser (error %lu)",
                  (unsigned long)GetLastError());
    *code = (int)status;
    CloseHandle(wd->process);
    wd->process = NULL;
    return waited;
}

#elif !defined(NS_WATCHDOG_NO_PROCESSES)

static gboolean
ns_watchdog_start_process(ns_watchdog *wd, gboolean recover, char **error)
{
    pid_t pid = fork();
    if (pid < 0) {
        *error = g_strdup(g_strerror(errno));
        return FALSE;
    }
    if (pid == 0) {
        /* The browser-to-be: its environment is its own now. */
        if (recover)
            setenv(NS_WATCHDOG_RECOVER_ENV, "1", 1);
        else
            unsetenv(NS_WATCHDOG_RECOVER_ENV);
        execv(wd->child_argv[0], wd->child_argv);
        _exit(127);
    }
    wd->process = (long)pid;
    return TRUE;
}

static long
ns_watchdog_process_id(ns_watchdog *wd)
{
    return wd->process;
}

static gboolean
ns_watchdog_wait_process(ns_watchdog *wd, int *code)
{
    int status = 0;
    pid_t r;
    do {
        r = waitpid((pid_t)wd->process, &status, 0);
    } while (r < 0 && errno == EINTR);
    wd->process = 0;
    if (r < 0) {
        g_warning("ns_watchdog: lost track of the browser: %s",
                  g_strerror(errno));
        return FALSE;
    }
    *code = WIFEXITED(status) ? WEXITSTATUS(status)
          : WIFSIGNALED(status) ? -WTERMSIG(status) : 1;
    return TRUE;
}

#else /* NS_WATCHDOG_NO_PROCESSES: never supervised */

static gboolean
ns_watchdog_start_process(ns_watchdog *wd, gboolean recover, char **error)
{
    (void)wd;
    (void)recover;
    *error = g_strdup("this system cannot start the browser as a process "
                      "of its own");
    return FALSE;
}

static long
ns_watchdog_process_id(ns_watchdog *wd)
{
    (void)wd;
    return 0;
}

static gboolean
ns_watchdog_wait_process(ns_watchdog *wd, int *code)
{
    (void)wd;
    *code = 1;
    return FALSE;
}

#endif

/* Launches the browser child, in recovery mode when asked: with
 * NS_WATCHDOG_RECOVER=1 in its environment, and only in its. */
static gboolean
ns_watchdog_spawn(ns_watchdog *wd, gboolean recover)
{
    char *error = NULL;
    if (!ns_watchdog_start_process(wd, recover, &error)) {
        g_warning("ns_watchdog: failed to launch browser: %s", error);
        char *detail = g_strdup_printf("Failed to launch the browser process: %s",
                                       error);
        ns_watchdog_report_failure(detail);
        g_free(detail);
        g_free(error);
        return FALSE;
    }
    wd->spawned_us = ns_time_monotonic_us();
#ifndef G_OS_WIN32
    g_child_pid = (sig_atomic_t)ns_watchdog_process_id(wd);
    /* A stop that arrived while the browser was being started. */
    if (g_stop_requested && g_child_pid > 0) {
        kill((pid_t)g_child_pid, SIGTERM);
        alarm(NS_WATCHDOG_STOP_GRACE_SECS);
    }
#endif
    return TRUE;
}

/* Waits for the browser to exit. *code gets its exit code, or on Unix the
 * negative of the signal that ended it. */
static gboolean
ns_watchdog_wait(ns_watchdog *wd, int *code)
{
    gboolean waited = ns_watchdog_wait_process(wd, code);
#ifndef G_OS_WIN32
    g_child_pid = 0;
    alarm(0);
#endif
    return waited;
}

/* Whether the browser should be started again after exiting with `code`:
 * not after a clean exit, nor when it was told to go. */
static gboolean
ns_watchdog_should_restart(int code)
{
    if (code == 0)
        return FALSE;
#ifndef G_OS_WIN32
    if (code < 0) {
        int sig = -code;
        if (sig == SIGTERM || sig == SIGINT || sig == SIGHUP ||
            sig == SIGQUIT || sig == SIGKILL) {
            g_message("ns_watchdog: browser terminated by signal %d — stopping",
                      sig);
            return FALSE;
        }
        g_warning("ns_watchdog: browser stopped by signal %d", sig);
        return TRUE;
    }
#endif
    g_warning("ns_watchdog: browser exited with code %d", code);
    return TRUE;
}

/* Counts a failure; FALSE when there were too many in a short time. */
static gboolean
ns_watchdog_count_restart(ns_watchdog *wd)
{
    gint64 now = ns_time_monotonic_us();
    if (now - wd->burst_start_us > (gint64)NS_WATCHDOG_BURST_SECS * G_USEC_PER_SEC) {
        wd->burst_start_us = now;
        wd->burst_count = 0;
    }
    wd->burst_count++;
    if (wd->burst_count > NS_WATCHDOG_BURST_MAX) {
        g_warning("ns_watchdog: child failed %d times in under %ds — giving up",
                  wd->burst_count, NS_WATCHDOG_BURST_SECS);
        ns_watchdog_report_failure(
            "The browser repeatedly exited during startup.");
        return FALSE;
    }
    g_message("ns_watchdog: restarting browser (attempt %d)", wd->burst_count);
    return TRUE;
}

/* Builds child argv without watchdog flags, adding the child flag and session path. */
static char **
ns_watchdog_build_child_argv(const char *self_exe, int argc, char **argv,
                             const char *session_path)
{
    GPtrArray *args = g_ptr_array_new();
    g_ptr_array_add(args, g_strdup(self_exe));
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (g_strcmp0(argv[i], NS_WATCHDOG_FLAG) == 0) continue;
        if (g_strcmp0(argv[i], NS_WATCHDOG_NO_FLAG) == 0) continue;
        if (g_strcmp0(argv[i], NS_WATCHDOG_CHILD_FLAG) == 0) continue;
        if (g_str_has_prefix(argv[i], NS_WATCHDOG_SESSION_PREFIX)) continue;
        g_ptr_array_add(args, g_strdup(argv[i]));
    }
    g_ptr_array_add(args, g_strdup(NS_WATCHDOG_CHILD_FLAG));
    g_ptr_array_add(args, g_strconcat(NS_WATCHDOG_SESSION_PREFIX, session_path, NULL));
    g_ptr_array_add(args, NULL);
    return (char **)g_ptr_array_free(args, FALSE);
}

/* Supervisor main: runs and restarts the browser child; returns its final exit status.
 * The supervisor is a process of its own that only starts the browser and
 * waits for it, so it needs no event loop: it starts the browser with
 * SDL's process API (in an SDL2 build, the system's) and blocks until it
 * exits. On Unix, SIGINT and
 * SIGTERM are passed on to the browser from their handlers. */
int
ns_watchdog_run_supervisor(const char *self_exe, int argc, char **argv)
{
    ns_watchdog wd = { 0 };

    char *uuid = ns_uuid_string_random();
    char *name = g_strconcat("weststar-watchdog-", uuid, ".session", NULL);
    wd.session_path = g_build_filename(ns_fs_user_runtime_dir(), name, NULL);
    g_free(name);
    g_free(uuid);

    wd.child_argv = ns_watchdog_build_child_argv(self_exe, argc, argv,
                                                 wd.session_path);
    wd.burst_start_us = ns_time_monotonic_us();

#ifndef G_OS_WIN32
    ns_watchdog_install_signals();
#endif

    g_message("ns_watchdog: supervising browser");

    /* Only the first retry of a crash streak runs in recovery mode. */
    gboolean recover = FALSE;
    while (!ns_watchdog_stopping()) {
        int code = 0;
        if (!ns_watchdog_spawn(&wd, recover) || !ns_watchdog_wait(&wd, &code)) {
            wd.exit_status = 1;
            break;
        }
        if (ns_time_monotonic_us() - wd.spawned_us >
            (gint64)NS_WATCHDOG_STABLE_SECS * G_USEC_PER_SEC)
            wd.recover_streak = 0;
        if (ns_watchdog_stopping() || !ns_watchdog_should_restart(code))
            break;
        if (!ns_watchdog_count_restart(&wd)) {
            wd.exit_status = 1;
            break;
        }
        SDL_Delay(NS_WATCHDOG_BACKOFF_MS);
        recover = wd.recover_streak++ == 0;
    }

    ns_fs_unlink(wd.session_path);
    g_strfreev(wd.child_argv);
    g_free(wd.session_path);
    return wd.exit_status;
}
