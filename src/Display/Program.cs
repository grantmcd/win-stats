using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using WinRT;

namespace WinStats.Display;

internal static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        ComWrappersSupport.InitializeComWrappers();
        Application.Start(_ =>
        {
            var dispatcher = DispatcherQueue.GetForCurrentThread();
            SynchronizationContext.SetSynchronizationContext(new DispatcherQueueSynchronizationContext(dispatcher));
            new DisplayApp(args);
        });
        return Environment.ExitCode;
    }
}

internal sealed class DisplayApp : Application
{
    private readonly string[] args;
    private Window? window;
    private Session? session;
    private DispatcherTimer? timer;

    public DisplayApp(string[] args) => this.args = args;

    protected override async void OnLaunched(LaunchActivatedEventArgs e)
    {
        try
        {
            Resources.MergedDictionaries.Add(new XamlControlsResources());
            if (args.Length != 3 || !int.TryParse(args[1], out var pid) || !long.TryParse(args[2], out var hwnd))
            {
                Environment.ExitCode = 2;
                Exit();
                return;
            }
            session = new Session(pid, (nint)hwnd);
            if (args[0] == "--smoke-test")
            {
                if (session.Read() is not { Cpu: >= 0 and <= 100, Memory: >= 0 and <= 100, Total: > 0 } reading)
                    throw new System.IO.InvalidDataException("Missing live sample from native monitor.");
                var panel = new DetailsWindow();
                window = panel;
                panel.Update(reading);
                panel.Activate();
                await Task.Delay(250);
                panel.Close();
                session.Dispose();
                Exit();
                return;
            }
            var stripMode = args[0] == "--strip";
            Action<Reading> render;
            StripWindow? strip = null;
            if (stripMode)
            {
                strip = new StripWindow(session.OpenDetails);
                window = strip;
                render = strip.Update;
                strip.Recreate += () => Finish(21);
            }
            else
            {
                var details = new DetailsWindow();
                window = details;
                render = details.Update;
            }
            window.Closed += (_, _) => { timer?.Stop(); session.Dispose(); Exit(); };
            void Refresh()
            {
                if (session.ParentExited) { Finish(0); return; }
                if (session.Read() is not { } sample) return;
                if (stripMode && !sample.Strip) { Finish(0); return; }
                render(sample);
            }
            timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
            timer.Tick += (_, _) => Refresh();
            Refresh();
            if (finishing) return;
            timer.Start();
            if (strip != null) await strip.AttachAsync();
            if (!finishing) window.Activate();
        }
        catch (Exception error)
        {
            if (args.FirstOrDefault() == "--smoke-test")
            {
                // A GUI process started by the native monitor has no inherited console.
                var log = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "WinStats-SmokeTest.log");
                System.IO.File.WriteAllText(log, error.ToString());
            }
            Console.Error.WriteLine($"Display startup failed: {error.GetType().Name} (0x{error.HResult:X8}).");
            Finish(22);
        }
    }

    private bool finishing;
    private void Finish(int code)
    {
        if (finishing) return;
        finishing = true;
        Environment.ExitCode = code;
        timer?.Stop();
        // Explorer can destroy the hosted HWND before the recreation callback.
        // Disposing the host and ending this dispatcher also covers that case.
        if (window is StripWindow strip) strip.ReleaseHost();
        Exit();
    }
}
