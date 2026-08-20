/**
 * port_watchdog.cpp — see port_watchdog.h for architecture notes.
 */

#include "port_watchdog.h"
#include "port_log.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

extern "C" {
unsigned char port_diag_get_scene_curr(void);
unsigned char port_diag_get_scene_prev(void);
const char *port_diag_get_scene_name(unsigned char id);
int port_log_get_fd(void);
}

namespace {

std::atomic<uint64_t> sYieldCount{0};
std::atomic<uint64_t> sFrameCount{0};
std::atomic<int>      sResumeActiveThreadId{-1};
std::atomic<uint64_t> sResumeStartMs{0};
std::atomic<bool>     sShutdown{false};
std::atomic<bool>     sStarted{false};
std::thread           sWatchdogThread;

constexpr uint64_t kHangThresholdMs = 3000;
constexpr uint64_t kRepeatLogMs     = 2000;

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

const char *ThreadLabel(int id) {
    /* Thread IDs are set by osCreateThread throughout sys/main.c and friends.
     * Values match dSYMainIdleStackArg, scheduler init, etc. */
    switch (id) {
    case -1: return "none";
    case 1:  return "idle";
    case 3:  return "scheduler";
    case 4:  return "audio";
    case 5:  return "game";
    case 6:  return "controller";
    default: return "service";
    }
}

void WatchdogLoop() {
    uint64_t last_yield = sYieldCount.load();
    uint64_t last_frame = sFrameCount.load();
    uint64_t last_yield_change_ms = NowMs();
    uint64_t last_frame_change_ms = NowMs();
    uint64_t last_log_ms = 0;

    while (!sShutdown.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        uint64_t now = NowMs();
        uint64_t yc  = sYieldCount.load(std::memory_order_relaxed);
        uint64_t fc  = sFrameCount.load(std::memory_order_relaxed);

        if (yc != last_yield) { last_yield = yc; last_yield_change_ms = now; }
        if (fc != last_frame) { last_frame = fc; last_frame_change_ms = now; }

        /* Grace period — don't fire alarms during early boot before the
         * first frame has been pumped. */
        if (fc == 0) continue;

        uint64_t since_yield_ms = now - last_yield_change_ms;
        uint64_t since_frame_ms = now - last_frame_change_ms;

        /* Require both liveness counters to stall. A running coroutine
         * could yield tens of thousands of times per frame; a frame that
         * takes >3s without any yield is a genuine hang. */
        if (since_frame_ms > kHangThresholdMs && since_yield_ms > kHangThresholdMs) {
            if (now - last_log_ms < kRepeatLogMs) continue;
            last_log_ms = now;

            int active_id = sResumeActiveThreadId.load(std::memory_order_relaxed);
            uint64_t resume_start = sResumeStartMs.load(std::memory_order_relaxed);
            uint64_t resume_elapsed_ms =
                (active_id >= 0 && resume_start > 0) ? (now - resume_start) : 0;

            unsigned char scene      = port_diag_get_scene_curr();
            unsigned char scene_prev = port_diag_get_scene_prev();

            /* Write via both port_log (persistent) and stderr (terminal). */
            const char *fmt =
                "SSB64: WATCHDOG HANG since_frame=%llums since_yield=%llums "
                "active_tid=%d(%s) active_elapsed=%llums "
                "scene=%u(%s) prev=%u(%s) frame=%llu yield_count=%llu\n";

            port_log(fmt,
                     (unsigned long long)since_frame_ms,
                     (unsigned long long)since_yield_ms,
                     active_id, ThreadLabel(active_id),
                     (unsigned long long)resume_elapsed_ms,
                     (unsigned)scene, port_diag_get_scene_name(scene),
                     (unsigned)scene_prev, port_diag_get_scene_name(scene_prev),
                     (unsigned long long)fc,
                     (unsigned long long)yc);

            std::fprintf(stderr, fmt,
                         (unsigned long long)since_frame_ms,
                         (unsigned long long)since_yield_ms,
                         active_id, ThreadLabel(active_id),
                         (unsigned long long)resume_elapsed_ms,
                         (unsigned)scene, port_diag_get_scene_name(scene),
                         (unsigned)scene_prev, port_diag_get_scene_name(scene_prev),
                         (unsigned long long)fc,
                         (unsigned long long)yc);
            std::fflush(stderr);
        }
    }
}

} // namespace

extern "C" void port_watchdog_init(void) {
    bool expected = false;
    if (!sStarted.compare_exchange_strong(expected, true)) return;
    sWatchdogThread = std::thread(WatchdogLoop);
    port_log("SSB64: watchdog started (hang threshold=%llums)\n",
             (unsigned long long)kHangThresholdMs);
}

extern "C" void port_watchdog_shutdown(void) {
    if (!sStarted.load()) return;
    sShutdown.store(true, std::memory_order_release);
    if (sWatchdogThread.joinable()) {
        sWatchdogThread.join();
    }
}

extern "C" void port_watchdog_note_yield(void) {
    sYieldCount.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void port_watchdog_note_resume_start(int thread_id) {
    sResumeStartMs.store(NowMs(), std::memory_order_relaxed);
    sResumeActiveThreadId.store(thread_id, std::memory_order_relaxed);
}

extern "C" void port_watchdog_note_resume_end(int /*thread_id*/) {
    sResumeActiveThreadId.store(-1, std::memory_order_relaxed);
    sResumeStartMs.store(0, std::memory_order_relaxed);
}

extern "C" void port_watchdog_note_frame_end(void) {
    sFrameCount.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void port_dump_backtrace(void) { /* no-op: crash backtraces are handled by the Win32 SEH filter in port.cpp */ }
