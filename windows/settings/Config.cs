using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.IO;
using System.Linq;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Threading;
using Microsoft.Win32;

namespace Pixwall.Settings;

/// 設定（config.json）。Mac 版と常駐側（core/config.cpp）と同じ形で読み書きする。
/// 足りない項目・読めない値は既定の値にする
public sealed record Config
{
    public int Version { get; init; } = 1;
    /// 動かす画像のパス。null なら選んでいない
    public string? Image { get; init; }
    /// integer / fit / fill / free
    public string Fit { get; init; } = "integer";
    /// 画面に合わせる・埋めるときの位置（0 で上・左、0.5 で中央、1 で下・右）
    public double Position { get; init; } = 0.5;
    /// 自由に置くときの横・縦の位置（0 で左・上の端、1 で右・下の端をそろえる）
    public double FreeX { get; init; } = 0.5;
    public double FreeY { get; init; } = 0.5;
    /// 自由に置くときの大きさ。画面に合わせたときの大きさを 1 とする（0.1〜3）
    public double FreeSize { get; init; } = 1;
    /// 余白の色の決め方。auto（画像の1枚目の左上の1ドット）/ custom（BackgroundColor）
    public string Background { get; init; } = "auto";
    /// 自分で選んだ余白の色（"#RRGGBB"）。まだ選んでいなければ null。auto にしても覚えておく
    public string? BackgroundColor { get; init; }
    /// 速さの倍率（0.25〜4）
    public double Speed { get; init; } = 1;
    public bool PauseOnLowPower { get; init; } = true;
    public bool LaunchAtLogin { get; init; } = true;
    /// 出す画像を何で切り替えるか。none / time / appearance
    public string SwitchBy { get; init; } = "none";
    /// 時刻で切り替えるときの、時刻ごとの画像と並べ方（書いた順）
    public SlotList TimeSlots { get; init; } = SlotList.Empty;
    /// ダーク・ライトで切り替えるときの画像と並べ方。null なら Image と並べ方を使う
    public Slot? LightSlot { get; init; }
    public Slot? DarkSlot { get; init; }

    /// 切り替えないときに出す画像と並べ方（上の階層の Image・Fit・Position・FreeX・FreeY・FreeSize）
    public Slot Main => new(Image, Fit, Position, FreeX, FreeY, FreeSize);

    public Config WithMain(Slot s) =>
        this with { Image = s.Image, Fit = s.Fit, Position = s.Position, FreeX = s.FreeX, FreeY = s.FreeY, FreeSize = s.FreeSize };

    /// 無いもの（番号が範囲の外・まだ選んでいないダーク・ライト）は Main
    public Slot GetSlot(SlotRef r) => r.Kind switch
    {
        SlotKind.Time when r.Index >= 0 && r.Index < TimeSlots.Items.Length => TimeSlots.Items[r.Index].Slot,
        SlotKind.Light => LightSlot ?? Main,
        SlotKind.Dark => DarkSlot ?? Main,
        _ => Main,
    };

    /// 番号が範囲の外なら何も変えない
    public Config WithSlot(SlotRef r, Slot s) => r.Kind switch
    {
        SlotKind.Time when r.Index >= 0 && r.Index < TimeSlots.Items.Length =>
            this with { TimeSlots = new(TimeSlots.Items.SetItem(r.Index, TimeSlots.Items[r.Index] with { Slot = s })) },
        SlotKind.Time => this,
        SlotKind.Light => this with { LightSlot = s },
        SlotKind.Dark => this with { DarkSlot = s },
        _ => WithMain(s),
    };

    /// 時刻で切り替えるときに、今（0 時からの分）出す TimeSlots の番号。無ければ -1。常駐側（core/config.cpp）と同じ決まり
    public int ActiveTimeIndex(int minute)
    {
        int best = -1, latest = -1;
        var items = TimeSlots.Items;
        for (var i = 0; i < items.Length; i++)
        {
            if (items[i].Start <= minute && (best < 0 || items[i].Start >= items[best].Start)) best = i;
            if (latest < 0 || items[i].Start >= items[latest].Start) latest = i;
        }
        return best >= 0 ? best : latest;
    }

    /// 今（0 時からの分・ダークモードか）出す画像と並べ方
    public Slot ActiveSlot(int minute, bool dark) => SwitchBy switch
    {
        "time" when ActiveTimeIndex(minute) is >= 0 and var i => TimeSlots.Items[i].Slot,
        "appearance" => (dark ? DarkSlot : LightSlot) ?? Main,
        _ => Main,
    };

    /// 切り替え方を変える。初めて切り替えるときは、今の画像と並べ方から始める（時刻は 6:00 と 18:00、ダーク・ライトは両方）
    public Config SwitchTo(string mode) => mode switch
    {
        "time" when TimeSlots.Items.IsEmpty =>
            this with { SwitchBy = mode, TimeSlots = new([new TimeSlot(6 * 60, Main), new TimeSlot(18 * 60, Main)]) },
        "appearance" => this with { SwitchBy = mode, LightSlot = LightSlot ?? Main, DarkSlot = DarkSlot ?? Main },
        _ => this with { SwitchBy = mode },
    };

    /// 今の時刻を 0 時からの分にする
    public static int NowMinute => DateTime.Now.Hour * 60 + DateTime.Now.Minute;

    /// Windows のモード（「設定 > 個人用設定 > 色」の「Windows モード」）がダークか。常駐側と同じ値を見る
    public static bool SystemIsDark =>
        Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
                          "SystemUsesLightTheme", 1) is int light && light == 0;

    /// "H:MM" か "HH:MM"（0:00〜23:59）を 0 時からの分にする。形が違えば null
    public static int? ParseClock(string s)
    {
        var parts = s.Split(':');
        if (parts.Length != 2 || parts[0].Length is < 1 or > 2 || parts[1].Length != 2 ||
            !parts.All(p => p.All(char.IsAsciiDigit))) return null;
        var h = int.Parse(parts[0]);
        var m = int.Parse(parts[1]);
        return h < 24 && m < 60 ? h * 60 + m : null;
    }

    public static string FormatClock(int minutes) => $"{minutes / 60:00}:{minutes % 60:00}";

    public static string FilePath { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "pixwall", "config.json");

    /// 無い・壊れているときは既定の設定
    public static Config Load()
    {
        var c = new Config();
        try
        {
            // 先頭の BOM は飛ばす（Windows PowerShell 5 の Set-Content -Encoding UTF8 などが付ける）
            // 読んでいる間も、常駐側が書いたファイルで入れ替えられるよう、消す・名前を変えるも許して開く
            byte[] all;
            using (var f = new FileStream(FilePath, FileMode.Open, FileAccess.Read,
                                          FileShare.ReadWrite | FileShare.Delete))
            using (var m = new MemoryStream())
            {
                f.CopyTo(m);
                all = m.ToArray();
            }
            var bytes = all.AsMemory();
            if (bytes.Span.StartsWith((ReadOnlySpan<byte>)[0xEF, 0xBB, 0xBF])) bytes = bytes[3..];
            using var doc = JsonDocument.Parse(bytes);
            if (doc.RootElement.ValueKind != JsonValueKind.Object) return c;
            foreach (var p in doc.RootElement.EnumerateObject())
            {
                var v = p.Value;
                // 画像と並べ方は、切り替える先の1つ（Slot）と同じ読み方をする
                if (Slot.Read(c.Main, p) is { } main)
                {
                    c = c.WithMain(main);
                    continue;
                }
                c = p.Name switch
                {
                    "version" when v.ValueKind == JsonValueKind.Number => c with { Version = v.GetInt32() },
                    "switchBy" when v.ValueKind == JsonValueKind.String && v.GetString() is "none" or "time" or "appearance" =>
                        c with { SwitchBy = v.GetString()! },
                    // 読めない1つは飛ばす
                    "timeSlots" when v.ValueKind == JsonValueKind.Array =>
                        c with { TimeSlots = new([.. v.EnumerateArray().Select(TimeSlot.Read).OfType<TimeSlot>()]) },
                    "lightSlot" => c with { LightSlot = Slot.ReadObject(v) },
                    "darkSlot" => c with { DarkSlot = Slot.ReadObject(v) },
                    "background" when v.GetString() is "auto" or "custom" => c with { Background = v.GetString()! },
                    "backgroundColor" when v.ValueKind == JsonValueKind.String =>
                        c with { BackgroundColor = ParseColor(v.GetString()!) is { } rgb ? FormatColor(rgb) : null },
                    "speed" when v.ValueKind == JsonValueKind.Number => c with { Speed = Math.Clamp(v.GetDouble(), 0.25, 4) },
                    "pauseOnLowPower" when v.ValueKind is JsonValueKind.True or JsonValueKind.False => c with { PauseOnLowPower = v.GetBoolean() },
                    "launchAtLogin" when v.ValueKind is JsonValueKind.True or JsonValueKind.False => c with { LaunchAtLogin = v.GetBoolean() },
                    _ => c,
                };
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException or InvalidOperationException)
        {
            return new Config();
        }
        return c;
    }

    /// "#RRGGBB" を 0xRRGGBB にする。形が違えば null
    public static int? ParseColor(string s) =>
        s.Length == 7 && s[0] == '#' && s[1..].All(Uri.IsHexDigit) ? Convert.ToInt32(s[1..], 16) : null;

    /// "#RRGGBB"（大文字）にする
    public static string FormatColor(int rgb) => $"#{rgb & 0xFFFFFF:X6}";

    /// キーを名前順に並べて書く（Mac 版・常駐側と同じ）。書きかけで壊れないよう、別のファイルに書いてから入れ替える
    public void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
        var temp = FilePath + ".tmp";
        using (var stream = File.Create(temp))
        using (var w = new Utf8JsonWriter(stream, new JsonWriterOptions
               {
                   Indented = true,
                   // 日本語のパスも \u にせず、そのまま書く（Mac 版と同じ）
                   Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
               }))
        {
            w.WriteStartObject();
            w.WriteString("background", Background);
            if (BackgroundColor is not null) w.WriteString("backgroundColor", BackgroundColor);
            if (DarkSlot is not null)
            {
                w.WritePropertyName("darkSlot");
                DarkSlot.Write(w, null);
            }
            w.WriteString("fit", Fit);
            w.WriteNumber("freeSize", FreeSize);
            w.WriteNumber("freeX", FreeX);
            w.WriteNumber("freeY", FreeY);
            if (Image is not null) w.WriteString("image", Image);
            w.WriteBoolean("launchAtLogin", LaunchAtLogin);
            if (LightSlot is not null)
            {
                w.WritePropertyName("lightSlot");
                LightSlot.Write(w, null);
            }
            w.WriteBoolean("pauseOnLowPower", PauseOnLowPower);
            w.WriteNumber("position", Position);
            w.WriteNumber("speed", Speed);
            w.WriteString("switchBy", SwitchBy);
            w.WriteStartArray("timeSlots");
            foreach (var t in TimeSlots.Items) t.Slot.Write(w, t.Start);
            w.WriteEndArray();
            w.WriteNumber("version", Version);
            w.WriteEndObject();
        }
        // ほかのプロセス（ウイルス対策など）が開いていると、入れ替えはアクセス拒否になる。
        // つまみを動かしている間は何度も書くので、少し待って何回か試し、だめでも落とさない（次に変えたときに書く）
        for (var i = 0; ; i++)
        {
            try
            {
                File.Move(temp, FilePath, overwrite: true);
                return;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException && i < 10)
            {
                Thread.Sleep(10);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                return;
            }
        }
    }
}

/// 1つの画像と、その並べ方・位置・大きさ。JSON では Config の上の階層と同じ名前の項目に書く
public sealed record Slot(string? Image, string Fit = "integer", double Position = 0.5, double FreeX = 0.5,
                          double FreeY = 0.5, double FreeSize = 1)
{
    /// Slot の項目なら読んで当てた Slot、ほかの項目なら null
    public static Slot? Read(Slot s, JsonProperty p)
    {
        var v = p.Value;
        var number = v.ValueKind == JsonValueKind.Number;
        return p.Name switch
        {
            "image" => s with { Image = v.ValueKind == JsonValueKind.String ? v.GetString() : null },
            "fit" => v.ValueKind == JsonValueKind.String && v.GetString() is "integer" or "fit" or "fill" or "free"
                ? s with { Fit = v.GetString()! } : s,
            "position" => number ? s with { Position = Math.Clamp(v.GetDouble(), 0, 1) } : s,
            "freeX" => number ? s with { FreeX = Math.Clamp(v.GetDouble(), 0, 1) } : s,
            "freeY" => number ? s with { FreeY = Math.Clamp(v.GetDouble(), 0, 1) } : s,
            "freeSize" => number ? s with { FreeSize = Math.Clamp(v.GetDouble(), 0.1, 3) } : s,
            _ => null,
        };
    }

    /// オブジェクトでなければ null
    public static Slot? ReadObject(JsonElement e)
    {
        if (e.ValueKind != JsonValueKind.Object) return null;
        var s = new Slot((string?)null);
        foreach (var p in e.EnumerateObject()) s = Read(s, p) ?? s;
        return s;
    }

    /// 名前順に書く（start は時刻のときだけ）
    public void Write(Utf8JsonWriter w, int? start)
    {
        w.WriteStartObject();
        w.WriteString("fit", Fit);
        w.WriteNumber("freeSize", FreeSize);
        w.WriteNumber("freeX", FreeX);
        w.WriteNumber("freeY", FreeY);
        if (Image is not null) w.WriteString("image", Image);
        w.WriteNumber("position", Position);
        if (start is { } m) w.WriteString("start", Config.FormatClock(m));
        w.WriteEndObject();
    }
}

/// 決めた時刻（0 時からの分）から出す画像
public sealed record TimeSlot(int Start, Slot Slot)
{
    /// 読めなければ null。時刻が読めなければ 0:00
    public static TimeSlot? Read(JsonElement e) =>
        Slot.ReadObject(e) is { } s
            ? new TimeSlot(e.TryGetProperty("start", out var v) && v.ValueKind == JsonValueKind.String &&
                           Config.ParseClock(v.GetString()!) is { } m ? m : 0, s)
            : null;
}

/// TimeSlot の並び。中身で比べる（record の == で、変わっていない設定を書かないため）
public sealed record SlotList(ImmutableArray<TimeSlot> Items)
{
    public static readonly SlotList Empty = new(ImmutableArray<TimeSlot>.Empty);

    public bool Equals(SlotList? other) => other is not null && Items.SequenceEqual(other.Items);

    public override int GetHashCode() => Items.Aggregate(0, (h, t) => HashCode.Combine(h, t));
}

public enum SlotKind { Main, Time, Light, Dark }

/// 設定画面で編集するもの（どの画像と並べ方か）
public readonly record struct SlotRef(SlotKind Kind, int Index = 0);
