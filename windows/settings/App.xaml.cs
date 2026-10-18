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
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        single = new Mutex(true, @"Local\pixwall-settings", out var first);
        if (!first)
        {
            Native.BringToFront("pixwall の設定");
            Exit();
            return;
        }
        window = new MainWindow();
        window.Activate();
    }
}
