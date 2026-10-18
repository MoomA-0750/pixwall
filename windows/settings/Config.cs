using System;
using System.IO;
using System.Linq;
using System.Text.Encodings.Web;
using System.Text.Json;

namespace Pixwall.Settings;

/// 設定（config.json）。Mac 版と常駐側（core/config.cpp）と同じ形で読み書きする。
/// 足りない項目・読めない値は既定の値にする
public sealed record Config
{
    public int Version { get; init; } = 1;
    /// 動かす画像のパス。null なら選んでいない
    public string? Image { get; init; }
    /// integer / fit / fill
    public string Fit { get; init; } = "integer";
    /// 画面に合わせる・埋めるときの位置（0 で上・左、0.5 で中央、1 で下・右）
    public double Position { get; init; } = 0.5;
    /// 余白の色の決め方。auto（画像の1枚目の左上の1ドット）/ custom（BackgroundColor）
    public string Background { get; init; } = "auto";
    /// 自分で選んだ余白の色（"#RRGGBB"）。まだ選んでいなければ null。auto にしても覚えておく
    public string? BackgroundColor { get; init; }
    /// 速さの倍率（0.25〜4）
    public double Speed { get; init; } = 1;
    public bool PauseOnLowPower { get; init; } = true;
    public bool LaunchAtLogin { get; init; } = true;

    public static string FilePath { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "pixwall", "config.json");

    /// 無い・壊れているときは既定の設定
    public static Config Load()
    {
        var c = new Config();
        try
        {
            // 先頭の BOM は飛ばす（Windows PowerShell 5 の Set-Content -Encoding UTF8 などが付ける）
            var bytes = File.ReadAllBytes(FilePath).AsMemory();
            if (bytes.Span.StartsWith((ReadOnlySpan<byte>)[0xEF, 0xBB, 0xBF])) bytes = bytes[3..];
            using var doc = JsonDocument.Parse(bytes);
            if (doc.RootElement.ValueKind != JsonValueKind.Object) return c;
            foreach (var p in doc.RootElement.EnumerateObject())
            {
                var v = p.Value;
                c = p.Name switch
                {
                    "version" when v.ValueKind == JsonValueKind.Number => c with { Version = v.GetInt32() },
                    "image" when v.ValueKind == JsonValueKind.String => c with { Image = v.GetString() },
                    "fit" when v.GetString() is "integer" or "fit" or "fill" => c with { Fit = v.GetString()! },
                    "position" when v.ValueKind == JsonValueKind.Number => c with { Position = Math.Clamp(v.GetDouble(), 0, 1) },
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
            w.WriteString("fit", Fit);
            if (Image is not null) w.WriteString("image", Image);
            w.WriteBoolean("launchAtLogin", LaunchAtLogin);
            w.WriteBoolean("pauseOnLowPower", PauseOnLowPower);
            w.WriteNumber("position", Position);
            w.WriteNumber("speed", Speed);
            w.WriteNumber("version", Version);
            w.WriteEndObject();
        }
        File.Move(temp, FilePath, overwrite: true);
    }
}
