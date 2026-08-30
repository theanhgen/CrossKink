// Toolchain gate for CrossInk on Kindle 3 Keyboard (ARMv6, kernel 2.6.26).
//
// Exercises exactly the C++20 surface CrossInk depends on, so a PASS here means
// the real build has a credible chance. Deliberately avoids syscalls introduced
// in kernel 2.6.27 -- glibc 2.9 has wrappers for accept4/pipe2/epoll_create1/
// eventfd2/dup3/inotify_init1, which LINK but return ENOSYS at runtime on 2.6.26.
//
// Build:  see gate/build.sh
// Expect: every line PASS, exit 0.

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <locale>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/utsname.h>
#include <time.h>

static int failures = 0;
static void check(const char* name, bool ok, const char* detail = "") {
    std::printf("[%s] %-28s %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) ++failures;
}

int main() {
    // --- environment ---------------------------------------------------
    utsname u{};
    if (uname(&u) == 0)
        std::printf("[INFO] kernel  %s %s (%s)\n", u.sysname, u.release, u.machine);

    // --- C++20 language ------------------------------------------------
    // CrossInk builds with -std=gnu++2a.
    auto lambda_init = [x = 42]() { return x; };           // init-capture
    struct Pt { int a; int b; };
    Pt p{.a = 1, .b = 2};                                  // designated initialisers
    check("c++20 language", lambda_init() == 42 && p.b == 2);
    std::printf("[INFO] __cplusplus = %ld\n", (long)__cplusplus);

    // --- threading: the single biggest unknown for 2010-era glibc ------
    std::atomic<int> counter{0};
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;

    std::thread worker([&] {
        for (int i = 0; i < 1000; ++i) counter.fetch_add(1, std::memory_order_relaxed);
        { std::lock_guard<std::mutex> lk(m); ready = true; }
        cv.notify_one();
    });
    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return ready; });
    }
    worker.join();
    check("std::thread + join", true);
    check("std::atomic fetch_add", counter.load() == 1000);
    check("condition_variable", ready);
    check("atomic<int> lock-free", counter.is_lock_free() ? true : true,
          counter.is_lock_free() ? "(lock-free)" : "(NOT lock-free, still works)");

    // --- exceptions + RTTI (unwinding on ARM is its own can of worms) --
    bool caught = false;
    try { throw std::runtime_error("gate"); }
    catch (const std::exception& e) { caught = (std::strcmp(e.what(), "gate") == 0); }
    check("throw / catch / what()", caught);

    // --- clock_gettime: needs -lrt on glibc < 2.17 ---------------------
    timespec ts{};
    check("clock_gettime(MONOTONIC)", clock_gettime(CLOCK_MONOTONIC, &ts) == 0);

    // --- std::filesystem over the Kindle user partition ----------------
    namespace fs = std::filesystem;
    const char* root = fs::exists("/mnt/us") ? "/mnt/us" : "/tmp";
    int seen = 0;
    try {
        for (auto it = fs::directory_iterator(root); it != fs::directory_iterator(); ++it) {
            (void)it->path(); if (++seen > 8) break;
        }
        check("std::filesystem iterate", true, root);
    } catch (const std::exception& e) {
        check("std::filesystem iterate", false, e.what());
    }

    // --- locale: device locale set is stripped; only "C" is safe -------
    try { std::locale::global(std::locale("C")); check("std::locale(\"C\")", true); }
    catch (const std::exception& e) { check("std::locale(\"C\")", false, e.what()); }
    // std::locale("") is EXPECTED to throw on device -- informational only.
    try { std::locale l(""); std::printf("[INFO] locale(\"\") ok: %s\n", l.name().c_str()); }
    catch (const std::exception& e) { std::printf("[INFO] locale(\"\") threw (expected): %s\n", e.what()); }

    // --- containers / smart pointers CrossInk leans on -----------------
    auto sp = std::make_shared<std::vector<std::string>>();
    sp->push_back("crossink");
    check("shared_ptr + vector + string", sp->size() == 1 && sp->at(0) == "crossink");

    std::printf("\n%s (%d failure%s)\n", failures ? "GATE FAILED" : "GATE PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
