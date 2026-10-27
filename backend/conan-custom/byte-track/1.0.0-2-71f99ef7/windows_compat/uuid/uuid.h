#ifndef UUID_WIN32_COMPAT_H
#define UUID_WIN32_COMPAT_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <rpcdce.h>
#include <cstring>

#pragma comment(lib, "rpcrt4.lib")

typedef UUID uuid_t;

static inline void uuid_generate(uuid_t *out) {
    UuidCreate(out);
}

static inline void uuid_unparse(const uuid_t uu, char *out) {
    RPC_CSTR str;
    if (UuidToStringA(&uu, &str) == RPC_S_OK) {
        strcpy(out, reinterpret_cast<const char*>(str));
        RpcStringFreeA(&str);
    }
}

#endif
