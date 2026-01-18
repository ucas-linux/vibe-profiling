// profile.cpp
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <sys/syscall.h>
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

int beginrecord = 1;
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


static void NO_INSTRUMENT print_top10_spread(void);
struct TLSMap {
  std::unordered_map<void*, Stats> m;

  ~TLSMap() {
    const char msg[] = "[trace] TLSMap destructor running\n";
    write(2, msg, sizeof(msg) - 1);
    print_top10_spread();
    beginrecord = 0;

  }
};

thread_local TLSMap thread_stats1;

static std::mutex g_mu;

// Global container to hold merged stats across all threads
std::unordered_map<void*, Stats> g_stats;  

// Thread-local storage for stats
thread_local std::unordered_map<void*, Stats> thread_stats;

// Function to update stats in a thread-local storage
void update_stats_in_thread(void* fn, uint64_t dur_ns) {
    auto& stats = thread_stats1.m[fn];
    if (stats.count == 0) {
        stats.min_ns = dur_ns;
        stats.max_ns = dur_ns;
    } else {
        if (dur_ns < stats.min_ns) stats.min_ns = dur_ns;
        if (dur_ns > stats.max_ns) stats.max_ns = dur_ns;
    }
    stats.count++;
}

// Merge all thread-local stats into the global stats
void merge_thread_stats() {
    for (auto& kv : thread_stats1.m) {
        auto& fn = kv.first;
        auto& thread_stat = kv.second;

        // Merge the thread's stats into the global g_stats
        auto it = g_stats.find(fn);
        if (it == g_stats.end()) {
            g_stats[fn] = thread_stat;
        } else {
            Stats& global_stat = it->second;
            if (thread_stat.min_ns < global_stat.min_ns) global_stat.min_ns = thread_stat.min_ns;
            if (thread_stat.max_ns > global_stat.max_ns) global_stat.max_ns = thread_stat.max_ns;
            global_stat.count += thread_stat.count;
        }
    }
}

// Function to print the top 10 spread

#include <elfutils/libdwfl.h>
#include <cstdio>
#include <cstdlib>
#include <elfutils/libdwfl.h>
#include <cstdio>
#include <cstdint>
#include <unistd.h>

static Dwfl* make_dwfl() {
    static Dwfl_Callbacks cb = {};
    cb.find_elf = dwfl_linux_proc_find_elf;
    cb.find_debuginfo = dwfl_standard_find_debuginfo;
    cb.debuginfo_path = nullptr;

    Dwfl* dwfl = dwfl_begin(&cb);
    if (!dwfl) return nullptr;

    if (dwfl_linux_proc_report(dwfl, getpid()) != 0) return nullptr;
    if (dwfl_report_end(dwfl, nullptr, nullptr) != 0) return nullptr;

    return dwfl;
}

const char* symbolize_addr(void* addr, char* buf, size_t buflen) {
    static Dwfl* dwfl = make_dwfl();
    if (!dwfl) {
        std::snprintf(buf, buflen, "%p", addr);
        return buf;
    }

    Dwarf_Addr a = (Dwarf_Addr)(uintptr_t)addr;

    Dwfl_Module* mod = dwfl_addrmodule(dwfl, a);
    if (!mod) {
        std::snprintf(buf, buflen, "%p", addr);
        return buf;
    }

    // Best effort (DWARF-aware name lookup)
    if (const char* name = dwfl_module_addrname(mod, a)) {
        return name;
    }

    // Fallback: closest ELF symbol (symtab/dynsym); signature uses GElf_Word* on many elfutils
    GElf_Sym sym;
    GElf_Word shndx = 0;
    if (const char* name = dwfl_module_addrsym(mod, a, &sym, &shndx)) {
        return name;
    }

    std::snprintf(buf, buflen, "%p", addr);
    return buf;
}

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
    beginrecord = 0;
    merge_thread_stats();
    std::vector<Row> rows;
    rows.reserve(g_stats.size());

    for (auto& kv : g_stats) {
        const Stats& s = kv.second;
        uint64_t spread = (s.max_ns >= s.min_ns) ? (s.max_ns - s.min_ns) : 0;
        rows.push_back(Row{kv.first, s.min_ns, s.max_ns, spread, s.count});
    }

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.spread_ns > b.spread_ns;
    });

    const size_t topN = std::min<size_t>(50, rows.size());
    std::fprintf(stderr, "\n=== Top %zu functions by (max_duration - min_duration) [ns] ===\n", topN);
    std::fprintf(stderr, "%-4s %-40s %12s %12s %12s %8s\n", "Rank", "Function", "min(ns)", "max(ns)", "spread", "calls");

    for (size_t i = 0; i < topN; i++) {
        char buf[100];
        const char* name = symbolize_addr(rows[i].fn, buf, sizeof(buf));
        std::fprintf(stderr, "%-4zu %-100s %12" PRIu64 " %12" PRIu64 " %12" PRIu64 " %8" PRIu64 "\n",
                     i + 1, name, rows[i].min_ns, rows[i].max_ns, rows[i].spread_ns, rows[i].count);
    }
    std::fprintf(stderr, "=============================================================\n");
}

// Ensure we print once at exit
static std::atomic<bool> g_registered{false};

static void NO_INSTRUMENT ensure_registered() {
  bool expected = false;
  if (g_registered.compare_exchange_strong(expected, true)) {
	  //std::atexit(print_top10_spread);
  }
}
extern "C" {
	extern int startrecord;
	void *symbol_addr = 0;
}


void sigusr1_handler(int signum) {
	print_top10_spread();
}
bool call_init_finished = false;
__attribute__((constructor)) void init_after_call_init() {
	// Set the flag to true once initialization is done
	symbol_addr = dlsym(RTLD_NEXT, "write");
	signal(SIGUSR1, sigusr1_handler);
	call_init_finished = true;
}


static thread_local int g_in_hook = 0;

// Function pointer to the real write()
using write_fn_t = ssize_t(*)(int, const void*, size_t);
static write_fn_t real_write = nullptr;

static inline ssize_t safe_write(int fd, const void* buf, size_t n) {
  return ::syscall(SYS_write, fd, buf, n);  // bypass libc + bypass interposition
}

__attribute__((constructor))
static void init_syms() {
  // Avoid iostream/stdio here; keep constructor minimal.
  real_write = reinterpret_cast<write_fn_t>(::dlsym(RTLD_NEXT, "write"));
}

// Interpose the C symbol "write"
extern "C" ssize_t write(int fd, const void* buf, size_t count) {
  if (!real_write) {
    real_write = reinterpret_cast<write_fn_t>(::dlsym(RTLD_NEXT, "write"));
    if (!real_write) {
      errno = ENOSYS;
      return -1;
    }
  }

  if (g_in_hook) {
    // Re-entrant call: go straight to syscall
    return safe_write(fd, buf, count);
  }

  g_in_hook = 1;

  // --- your audit logic (keep it async-signal-safe-ish) ---
  const char msg[] = "[audit] write() called\n";
  safe_write(STDERR_FILENO, msg, sizeof(msg) - 1);

  // Call the real libc write
  ssize_t rc = real_write(fd, buf, count);

  g_in_hook = 0;
  return rc;
}

extern "C" {
#if 0	
	extern void * _ZN7console11set_displayE12display_type;
	extern void * _ZN7console8readlineERNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEb;
	void *symbol_addr = &_ZN7console8readlineERNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEb;
#endif


thread_local bool has_entered = false;
thread_local bool has_exited = false;
	bool is_function_excluded(void* this_function);
	

void NO_INSTRUMENT __cyg_profile_func_enter(void* this_fn, void* call_site) {
#if 0	
	if (symbol_addr == this_fn) {
		beginrecord = 0;
	}
#endif
	if (has_entered || !call_init_finished|| has_exited||!beginrecord)
		return;
	/*if (is_function_excluded(this_fn))
	  return;*/
	
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
/*
  if (is_function_excluded(this_fn))
		return;
*/
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
  update_stats_in_thread(this_fn, dur);
  has_exited = false;
}

} // extern "C"
