using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;

namespace WinStats.Display;

internal readonly record struct Reading(bool Strip, int Metrics, int Cpu, int Memory,
    long Used, long Total, double Receive, double Transmit)
{
    public static string Percent(int n) => n < 0 ? "—" : $"{n}%";
    public static string Rate(double n) => n switch
    {
        < 0 => "—",
        < 1_000 => $"{n:0} B/s",
        < 1_000_000 => $"{n / 1_000:0.#} kB/s",
        < 1_000_000_000 => $"{n / 1_000_000:0.#} MB/s",
        _ => $"{n / 1_000_000_000:0.#} GB/s"
    };
}

internal sealed class Session : IDisposable
{
    private readonly Process parent;
    private readonly Mutex guard;
    private readonly MemoryMappedFile mapping;
    private readonly MemoryMappedViewAccessor view;
    private readonly nint monitorWindow;

    public Session(int pid, nint window)
    {
        parent = Process.GetProcessById(pid);
        monitorWindow = window;
        var prefix = $@"Local\WinStats.{pid}";
        guard = Mutex.OpenExisting(prefix + ".Guard");
        mapping = MemoryMappedFile.OpenExisting(prefix + ".Data", MemoryMappedFileRights.Read);
        view = mapping.CreateViewAccessor(0, 56, MemoryMappedFileAccess.Read);
    }

    public bool ParentExited => parent.HasExited;
    public void OpenDetails() => PostMessageW(monitorWindow, 0x8002, 0, 0);

    public Reading? Read()
    {
        try { if (!guard.WaitOne(20)) return null; }
        catch (AbandonedMutexException) { /* This thread now owns the mutex. */ }
        try
        {
            if (view.ReadInt32(0) != 1) return null;
            return new(view.ReadInt32(4) != 0, view.ReadInt32(8), view.ReadInt32(12),
                view.ReadInt32(16), view.ReadInt64(24), view.ReadInt64(32),
                view.ReadDouble(40), view.ReadDouble(48));
        }
        finally { guard.ReleaseMutex(); }
    }

    public void Dispose()
    {
        view.Dispose();
        mapping.Dispose();
        guard.Dispose();
        parent.Dispose();
    }

    [DllImport("user32.dll")]
    private static extern bool PostMessageW(nint window, uint message, nuint wParam, nint lParam);
}
