// ===================================================================
// Core standard library - time
// ===================================================================

extern func core_rt_time_ms() -> u64
extern func core_rt_monotonic_ms() -> u64
extern func core_rt_sleep_ms(ms: u64) -> void

// Milliseconds since the Unix epoch.
pub func time_ms() -> u64 { return core_rt_time_ms() }
// Milliseconds since an arbitrary fixed point (for measuring durations).
pub func monotonic_ms() -> u64 { return core_rt_monotonic_ms() }
pub func sleep_ms(ms: u64) { core_rt_sleep_ms(ms) }
