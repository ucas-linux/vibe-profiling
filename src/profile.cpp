// profile.cpp
#include <stdint.h>
#include <inttypes.h>
#include <time.h>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <csignal>
#include <dlfcn.h>   // dladdr (optional but recommended)

#if defined(__GNUC__)
#define NO_INSTRUMENT __attribute__((no_instrument_function))
#else
#define NO_INSTRUMENT
#endif

// ----------- timing (ns) -----------
static inline uint64_t NO_INSTRUMENT now_ns() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// ----------- per-thread call stack -----------
struct Frame {
  void* fn;
  uint64_t start_ns;
};

static thread_local std::vector<Frame> tls_stack;

// ----------- per-function stats -----------
struct Stats {
  uint64_t min_ns;
  uint64_t max_ns;
  uint64_t count;
};

static std::mutex g_mu;
static std::unordered_map<void*, Stats> g_stats;

// Optional: best-effort symbol name
static const char* NO_INSTRUMENT symbol_name(void* fn, char* buf, size_t buflen) {
  Dl_info info;
  if (dladdr(fn, &info) && info.dli_sname) {
    return info.dli_sname;
  }
  std::snprintf(buf, buflen, "%p", fn);
  return buf;
}

static void NO_INSTRUMENT update_stats(void* fn, uint64_t dur_ns) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_stats.find(fn);
  if (it == g_stats.end()) {
    Stats s;
    s.min_ns = dur_ns;
    s.max_ns = dur_ns;
    s.count  = 1;
    g_stats.emplace(fn, s);
  } else {
    Stats& s = it->second;
    if (dur_ns < s.min_ns) s.min_ns = dur_ns;
    if (dur_ns > s.max_ns) s.max_ns = dur_ns;
    s.count++;
  }
}

static void NO_INSTRUMENT print_top10_spread() {
  struct Row {
    void* fn;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t spread_ns;
    uint64_t count;
  };

  std::vector<Row> rows;
  {
	  //std::lock_guard<std::mutex> lock(g_mu);
    rows.reserve(g_stats.size());
    for (auto& kv : g_stats) {
      const Stats& s = kv.second;
      // Only meaningful if called at least twice; otherwise spread=0.
      uint64_t spread = (s.max_ns >= s.min_ns) ? (s.max_ns - s.min_ns) : 0;
      rows.push_back(Row{kv.first, s.min_ns, s.max_ns, spread, s.count});
    }
  }

  std::sort(rows.begin(), rows.end(),
            [](const Row& a, const Row& b) { return a.spread_ns > b.spread_ns; });

  const size_t topN = std::min<size_t>(50, rows.size());
  std::fprintf(stderr, "\n=== Top %zu functions by (max_duration - min_duration) [ns] ===\n", topN);
  std::fprintf(stderr, "%-4s %-40s %12s %12s %12s %8s\n",
               "Rank", "Function", "min(ns)", "max(ns)", "spread", "calls");

  for (size_t i = 0; i < topN; i++) {
    char buf[100];
    const char* name = symbol_name(rows[i].fn, buf, sizeof(buf));
    std::fprintf(stderr, "%-4zu %-100s %12" PRIu64 " %12" PRIu64 " %12" PRIu64 " %8" PRIu64 "\n",
                 i + 1, name,
                 rows[i].min_ns, rows[i].max_ns, rows[i].spread_ns, rows[i].count);
  }
  std::fprintf(stderr, "=============================================================\n");
}

// Ensure we print once at exit
static std::atomic<bool> g_registered{false};

static void NO_INSTRUMENT ensure_registered() {
  bool expected = false;
  if (g_registered.compare_exchange_strong(expected, true)) {
    std::atexit(print_top10_spread);
  }
}
extern "C" {
	extern int startrecord;	
}
void sigusr1_handler(int signum) {
	print_top10_spread();
}
bool call_init_finished = false;
__attribute__((constructor)) void init_after_call_init() {
	// Set the flag to true once initialization is done
	signal(SIGUSR1, sigusr1_handler);
	call_init_finished = true;
}

extern "C" {
	extern void * _ZN7console11set_displayE12display_type;
	extern void * _ZN7console8readlineERNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEb;
	void *symbol_addr = &_ZN7console8readlineERNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEb;
	int beginrecord = 0;
thread_local bool has_entered = false;
thread_local bool has_exited = false;
	bool is_function_excluded(void* this_function);


void NO_INSTRUMENT __cyg_profile_func_enter(void* this_fn, void* call_site) {
	if (symbol_addr == this_fn) {
		beginrecord = 1;
	}

	if (has_entered || !call_init_finished|| has_exited||!beginrecord)
		return;
	if (is_function_excluded(this_fn))
		return;
	
	has_entered = true;
	(void)call_site;
	ensure_registered();

	// record start time on per-thread stack
	tls_stack.push_back(Frame{this_fn, now_ns()});
	has_entered = false;
}

void NO_INSTRUMENT __cyg_profile_func_exit(void* this_fn, void* call_site) {
  (void)call_site;
  if (has_exited || !call_init_finished || has_entered || !beginrecord)
	  return;

  if (is_function_excluded(this_fn))
		return;
  
  has_exited = true;
  if (tls_stack.empty()) return;

  // In well-formed instrumentation, the top should match this_fn.
  // If it doesn't (rare; e.g., longjmp), we try to recover by searching backwards.
  uint64_t end = now_ns();

  ssize_t idx = (ssize_t)tls_stack.size() - 1;
  if (tls_stack[(size_t)idx].fn != this_fn) {
    for (ssize_t j = idx; j >= 0; --j) {
      if (tls_stack[(size_t)j].fn == this_fn) { idx = j; break; }
    }
  }

  Frame fr = tls_stack[(size_t)idx];
  tls_stack.resize((size_t)idx); // pop everything above (keeps stack consistent)

  uint64_t dur = end - fr.start_ns;
  update_stats(this_fn, dur);
  has_exited = false;
}

} // extern "C"
