using Microsoft.Windows.ApplicationModel.Resources;

namespace Pixwall.Settings;

/// 画面の文字。Strings/<言語>/Resources.resw から、Windows の表示言語に合わせて引く。
/// 日本語でも英語でもない言語では英語（csproj の DefaultLanguage）。XAML の文字は x:Uid で同じ表から引く
internal static class Strings
{
    private static readonly ResourceLoader loader = new();

    public static string Get(string name) => loader.GetString(name);

    /// {0} などを埋める
    public static string Format(string name, params object[] args) => string.Format(Get(name), args);
}
