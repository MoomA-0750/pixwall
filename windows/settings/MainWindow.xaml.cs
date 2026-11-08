using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
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
    /// 切り替え方の選択肢の並び（MainWindow.xaml の SwitchBox と同じ順）
    private static readonly string[] Switches = ["none", "time", "appearance"];
    /// どの画像と並べ方を編集しているか（切り替えないときは Main）
    private SlotRef editing;
    /// 編集している画像と並べ方
    private Slot slot => config.GetSlot(editing);
    /// 書き出す絵（何枚目か、0 から）
    private int frame;

    public MainWindow()
    {
        InitializeComponent();
        Title = Strings.Get("WindowTitle");
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        config = Config.Load();
        editing = InitialEditing(config);
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

    /// 開いたとき・切り替え方を変えたときは、今出しているものを編集する
    private static SlotRef InitialEditing(Config c) => c.SwitchBy switch
    {
        "time" when c.ActiveTimeIndex(Config.NowMinute) is >= 0 and var i => new SlotRef(SlotKind.Time, i),
        "appearance" => new SlotRef(Config.SystemIsDark ? SlotKind.Dark : SlotKind.Light),
        _ => new SlotRef(SlotKind.Main),
    };

    /// 常駐側が今出している画像と並べ方
    private Slot ActiveSlot => config.ActiveSlot(Config.NowMinute, Config.SystemIsDark);

    private void ApplySlot(Slot next) => Apply(config.WithSlot(editing, next));

    private double GetDpiScale() => Native.ForWindow(WindowNative.GetWindowHandle(this)) / 96.0;

    private void Show()
    {
        loading = true;
        // 時刻を消した・pixwallctl で変えたなどで、編集していたものが無くなったら今出しているものに移る
        if (editing.Kind == SlotKind.Time && editing.Index >= config.TimeSlots.Items.Length ||
            editing.Kind != SlotKind.Main && config.SwitchBy == "none" ||
            editing.Kind == SlotKind.Time && config.SwitchBy != "time" ||
            editing.Kind is SlotKind.Light or SlotKind.Dark && config.SwitchBy != "appearance")
        {
            editing = InitialEditing(config);
        }
        ShowSwitch();
        var s = slot;
        ImageName.Text = s.Image is null ? Strings.Get("NoImage") : Path.GetFileName(s.Image);
        FitBox.SelectedIndex = Array.IndexOf(Fits, s.Fit);
        SpeedSlider.Value = Math.Round(Math.Log2(config.Speed));
        SpeedLabel.Text = SpeedText(config.Speed);
        PositionSlider.Value = Math.Round(s.Position * 100);
        PositionLabel.Text = PositionText(s.Position);
        FreeSizeSlider.Value = Math.Round(s.FreeSize * 100);
        FreeXSlider.Value = Math.Round(s.FreeX * 100);
        FreeYSlider.Value = Math.Round(s.FreeY * 100);
        ShowFreeLabels(s);
        ShowFitCards(s.Fit);
        BackgroundBox.SelectedIndex = config.Background == "custom" ? 1 : 0;
        ShowBackgroundColor();
        LowPowerSwitch.IsOn = config.PauseOnLowPower;
        LoginSwitch.IsOn = config.LaunchAtLogin;
        KeepSwitch.IsOn = config.KeepWallpaperOnQuit;
        ShowQuitLabel();
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
        ExportCard.Visibility = slot.Image is not null && slot.Image == ActiveSlot.Image ? Visibility.Visible : Visibility.Collapsed;
        _ = ShowPreview(slot.Image);
    }

    /// 切り替え方と、時刻の一覧・ライトとダークのどちらを編集するかを出す。「画像」の見出しに、編集しているものを書く
    private void ShowSwitch()
    {
        SwitchBox.SelectedIndex = Array.IndexOf(Switches, config.SwitchBy);
        var time = config.SwitchBy == "time";
        TimeCards.Visibility = time ? Visibility.Visible : Visibility.Collapsed;
        AddTimeButton.Visibility = TimeCards.Visibility;
        AppearanceCard.Visibility = config.SwitchBy == "appearance" ? Visibility.Visible : Visibility.Collapsed;
        AppearanceButtons.SelectedIndex = editing.Kind == SlotKind.Dark ? 1 : 0;
        ImageHeadingText.Text = editing.Kind switch
        {
            SlotKind.Time => Strings.Format("ImageFrom", Config.FormatClock(config.TimeSlots.Items[editing.Index].Start)),
            SlotKind.Light => Strings.Get("ImageLight"),
            SlotKind.Dark => Strings.Get("ImageDark"),
            _ => Strings.Get("ImageMain"),
        };
        TimeCards.Children.Clear();
        if (!time) return;
        var active = config.ActiveTimeIndex(Config.NowMinute);
        var items = config.TimeSlots.Items;
        for (var i = 0; i < items.Length; i++) TimeCards.Children.Add(TimeCard(i, items[i], i == active));
    }

    /// 時刻の一覧の1行。選ぶ丸（画像の名前）・始まる時刻・今出しているか・消すボタン
    private Grid TimeCard(int i, TimeSlot t, bool active)
    {
        // スタイルは XAML の部品から写す（リソースを引いて型を変えると、AOT では落ちることがある）
        var card = new Grid { Style = AppearanceCard.Style };
        card.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        for (var k = 0; k < 3; k++) card.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var choose = new RadioButton
        {
            GroupName = "TimeSlots",
            Content = t.Slot.Image is null ? Strings.Get("NoImage") : Path.GetFileName(t.Slot.Image),
            IsChecked = editing == new SlotRef(SlotKind.Time, i),
            VerticalAlignment = VerticalAlignment.Center,
        };
        choose.Checked += (_, _) =>
        {
            if (loading) return;
            editing = new SlotRef(SlotKind.Time, i);
            Show();
        };
        card.Children.Add(choose);
        var picker = new TimePicker
        {
            ClockIdentifier = "24HourClock",
            MinuteIncrement = 1,
            SelectedTime = TimeSpan.FromMinutes(t.Start),
            VerticalAlignment = VerticalAlignment.Center,
        };
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(picker, Strings.Get("StartTime"));
        picker.SelectedTimeChanged += (_, e) =>
        {
            if (loading || e.NewTime is not { } time) return;
            var start = (int)time.TotalMinutes % (24 * 60);
            Apply(config with { TimeSlots = new(config.TimeSlots.Items.SetItem(i, config.TimeSlots.Items[i] with { Start = start })) });
            editing = new SlotRef(SlotKind.Time, i);
            // 選んでいる最中の部品を消さないよう、知らせが終わってから並べ直す
            DispatcherQueue.TryEnqueue(Show);
        };
        Grid.SetColumn(picker, 1);
        card.Children.Add(picker);
        if (active)
        {
            var now = new TextBlock
            {
                Text = Strings.Get("Now"),
                Style = SwitchHintText.Style,
                Foreground = SwitchHintText.Foreground,
                VerticalAlignment = VerticalAlignment.Center,
            };
            Grid.SetColumn(now, 2);
            card.Children.Add(now);
        }
        if (config.TimeSlots.Items.Length > 1)
        {
            var remove = new Button
            {
                Content = new FontIcon { Glyph = "\uE738", FontSize = 14 },
                VerticalAlignment = VerticalAlignment.Center,
            };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(remove, Strings.Get("RemoveTime"));
            ToolTipService.SetToolTip(remove, Strings.Get("RemoveTime"));
            remove.Click += (_, _) =>
            {
                Apply(config with { TimeSlots = new(config.TimeSlots.Items.RemoveAt(i)) });
                editing = new SlotRef(SlotKind.Time, Math.Min(i, config.TimeSlots.Items.Length - 1));
                Show();
            };
            Grid.SetColumn(remove, 3);
            card.Children.Add(remove);
        }
        return card;
    }

    private void Switch_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (loading || SwitchBox.SelectedIndex < 0 || config is null) return;
        Apply(config.SwitchTo(Switches[SwitchBox.SelectedIndex]));
        editing = InitialEditing(config);
        Show();
    }

    /// 時刻を1つ足す。今編集しているものを写し、一番遅い時刻の1時間後から始める
    private void AddTime_Click(object sender, RoutedEventArgs e)
    {
        var items = config.TimeSlots.Items;
        var start = ((items.IsEmpty ? 0 : items.Max(t => t.Start)) + 60) % (24 * 60);
        Apply(config with { TimeSlots = new(items.Add(new TimeSlot(start, slot))) });
        editing = new SlotRef(SlotKind.Time, config.TimeSlots.Items.Length - 1);
        Show();
    }

    private void Appearance_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (loading || AppearanceButtons.SelectedIndex < 0 || config is null) return;
        editing = new SlotRef(AppearanceButtons.SelectedIndex == 1 ? SlotKind.Dark : SlotKind.Light);
        Show();
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

    private void ShowFreeLabels(Slot c)
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
        // 今出している画像なら、常駐側に描いてもらう（GIF の重ね方と余白の色を当てた、壁紙と同じ絵になる）
        if (path == ActiveSlot.Image)
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
            _ = ShowPreview(slot.Image);
            return;
        }
        LoadError.Visibility = Visibility.Collapsed;
        ImageName.Text = file.Name;
        ApplySlot(slot with { Image = file.Path });
        Show();
        UpdateFrames();
    }

    private void Frame_Changed(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        // 画面を作っている途中（config を読む前）にも呼ばれる
        if (loading || FrameLabel is null || config is null) return;
        frame = (int)Math.Round(e.NewValue);
        FrameLabel.Text = Strings.Format("FrameOf", FrameSlider.Maximum + 1, frame + 1);
        _ = ShowPreview(slot.Image, frame);
    }

    /// 画面ごとに、その画面の大きさで書き出す。描くのは常駐側（壁紙と同じ並べ方・余白の色）
    private async void Export_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var picker = new FileSavePicker { SuggestedStartLocation = PickerLocationId.PicturesLibrary };
            InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
            var screen = DisplayArea.Primary.OuterBounds;
            picker.SuggestedFileName = $"{Path.GetFileNameWithoutExtension(slot.Image)}-{screen.Width}x{screen.Height}";
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
        if (loading || config is null) return;
        ApplySlot(slot with { Fit = Fits[FitBox.SelectedIndex] });
    }

    /// 自由に置くときの3本のつまみ。設定には 100 分の 1 に丸めて書く
    private void Free_Changed(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (FreeYLabel is null) return;
        var next = slot with
        {
            FreeSize = Math.Round(FreeSizeSlider.Value) / 100,
            FreeX = Math.Round(FreeXSlider.Value) / 100,
            FreeY = Math.Round(FreeYSlider.Value) / 100,
        };
        ShowFreeLabels(next);
        ApplySlot(next);
    }

    private void Position_Changed(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (PositionLabel is null) return;
        var position = Math.Round(e.NewValue) / 100;
        PositionLabel.Text = PositionText(position);
        ApplySlot(slot with { Position = position });
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
        if (mine == previewRequests) _ = ShowPreview(slot.Image, frame);
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

    private void Keep_Toggled(object sender, RoutedEventArgs e)
    {
        Apply(config with { KeepWallpaperOnQuit = KeepSwitch.IsOn });
        ShowQuitLabel();
    }

    /// 終了したときに OS の壁紙がどうなるか
    private void ShowQuitLabel() => QuitLabel.Text = Strings.Get(KeepSwitch.IsOn ? "QuitKeeps" : "QuitRestores");

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
