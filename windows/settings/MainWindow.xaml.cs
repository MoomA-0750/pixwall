using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading.Tasks;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;
using Windows.Graphics.Imaging;
using Windows.Storage;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace Pixwall.Settings;

/// 設定画面。変えるとすぐ config.json に書いて、常駐側に読み直してもらう（Mac の設定画面と同じ）
public sealed partial class MainWindow : Window
{
    private Config config;
    /// 画面に今の設定を入れている間は、変化を書き戻さない
    private bool loading;

    public MainWindow()
    {
        InitializeComponent();
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        config = Config.Load();
        // 中身に合う大きさ（96dpi の大きさを、画面の倍率に合わせる）
        var scale = GetDpiScale();
        AppWindow.Resize(new SizeInt32((int)(560 * scale), (int)(980 * scale)));
        if (AppWindow.Presenter is OverlappedPresenter p) p.IsMaximizable = false;
        Show();
    }

    private double GetDpiScale() => Native.ForWindow(WindowNative.GetWindowHandle(this)) / 96.0;

    private void Show()
    {
        loading = true;
        ImageName.Text = config.Image is null ? "選んでいない" : Path.GetFileName(config.Image);
        foreach (ComboBoxItem item in FitBox.Items)
        {
            if ((string)item.Tag == config.Fit) FitBox.SelectedItem = item;
        }
        SpeedSlider.Value = Math.Round(Math.Log2(config.Speed));
        SpeedLabel.Text = SpeedText(config.Speed);
        LowPowerSwitch.IsOn = config.PauseOnLowPower;
        LoginSwitch.IsOn = config.LaunchAtLogin;
        PauseSwitch.IsOn = Resident.IsPaused;
        loading = false;
        _ = ShowPreview(config.Image);
    }

    private void Apply(Config next)
    {
        if (loading || next == config) return;
        config = next;
        config.Save();
        Resident.ReloadConfig();
    }

    private static string SpeedText(double s) => s < 1 ? $"{s:0.##}倍" : $"{s:0}倍";

    /// 1枚目を、ドットがぼやけないよう最近傍で整数倍に広げて出す
    private async Task<bool> ShowPreview(string? path)
    {
        Preview.Visibility = Visibility.Collapsed;
        if (path is null) return false;
        try
        {
            var file = await StorageFile.GetFileFromPathAsync(path);
            using var stream = await file.OpenReadAsync();
            var decoder = await BitmapDecoder.CreateAsync(stream);
            var w = decoder.OrientedPixelWidth;
            var h = decoder.OrientedPixelHeight;
            var target = 160 * GetDpiScale();
            var k = Math.Max(1, (uint)Math.Floor(target / Math.Max(w, h)));
            var transform = new BitmapTransform
            {
                ScaledWidth = w * k,
                ScaledHeight = h * k,
                InterpolationMode = BitmapInterpolationMode.NearestNeighbor,
            };
            var pixels = await decoder.GetPixelDataAsync(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied,
                transform, ExifOrientationMode.RespectExifOrientation, ColorManagementMode.DoNotColorManage);
            var bitmap = new WriteableBitmap((int)(w * k), (int)(h * k));
            using (var buffer = bitmap.PixelBuffer.AsStream())
            {
                var bytes = pixels.DetachPixelData();
                await buffer.WriteAsync(bytes, 0, bytes.Length);
            }
            Preview.Source = bitmap;
            // 物理ピクセルでそのままの大きさに出す
            Preview.Width = w * k / GetDpiScale();
            Preview.Height = h * k / GetDpiScale();
            Preview.Visibility = Visibility.Visible;
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or COMException)
        {
            return false;
        }
    }

    private void Refresh_Click(object sender, RoutedEventArgs e) => Resident.Refresh();

    private async void ChooseImage_Click(object sender, RoutedEventArgs e)
    {
        var picker = new FileOpenPicker();
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        foreach (var ext in new[] { ".gif", ".png", ".webp" }) picker.FileTypeFilter.Add(ext);
        var file = await picker.PickSingleFileAsync();
        if (file is null) return;
        // 読めない画像は設定に書かない。壁紙はそのまま
        if (!await ShowPreview(file.Path))
        {
            LoadError.Text = $"読めませんでした: {file.Path}";
            LoadError.Visibility = Visibility.Visible;
            _ = ShowPreview(config.Image);
            return;
        }
        LoadError.Visibility = Visibility.Collapsed;
        ImageName.Text = file.Name;
        Apply(config with { Image = file.Path });
    }

    private void Fit_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (FitBox.SelectedItem is ComboBoxItem item) Apply(config with { Fit = (string)item.Tag });
    }

    private void Speed_Changed(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (SpeedLabel is null) return;
        var speed = Math.Pow(2, Math.Round(e.NewValue));
        SpeedLabel.Text = SpeedText(speed);
        Apply(config with { Speed = speed });
    }

    private void LowPower_Toggled(object sender, RoutedEventArgs e) => Apply(config with { PauseOnLowPower = LowPowerSwitch.IsOn });

    private void Login_Toggled(object sender, RoutedEventArgs e) => Apply(config with { LaunchAtLogin = LoginSwitch.IsOn });

    private void Pause_Toggled(object sender, RoutedEventArgs e)
    {
        if (!loading) Resident.SetPaused(PauseSwitch.IsOn);
    }

    private void Quit_Click(object sender, RoutedEventArgs e)
    {
        Resident.Quit();
        Close();
    }
}
