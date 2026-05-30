#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "util/log.h"

#define ANSI_RESET  "\033[0m"
#define ANSI_WHITE  "\033[37m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RED    "\033[31m"

static log_level_t g_level   = LOG_INFO;
static int         g_use_color = -1;  /* -1 = uninitialized */

static int should_use_color(void)
{
    if (g_use_color == -1)
        g_use_color = isatty(STDERR_FILENO);
    return g_use_color;
}

void log_set_level(log_level_t level)
{
    g_level = level;
}

static void log_vprint(log_level_t level, const char *color,
                       const char *tag, const char *fmt, va_list ap)
{
    time_t now;
    struct tm tm_now;
    char timestamp[9];

    if (level < g_level)
        return;

    now = time(NULL);
    if (localtime_r(&now, &tm_now) != NULL)
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &tm_now);
    else
        snprintf(timestamp, sizeof(timestamp), "--:--:--");

    if (should_use_color())
        fprintf(stderr, "%s%s %s ", color, timestamp, tag);
    else
        fprintf(stderr, "%s %s ", timestamp, tag);
    vfprintf(stderr, fmt, ap);
    if (should_use_color())
        fprintf(stderr, "%s\n", ANSI_RESET);
    else
        fprintf(stderr, "\n");
}

void log_debug(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vprint(LOG_DEBUG, ANSI_WHITE, "[DEBUG]", fmt, ap);
    va_end(ap);
}

void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vprint(LOG_INFO, ANSI_GREEN, "[INFO]", fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vprint(LOG_WARN, ANSI_YELLOW, "[WARN]", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vprint(LOG_ERROR, ANSI_RED, "[ERROR]", fmt, ap);
    va_end(ap);
}
