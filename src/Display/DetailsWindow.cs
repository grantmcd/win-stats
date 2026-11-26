using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace WinStats.Display;

internal sealed class DetailsWindow : Window
{
    private readonly TextBlock cpu = new(), ram = new(), download = new(), upload = new();
    private readonly ProgressBar cpuBar = new(), ramBar = new();
    private readonly TextBlock memoryDetail = new() { Opacity = 0.72, FontSize = 12 };

    public DetailsWindow()
    {
        Title = "Win Stats";
        SystemBackdrop = new MicaBackdrop();
        var body = new StackPanel { Spacing = 16, Padding = new Thickness(24) };
        body.Children.Add(new TextBlock { Text = "Live metrics", FontSize = 28,
            Style = (Style)Application.Current.Resources["TitleTextBlockStyle"] });
        body.Children.Add(Card("CPU", cpu, cpuBar));
        body.Children.Add(Card("Memory", ram, ramBar, memoryDetail));
        body.Children.Add(Card("Network", download, upload));
        Content = new ScrollViewer { Content = body, HorizontalScrollMode = ScrollMode.Disabled };
        AppWindow.Resize(new Windows.Graphics.SizeInt32(420, 475));
    }

    private static Border Card(string name, TextBlock value, params UIElement[] content)
    {
        value.FontSize = 20;
        var panel = new StackPanel { Spacing = 6 };
        panel.Children.Add(new TextBlock { Text = name, FontSize = 13, Opacity = 0.72 });
        panel.Children.Add(value);
        foreach (var element in content) panel.Children.Add(element);
        return new Border { Child = panel, Padding = new Thickness(14, 10, 14, 12),
            CornerRadius = new CornerRadius(8), BorderThickness = new Thickness(1),
            Background = (Brush)Application.Current.Resources["CardBackgroundFillColorDefaultBrush"],
            BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"] };
    }

    public void Update(Reading r)
    {
        cpu.Text = Reading.Percent(r.Cpu);
        ram.Text = Reading.Percent(r.Memory);
        cpuBar.Value = Math.Max(0, r.Cpu);
        ramBar.Value = Math.Max(0, r.Memory);
        memoryDetail.Text = r.Memory < 0 ? "Unavailable" : $"{r.Used / 1073741824d:N1} of {r.Total / 1073741824d:N1} GiB in use";
        download.Text = $"↓  {Reading.Rate(r.Receive)}";
        upload.Text = $"↑  {Reading.Rate(r.Transmit)}";
    }
}
