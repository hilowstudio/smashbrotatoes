// Standalone unit test for RelocPointerTable — the generation-stamped 32-bit
// token <-> 64-bit pointer table that underpins the port's LP64 pointer handling
// and the scene-arena stale-pointer containment. It verifies the live mechanism
// that port_taskman_evict_arena_caches() -> portRelocInvalidateRange() relies on
// at every scene transition (see the arena-recycle rationale in
// decomp/src/sys/taskman.c:syTaskmanStartTask), with zero game/ROM dependencies:
//
//   1. register/resolve round-trips and NULL handling
//   2. STALE DETECTION: a pointer into a scene arena resolves to NULL after that
//      arena range is invalidated (the core guarantee)
//   3. CROSS-SCENE PERSISTENCE: a pointer OUTSIDE the invalidated range (e.g. the
//      intern buffer that survives scene transitions) keeps resolving — the
//      per-slot generation model must NOT invalidate live references
//   4. slot recycling + generation bump after invalidation
//   5. the "same arena address reused next scene" case that defeats a plain
//      range check — an old token stays stale even though a NEW registration
//      hands out a valid token for the exact same address
//
// Build/run: the `reloc_pointer_table_test` CMake target (SSB64_BUILD_TESTS=ON,
// default ON). Exits 0 on success, non-zero with a message on the first failure.

#include "RelocPointerTable.h"

#include <cstdio>
#include <cstdint>

static int g_failures = 0;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);      \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

// Stand-ins for real memory with real, comparable addresses. `arena` models the
// recycled scene arena; `intern` models the persistent intern buffer.
static char arena[64 * 1024];
static char intern[16 * 1024];

static void test_basic_roundtrip() {
    portRelocResetPointerTable();

    CHECK(portRelocRegisterPointer(nullptr) == 0, "NULL registers as token 0");
    CHECK(portRelocResolvePointer(0) == nullptr, "token 0 resolves to NULL");

    void* p = &arena[128];
    uint32_t tok = portRelocRegisterPointer(p);
    CHECK(tok != 0, "non-NULL pointer gets a non-zero token");
    CHECK(portRelocResolvePointer(tok) == p, "token resolves back to the pointer");

    // A garbage token must fail closed (return NULL), never crash.
    CHECK(portRelocResolvePointer(0x7FFFFFFFu) == nullptr, "bogus token resolves to NULL");
}

static void test_stale_detection_and_persistence() {
    portRelocResetPointerTable();

    void* arena_ptr = &arena[4096];    // scene-scoped: freed on scene boundary
    void* intern_ptr = &intern[64];    // persistent: survives scene boundary

    uint32_t arena_tok = portRelocRegisterPointer(arena_ptr);
    uint32_t intern_tok = portRelocRegisterPointer(intern_ptr);

    CHECK(portRelocResolvePointer(arena_tok) == arena_ptr, "arena token valid pre-eviction");
    CHECK(portRelocResolvePointer(intern_tok) == intern_ptr, "intern token valid pre-eviction");

    // Scene boundary: the arena range is recycled. This is exactly what
    // port_taskman_evict_arena_caches() calls.
    portRelocInvalidateRange(arena, sizeof(arena));

    // CORE GUARANTEE: the arena reference is now detected as stale...
    CHECK(portRelocResolvePointer(arena_tok) == nullptr, "arena token STALE after eviction");
    // ...while the intern reference (outside the range) stays live.
    CHECK(portRelocResolvePointer(intern_tok) == intern_ptr, "intern token SURVIVES eviction");
}

static void test_same_address_reuse() {
    // The scene arena is a singleton recycled at the SAME address every scene,
    // so a plain "is this pointer in the arena range?" check can't tell a stale
    // reference from a fresh one. The generation stamp is what distinguishes
    // them: the OLD token stays stale even though the SAME address, re-registered
    // next scene, produces a NEW valid token.
    portRelocResetPointerTable();

    void* addr = &arena[2048];
    uint32_t old_tok = portRelocRegisterPointer(addr);
    CHECK(portRelocResolvePointer(old_tok) == addr, "old token valid in scene N");

    portRelocInvalidateRange(arena, sizeof(arena));           // end scene N
    CHECK(portRelocResolvePointer(old_tok) == nullptr, "old token stale after scene N ends");

    uint32_t new_tok = portRelocRegisterPointer(addr);        // scene N+1, same address
    CHECK(new_tok != old_tok, "re-registration mints a distinct token (bumped gen)");
    CHECK(portRelocResolvePointer(new_tok) == addr, "new token valid in scene N+1");
    CHECK(portRelocResolvePointer(old_tok) == nullptr, "stale token STAYS stale despite address reuse");
}

static void test_many_cycles() {
    // Register + invalidate across many scene cycles; every prior token must be
    // stale and no live token may be falsely invalidated.
    portRelocResetPointerTable();
    void* persistent = &intern[100];
    uint32_t persistent_tok = portRelocRegisterPointer(persistent);

    uint32_t prev = 0;
    for (int scene = 0; scene < 200; ++scene) {
        uint32_t tok = portRelocRegisterPointer(&arena[(scene * 37) % (sizeof(arena) - 4)]);
        if (prev != 0) {
            CHECK(portRelocResolvePointer(prev) == nullptr, "previous-scene token is stale");
        }
        portRelocInvalidateRange(arena, sizeof(arena));
        prev = tok;
    }
    CHECK(portRelocResolvePointer(persistent_tok) == persistent,
          "persistent token survives 200 scene cycles");
}

int main() {
    test_basic_roundtrip();
    test_stale_detection_and_persistence();
    test_same_address_reuse();
    test_many_cycles();

    if (g_failures == 0) {
        std::printf("RelocPointerTable_test: ALL PASS\n");
        return 0;
    }
    std::printf("RelocPointerTable_test: %d FAILURE(S)\n", g_failures);
    return 1;
}
