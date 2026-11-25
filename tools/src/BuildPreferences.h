#pragma once

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace trusscliBuild {
using namespace std;

inline string rememberedNativeBuildType(const nlohmann::json& vendor) {
    const auto saved = vendor.value("buildType", nlohmann::json());
    if (saved.is_string()) {
        const auto value = saved.get<string>();
        if (value == "Debug" || value == "Release" || value == "RelWithDebInfo") return value;
    }
    return "RelWithDebInfo";
}

// The --parallel argument for cmake --build. -j wins. Without it, jobs are
// limited by RAM (~1.5 GB per compile job) to prevent memory exhaustion on
// small machines (SBCs, 8 GB Macs), capped at CPU count + 2. Linux uses
// available RAM, macOS total RAM; elsewhere use CPU count + 2.
inline int parallelBuildJobs(int jobs = 0, bool quiet = true) {
    if (jobs > 0) {
        if (!quiet) cout << "  * Parallel jobs: " << jobs << " (-j)\n";
        return jobs;
    }
#if defined(__linux__) || defined(__APPLE__)
    const double bytesPerJob = 1.5 * 1024 * 1024 * 1024;
    double ramBytes = 0;
#ifdef __linux__
    ifstream meminfo("/proc/meminfo");
    string line;
    while (getline(meminfo, line)) {
        long availKB = 0;
        if (sscanf(line.c_str(), "MemAvailable: %ld", &availKB) == 1) {
            ramBytes = availKB * 1024.0;
            break;
        }
    }
#else
    int64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0) == 0) {
        ramBytes = (double)memsize;
    }
#endif
    if (ramBytes > 0) {
        int ramJobs = max(1, (int)(ramBytes / bytesPerJob));
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (cpus > 0 && ramJobs >= (int)cpus + 2) {
            ramJobs = (int)cpus + 2;
        } else {
            // RAM is the limiting factor
            if (!quiet) cout << "  * Parallel jobs limited to " << ramJobs
                 << " (" << fixed << setprecision(1) << ramBytes / (1024.0 * 1024 * 1024)
                 << " GB RAM, ~1.5 GB per job)\n";
        }
        return ramJobs;
    }
#endif
    return max(1u, std::thread::hardware_concurrency()) + 2;
}

inline string parallelBuildArg(int jobs) {
    return "--parallel=" + to_string(parallelBuildJobs(jobs, false));
}

} // namespace trusscliBuild
