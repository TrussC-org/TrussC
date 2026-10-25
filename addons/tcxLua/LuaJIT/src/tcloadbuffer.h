/* TrussC vendor patch: stable binary input for pre-allocation range checks.
 * Source readers remain streamed. No load size policy or allocator swap.
 * Kept vendor-local so the bundled engine builds independently of tcxLua. */
#ifndef TC_LOAD_BUFFER_H
#define TC_LOAD_BUFFER_H
#define TC_LOAD_MEMORY_ERROR(L) lj_err_mem(L)
#include <stddef.h>
#include <string.h>
typedef struct TcLuaLoadGuard {
  lua_Reader reader;
  void *readerData;
  char *binary;
  size_t binarySize;
  size_t binaryCapacity;
  int started;
  int allowBinary;
} TcLuaLoadGuard;

static const char *tcLuaLoadRead(lua_State *L, void *ud, size_t *size) {
  TcLuaLoadGuard *g = (TcLuaLoadGuard *)ud;
  const char *part;
  if (g->binary) { *size = 0; return NULL; }
  part = g->reader(L, g->readerData, size);
  if (g->started || !part || !*size) return part;
  g->started = 1;
  /* LuaJIT package loaders use exactly SIZE_MAX for in-process embedded
   * bytecode; the lexer establishes its end-marker convention. */
  if (*size == ~(size_t)0) return part;
  if ((unsigned char)part[0] != 27 || !g->allowBinary) return part;
  /* Counts must be checked against all remaining binary bytes before the
   * undumper allocates. Buffer actual reader bytes, without a size cap. */
  do {
    size_t required;
    // Bound (abi): buffer sizes use size_t. Overflow rejects the whole load and is counted by the Lua error result.
    if (*size > (size_t)-1 - g->binarySize) {
      lua_pushliteral(L, "binary chunk size overflow");
      lua_error(L);
    }
    required = g->binarySize + *size;
    if (required > g->binaryCapacity) {
      size_t capacity = g->binaryCapacity ? g->binaryCapacity : 4096;
      char *replacement;
      while (capacity < required && capacity <= (size_t)-1 / 2) capacity *= 2;
      if (capacity < required) capacity = required;
      /* Bound (abi): GCSize accounts native allocator sizes. A narrowing
       * overflow rejects the load through LuaJIT's memory error result. */
      if ((size_t)(GCSize)capacity != capacity) TC_LOAD_MEMORY_ERROR(L);
      /* Use native GC accounting and allocation recovery. */
      replacement = (char *)lj_mem_realloc(L, g->binary, (GCSize)g->binaryCapacity, (GCSize)capacity);
      if (!replacement) TC_LOAD_MEMORY_ERROR(L);
      g->binary = replacement;
      g->binaryCapacity = capacity;
    }
    memcpy(g->binary + g->binarySize, part, *size);
    g->binarySize = required;
    part = g->reader(L, g->readerData, size);
  } while (part && *size);
  *size = g->binarySize;
  return g->binary;
}
static void tcLuaLoadBegin(lua_State *L, TcLuaLoadGuard *g,
                           lua_Reader reader, void *data, const char *mode) {
  memset(g, 0, sizeof(*g));
  (void)L;
  g->reader = reader;
  g->readerData = data;
  g->allowBinary = !mode || strchr(mode, 'b') != NULL;
}
static void tcLuaLoadEnd(lua_State *L, TcLuaLoadGuard *g) {
  if (g->binary) lj_mem_free(G(L), g->binary, g->binaryCapacity);
}
#endif
