using System;
using System.Collections.Generic;
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
    /// 並べ方の選択肢の並び（MainWindow.xaml の FitBox と同じ順）
    private static readonly string[] Fits = ["integer", "fit", "fill", "free"];
    /// 書き出す絵（何枚目か、0 から）
    private int frame;

    public MainWindow()
    {
        InitializeComponent();
        Title = Strings.Get("WindowTitle");
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        config = Config.Load();
        // 中身に合う大きさ（96dpi の大きさを、画面の倍率に合わせる）
        var scale = GetDpiScale();
        AppWindow.Resize(new SizeInt32((int)(560 * scale), (int)(1100 * scale)));
        if (AppWindow.Presenter is OverlappedPresenter p) p.IsMaximizable = false;
        Show();
        // 開いている間に pixwallctl で変えたとき。古い設定のまま次に書いて、変えた分を消さないように読み直す。
        // 一時停止は config.json に無いので、前に出たときに見直す
        Directory.CreateDirectory(Path.GetDirectoryName(Config.FilePath)!);
        watcher = new FileSystemWatcher(Path.GetDirectoryName(Config.FilePath)!, Path.GetFileName(Config.FilePath))
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName,
        };
        watcher.Changed += (_, _) => ReloadSoon();
        watcher.Created += (_, _) => ReloadSoon();
        watcher.Renamed += (_, _) => ReloadSoon();
        watcher.EnableRaisingEvents = true;
        Closed += (_, _) => watcher.Dispose();
        Activated += (_, e) =>
        {
            if (e.WindowActivationState != WindowActivationState.Deactivated) Reload();
        };
    }

    private readonly FileSystemWatcher watcher;
    private int reloadRequests;

    /// 入れ替えの途中で読まないよう、知らせが止まってから少し待って読む
    private void ReloadSoon() => DispatcherQueue.TryEnqueue(async () =>
    {
        var mine = ++reloadRequests;
        await Task.Delay(100);
        if (mine == reloadRequests) Reload();
    });

    /// 自分で書いたあとにも呼ばれるが、そのときは config と同じなので、一時停止を見直すだけ
    private void Reload()
    {
        var disk = Config.Load();
        if (disk == config)
        {
            loading = true;
            PauseSwitch.IsOn = Resident.IsPaused;
            loading = false;
            return;
        }
        config = disk;
        Show();
    }

    private double GetDpiScale() => Native.ForWindow(WindowNative.GetWindowHandle(this)) / 96.0;

    private void Show()
    {
        loading = true;
        ImageName.Text = config.Image is null ? Strings.Get("NoImage") : Path.GetFileName(config.Image);
        FitBox.SelectedIndex = Array.IndexOf(Fits, config.Fit);
        SpeedSlider.Value = Math.Round(Math.Log2(config.Speed));
        SpeedLabel.Text = SpeedText(config.Speed);
        PositionSlider.Value = Math.Round(config.Position * 100);
        PositionLabel.Text = PositionText(config.Position);
        FreeSizeSlider.Value = Math.Round(config.FreeSize * 100);
        FreeXSlider.Value = Math.Round(config.FreeX * 100);
        FreeYSlider.Value = Math.Round(config.FreeY * 100);
        ShowFreeLabels(config);
        ShowFitCards(config.Fit);
        BackgroundBox.SelectedIndex = config.Background == "custom" ? 1 : 0;
        ShowBackgroundColor();
        LowPowerSwitch.IsOn = config.PauseOnLowPower;
        LoginSwitch.IsOn = config.LaunchAtLogin;
        PauseSwitch.IsOn = Resident.IsPaused;
        loading = false;
        UpdateFrames();
    }

    /// 動かしている画像のフレームの数に合わせて、書き出す絵のつまみを作り直す。1枚目に戻す
    private void UpdateFrames()
    {
        var count = Resident.FrameCount;
        frame = 0;
        loading = true;
        FrameSlider.Maximum = Math.Max(0, count - 1);
        FrameSlider.Value = 0;
        loading = false;
        FrameSlider.Visibility = count > 1 ? Visibility.Visible : Visibility.Collapsed;
        FrameLabel.Text = count > 1 ? Strings.Format("FrameOf", count, 1) : "";
        ExportButton.IsEnabled = count > 0;
        _ = ShowPreview(config.Image);
    }

    private void Apply(Config next)
    {
        if (loading || next == config) return;
        config = next;
        config.Save();
        Resident.ReloadConfig();
    }

    private static string SpeedText(double s) => Strings.Format("SpeedTimes", s < 1 ? $"{s:0.##}" : $"{s:0}");

    /// 位置は「画面に合わせる」「画面を埋める」のとき、大きさと横・縦の位置は「自由に置く」のときだけ出す
    private void ShowFitCards(string fit)
    {
        PositionCard.Visibility = fit is "fit" or "fill" ? Visibility.Visible : Visibility.Collapsed;
        FreeCards.Visibility = fit == "free" ? Visibility.Visible : Visibility.Collapsed;
    }

    private void ShowFreeLabels(Config c)
    {
        FreeSizeLabel.Text = $"{c.FreeSize * 100:0}%";
        FreeXLabel.Text = PositionText(c.FreeX);
        FreeYLabel.Text = PositionText(c.FreeY);
    }

    private static string PositionText(double p) => Math.Abs(p - 0.5) < 0.001 ? Strings.Get("Center") : $"{p * 100:0}";

    /// frame 枚目を、ドットがぼやけないよう最近傍で整数倍に広げて出す
    private async Task<bool> ShowPreview(string? path, int frame = 0)
    {
        Preview.Visibility = Visibility.Collapsed;
        if (path is null) return false;
        var source = path;
        // 動かしている画像なら、常駐側に描いてもらう（GIF の重ね方と余白の色を当てた、壁紙と同じ絵になる）
        if (path == config.Image)
        {
            var temp = Path.Combine(Path.GetTempPath(), "pixwall-preview.png");
            if (Resident.RenderPreview(frame, (int)(160 * GetDpiScale()), temp)) source = temp;
        }
        try
        {
            var file = await StorageFile.GetFileFromPathAsync(source);
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
            LoadError.Text = Strings.Format("CouldNotRead", file.Path);
            LoadError.Visibility = Visibility.Visible;
            _ = ShowPreview(config.Image);
            return;
        }
        LoadError.Visibility = Visibility.Collapsed;
        ImageName.Text = file.Name;
        Apply(config with { Image = file.Path });
        UpdateFrames();
    }

    private void Frame_Changed(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        // 画面を作っている途中（config を読む前）にも呼ばれる
        if (loading || FrameLabel is null || config is null) return;
        frame = (int)Math.Round(e.NewValue);
        FrameLabel.Text = Strings.Format("FrameOf", FrameSlider.Maximum + 1, frame + 1);
        _ = ShowPreview(config.Image, frame);
    }

    /// 画面ごとに、その画面の大きさで書き出す。描くのは常駐側（壁紙と同じ並べ方・余白の色）
    private async void Export_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var picker = new FileSavePicker { SuggestedStartLocation = PickerLocationId.PicturesLibrary };
            InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
            var screen = DisplayArea.Primary.OuterBounds;
            picker.SuggestedFileName = $"{Path.GetFileNameWithoutExtension(config.Image)}-{screen.Width}x{screen.Height}";
            picker.FileTypeChoices.Add("PNG", new List<string> { ".png" });
            var file = await picker.PickSaveFileAsync();
            if (file is null) return;
            ExportResult.Text = Resident.ExportStill(frame, file.Path)
                ? Strings.Format("Exported", file.Path)
                : Strings.Get("ExportFailedNotRunning");
        }
        catch (Exception ex) when (ex is COMException or InvalidCastException or ArgumentException)
        {
            ExportResult.Text = Strings.Format("ExportFailed", ex.Message);
        }
        ExportResult.Visibility = Visibility.Visible;
    }

    private void Fit_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (FitBox.SelectedIndex < 0) return;
        ShowFitCards(Fits[FitBox.SelectedIndex]);
        Apply(config with { Fit = Fits[FitBox.SelectedIndex] });
    }

    /// 自由に置くときの3本のつまみ。設定には 100 分の 1 に丸めて書く
    private void Free_Changed(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (FreeYLabel is null) return;
        var next = config with
        {
            FreeSize = Math.Round(FreeSizeSlider.Value) / 100,
            FreeX = Math.Round(FreeXSlider.Value) / 100,
            FreeY = Math.Round(FreeYSlider.Value) / 100,
        };
        ShowFreeLabels(next);
        Apply(next);
    }

    private void Position_Changed(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (PositionLabel is null) return;
        var position = Math.Round(e.NewValue) / 100;
        PositionLabel.Text = PositionText(position);
        Apply(config with { Position = position });
    }

    /// 自分で選ぶときだけ色のボタンを出し、今の色を見せる
    private void ShowBackgroundColor()
    {
        var custom = config.Background == "custom";
        ColorButton.Visibility = custom ? Visibility.Visible : Visibility.Collapsed;
        if (!custom || config.BackgroundColor is null || Config.ParseColor(config.BackgroundColor) is not { } rgb) return;
        var color = Windows.UI.Color.FromArgb(255, (byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb);
        ColorSwatch.Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(color);
        var was = loading;
        loading = true;
        BackgroundPicker.Color = color;
        loading = was;
    }

    private void Background_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (loading || BackgroundBox.SelectedIndex < 0 || config is null) return;
        var custom = BackgroundBox.SelectedIndex == 1;
        // 初めて自分で選ぶときは、今の自動の色から始める
        var color = config.BackgroundColor;
        if (custom && color is null) color = Config.FormatColor(Resident.AutoColor ?? 0);
        Apply(config with { Background = custom ? "custom" : "auto", BackgroundColor = color });
        ShowBackgroundColor();
        RefreshPreviewSoon();
    }

    private void Background_ColorChanged(ColorPicker sender, ColorChangedEventArgs e)
    {
        if (loading || config is null) return;
        var c = e.NewColor;
        Apply(config with { BackgroundColor = Config.FormatColor(c.R << 16 | c.G << 8 | c.B) });
        ColorSwatch.Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(c);
        RefreshPreviewSoon();
    }

    /// プレビューの透明なところにも余白の色が入るので、描き直す。常駐側が読み直してから頼むよう、少し待つ。
    /// 色を選ぶ画面を動かしている間は、止まるまで待つ
    private int previewRequests;

    private async void RefreshPreviewSoon()
    {
        var mine = ++previewRequests;
        await Task.Delay(300);
        if (mine == previewRequests) _ = ShowPreview(config.Image, frame);
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
