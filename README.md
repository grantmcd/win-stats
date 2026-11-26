# Win Stats

A Windows 11 taskbar monitor for CPU load, physical memory, and network traffic. The prototype targets **Windows 11 24H2 (build 26100) or later** on x64 and Arm64.

## Run the prototype

1. Open [Windows prototype builds](https://github.com/grantmcd/win-stats/actions/workflows/windows.yml) and select a successful run. Sign in to GitHub to download Actions artifacts.
2. Under **Artifacts**, download **WinStats-win-x64** for most Intel and AMD PCs, or **WinStats-win-arm64** for a Windows on Arm PC. Check **Settings → System → About → System type** if unsure.
3. Extract the **entire ZIP**, keeping the Display folder beside **WinStats.exe**, and double-click **WinStats.exe**. The package includes the application runtimes. Prototype executables are unsigned.

The strip appears in available space on the primary taskbar. Click it or the tray gauge to open live metrics. The gauge may start in the taskbar's hidden-icons menu. Right-click the gauge to choose CPU or memory for the icon, select the strip metrics, hide/show the strip, retry attachment, or exit. If space is tight, turn off a metric. Windows controls tray icon visibility.

Preferences are stored under `HKCU\Software\WinStats\Prototype`. Counters are local. Network rates sum active hardware interfaces and skip virtual interfaces. A dash means the sample is unavailable or still warming up. CPU is non-idle processor time measured with GetSystemTimes and can differ from Task Manager's frequency-adjusted utilization. On systems larger than one processor group it reflects the calling thread's group.

## Implementation

- **WinStats.exe** is a C++20 Win32 monitor. It samples once per second, owns the tray gauge, and keeps the latest sample in a per-process, session-local memory mapping guarded by a mutex.
- **Display/WinStats.Display.exe** is a self-contained .NET 10 / WinUI 3 executable. The monitor starts a strip host when enabled and opens a separate detail window on demand. Both read the same snapshot and exit when the monitor exits.
- Only the strip host uses Deskband11Lib. If Explorer requests recreation, the monitor starts a fresh host with bounded retries. A failed strip leaves the native gauge available. A crowded taskbar can clip or hide the strip.

## Build and verification

On Windows with the .NET 10 SDK and Visual Studio C++ desktop build tools (including the ARM64 tools for cross-compilation), run:

```powershell
./scripts/build.ps1 -Arch x64 -SmokeTest
./scripts/build.ps1 -Arch arm64
```

The output is in `out/win-<architecture>/`. GitHub Actions publishes the full portable directory for each architecture. The smoke tests sample real CPU and memory counters and open a WinUI details window. Interactive verification on a Windows desktop covers taskbar placement, mixed DPI, Explorer restart, crowded taskbars, and idle resource use. The prototype renders current samples; history charts and sensor monitoring are outside this first build.
