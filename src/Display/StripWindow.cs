using Deskband11Lib.Core;
using Deskband11Lib.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace WinStats.Display;

internal sealed class StripWindow : Window
{
    private readonly TaskbarContentHost host;
    private readonly TaskbarContentHostOptions options = new()
    {
        PreferredWidth = 300,
        Placement = TaskbarContentPlacement.BeforeNotificationArea,
        LayoutRefreshInterval = TimeSpan.FromSeconds(1),
        AnimateLayoutChanges = false,
        HighRefreshRateMode = false
    };
    private readonly StackPanel items = new() { Spacing = 10, Orientation = Orientation.Horizontal,
        VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Center };
    private readonly TextBlock cpu = new(), ram = new(), network = new();
    private readonly Button button;
    private int mask;
    private bool disposed;
    public event Action? Recreate;

    public StripWindow(Action openDetails)
    {
        Title = "Win Stats taskbar strip";
        foreach (var metric in new[] { cpu, ram, network })
        {
            metric.FontSize = 12;
            metric.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
            items.Children.Add(metric);
        }
        button = new Button { Content = items, Padding = new Thickness(2, 0, 2, 0),
            BorderThickness = new Thickness(0), Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent),
            HorizontalAlignment = HorizontalAlignment.Stretch, VerticalAlignment = VerticalAlignment.Stretch };
        AutomationProperties.SetName(button, "Open live system metrics");
        button.Click += (_, _) => openDetails();
        var root = new Grid();
        root.Children.Add(button);
        Content = root;
        host = new TaskbarContentHost(this, root, options);
        host.TaskbarEdgeChanged += (_, _) => Layout();
        host.TaskbarWindowRecreationRequired += (_, _) => Recreate?.Invoke();
        Closed += (_, _) => ReleaseHost();
    }

    public Task AttachAsync() => host.AttachWhenLayoutReadyAsync();

    public void ReleaseHost()
    {
        if (disposed) return;
        disposed = true;
        host.Dispose();
    }

    private void Layout()
    {
        items.Orientation = host.IsTaskbarVertical ? Orientation.Vertical : Orientation.Horizontal;
        items.Spacing = host.IsTaskbarVertical ? 4 : 10;
    }

    public void Update(Reading r)
    {
        if (mask != r.Metrics)
        {
            mask = r.Metrics;
            cpu.Visibility = (mask & 1) != 0 ? Visibility.Visible : Visibility.Collapsed;
            ram.Visibility = (mask & 2) != 0 ? Visibility.Visible : Visibility.Collapsed;
            network.Visibility = (mask & 4) != 0 ? Visibility.Visible : Visibility.Collapsed;
            options.PreferredWidth = 14 + ((mask & 1) != 0 ? 66 : 0) +
                ((mask & 2) != 0 ? 70 : 0) + ((mask & 4) != 0 ? 166 : 0);
        }
        var separator = host.IsTaskbarVertical ? "\n" : " ";
        cpu.Text = $"CPU{separator}{Reading.Percent(r.Cpu)}";
        ram.Text = $"RAM{separator}{Reading.Percent(r.Memory)}";
        network.Text = $"↓{Reading.Rate(r.Receive)}{separator}↑{Reading.Rate(r.Transmit)}";
        ToolTipService.SetToolTip(button, $"CPU {Reading.Percent(r.Cpu)} · Memory {Reading.Percent(r.Memory)}\nDownload {Reading.Rate(r.Receive)} · Upload {Reading.Rate(r.Transmit)}");
    }
}
