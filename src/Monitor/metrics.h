#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>

// Read by the UI under the session mutex. Rates are bytes per second.
struct Snapshot {
    uint32_t version = 1;
    uint32_t stripEnabled = 1;
    uint32_t visibleMetrics = 7;
    int32_t cpu = -1;
    int32_t memory = -1;
    uint32_t reserved = 0;
    uint64_t usedMemory = 0;
    uint64_t totalMemory = 0;
    double receive = -1;
    double transmit = -1;
};
static_assert(sizeof(Snapshot) == 56 && offsetof(Snapshot, receive) == 40);

class Sampler {
    using Clock = std::chrono::steady_clock;
    struct Traffic { uint64_t in, out; };
    std::unordered_map<uint64_t, Traffic> interfaces;
    Clock::time_point previousTime{};
    uint64_t previousIdle = 0, previousTotal = 0;

    static uint64_t ticks(FILETIME value) {
        return (uint64_t{value.dwHighDateTime} << 32) | value.dwLowDateTime;
    }

public:
    void reset() {
        interfaces.clear();
        previousTime = {};
        previousTotal = 0;
    }

    void sample(Snapshot& s) {
        FILETIME idle{}, kernel{}, user{};
        s.cpu = -1;
        if (GetSystemTimes(&idle, &kernel, &user)) {
            const auto total = ticks(kernel) + ticks(user);
            const auto idleNow = ticks(idle);
            if (previousTotal && total > previousTotal && idleNow >= previousIdle) {
                const auto busy = 1.0 - double(idleNow - previousIdle) / double(total - previousTotal);
                s.cpu = static_cast<int>(std::clamp(busy, 0.0, 1.0) * 100 + 0.5);
            }
            previousIdle = idleNow;
            previousTotal = total;
        }
        MEMORYSTATUSEX memory{sizeof(MEMORYSTATUSEX)};
        s.memory = -1;
        if (GlobalMemoryStatusEx(&memory)) {
            s.memory = static_cast<int>(memory.dwMemoryLoad);
            s.usedMemory = memory.ullTotalPhys - memory.ullAvailPhys;
            s.totalMemory = memory.ullTotalPhys;
        }

        const auto now = Clock::now();
        const double elapsed = std::chrono::duration<double>(now - previousTime).count();
        s.receive = s.transmit = -1;
        PMIB_IF_TABLE2 raw = nullptr;
        if (GetIfTable2(&raw) == NO_ERROR) {
            const std::unique_ptr<MIB_IF_TABLE2, decltype(&FreeMibTable)> table(raw, FreeMibTable);
            std::unordered_map<uint64_t, Traffic> current;
            uint64_t received = 0, sent = 0;
            bool comparable = false;
            for (ULONG i = 0; i < table->NumEntries; ++i) {
                const auto& row = table->Table[i];
                if (row.OperStatus != IfOperStatusUp || !row.InterfaceAndOperStatusFlags.HardwareInterface) continue;
                const auto key = row.InterfaceLuid.Value;
                current.emplace(key, Traffic{row.InOctets, row.OutOctets});
                const auto prev = interfaces.find(key);
                if (prev == interfaces.end() || row.InOctets < prev->second.in || row.OutOctets < prev->second.out) continue;
                received += row.InOctets - prev->second.in;
                sent += row.OutOctets - prev->second.out;
                comparable = true;
            }
            if (comparable && elapsed > 0 && elapsed < 10) {
                s.receive = received / elapsed;
                s.transmit = sent / elapsed;
            }
            interfaces = std::move(current);
        } else {
            interfaces.clear();
        }
        previousTime = now;
    }
};
