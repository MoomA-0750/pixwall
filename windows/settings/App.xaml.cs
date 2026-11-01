using System.Threading;
using Microsoft.UI.Xaml;

namespace Pixwall.Settings;

public partial class App : Application
{
    private Window? window;
    // 設定画面は1つだけ開く。2つ目は、開いている方を前に出して終わる
    private static Mutex? single;

    public App()
    {
        // 試すときに、Windows の表示言語を変えずに別の言語で開く（例 PIXWALL_LANGUAGE=en-US）
        if (System.Environment.GetEnvironmentVariable("PIXWALL_LANGUAGE") is { Length: > 0 } language)
        {
            Microsoft.Windows.Globalization.ApplicationLanguages.PrimaryLanguageOverride = language;
        }
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        single = new Mutex(true, @"Local\pixwall-settings", out var first);
        if (!first)
        {
            Native.BringToFront(Strings.Get("WindowTitle"));
            Exit();
            return;
        }
        window = new MainWindow();
        window.Activate();
    }
}
