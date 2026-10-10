/* Pipkite native module C ABI (version 1).
 * No STL types, C++ exceptions, host-owned state pointers, UI controls or broker access cross this ABI.
 * Callers own output buffers. All text is UTF-8 with explicit byte lengths.
 */
#ifndef PIPKITE_PLUGIN_ABI_H
#define PIPKITE_PLUGIN_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SR_PLUGIN_ABI_VERSION 1u
#define SR_BRIDGE_IMPL_ABI_VERSION 1u

/* ---------------- parser/transformer plugins ---------------- */
typedef struct SrPluginInfo {
    uint32_t abiVersion;     /* must equal SR_PLUGIN_ABI_VERSION */
    const char* moduleType;  /* "parser" */
    const char* id;          /* e.g. "gold_parser_native" */
    const char* version;     /* semantic version, e.g. "1.0.0" */
} SrPluginInfo;

typedef const SrPluginInfo* (*SrPluginQueryFn)(void);
/* Creates a (preferably stateless) instance. Returns NULL on failure. */
typedef void* (*SrPluginCreateFn)(const char* configJson, int32_t configLen);
/* Parses text into canonical signal JSON written to out (not NUL-terminated).
 * Returns bytes written (>0); -needed when cap is too small; 0 on internal error. */
typedef int32_t (*SrPluginParseFn)(void* instance, const char* text, int32_t textLen, char* out, int32_t cap);
/* Returns 1 when healthy. */
typedef int32_t (*SrPluginHealthcheckFn)(void* instance);
typedef void (*SrPluginDestroyFn)(void* instance);

/* ---------------- MT5 bridge implementation modules ---------------- */
typedef struct SrBridgeImplApi {
    uint32_t abiVersion; /* must equal SR_BRIDGE_IMPL_ABI_VERSION */
    const char* (*version)(void);
    int32_t (*healthcheck)(void);
    /* Engine JSON envelope -> EA line format. Returns bytes, -needed if too small, 0 if rejected. */
    int32_t (*toEa)(const char* json, int32_t jsonLen, char* out, int32_t cap);
    /* EA line format -> engine JSON envelope. Same return convention. */
    int32_t (*fromEa)(const char* kv, int32_t kvLen, char* out, int32_t cap);
} SrBridgeImplApi;

typedef const SrBridgeImplApi* (*SrBridgeImplGetApiFn)(void);

#ifdef __cplusplus
}
#endif

#endif
