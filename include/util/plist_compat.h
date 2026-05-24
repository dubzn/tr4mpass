#ifndef PLIST_COMPAT_H
#define PLIST_COMPAT_H

#include <plist/plist.h>
#include <stdlib.h>

/*
 * Ubuntu 22.04 ships libplist 2.2.0, which lacks plist_mem_free and the
 * 4-argument plist_from_memory(). Newer distros / Homebrew provide both.
 */
#ifdef TR4MPASS_PLIST_LEGACY

#define plist_mem_free free

static inline void tr4mpass_plist_from_memory(const char *data, uint32_t len,
                                              plist_t *plist, const char **err)
{
    (void)err;
    plist_from_memory(data, len, plist);
}

#else

static inline void tr4mpass_plist_from_memory(const char *data, uint32_t len,
                                              plist_t *plist, const char **err)
{
    plist_from_memory(data, len, plist, err);
}

#endif

#endif /* PLIST_COMPAT_H */
