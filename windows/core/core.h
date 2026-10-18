// OS に触らない計算（並べ方・待ち時間・止める条件・GIF の重ね方）。
// Mac 版の PixwallCore と同じ決まりで作り、Linux の g++ でも単体テストできるようにする
#pragma once

#include <cstdint>
#include <vector>

namespace pixwall {

// 並べ方。設定の JSON にもこの名前で書く（integer / fit / fill）
enum class Fit { Integer, Fit, Fill };

// 物理ピクセルでの大きさと位置。画面の左上が原点
struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool operator==(const PixelRect&) const = default;
};

// 画像を画面のどこにどの大きさで置くかを、物理ピクセルで決める。
// 画面の中央に置く。Fill では画面より大きくなり、x・y が負になる。
// Integer で整数倍に収まらない大きな画像は Fit と同じ。
// Fit のときだけ、余白の中のどこに置くかを position で決める（0 で上・左、0.5 で中央、1 で下・右）
PixelRect Layout(int imageWidth, int imageHeight, int screenWidth, int screenHeight, Fit fit,
                 double position = 0.5);

// 静止画を描く。frame（premultiplied の BGRA、imageWidth × imageHeight）を、canvas の大きさの絵に
// Layout で置き、最近傍で広げる（ドットをぼかさない）。余白と透明なところは background（r, g, b）。
// 返すのは不透明な BGRA（canvasWidth * canvasHeight * 4 バイト）。OS の壁紙と「静止画を書き出す」に使う
std::vector<uint8_t> RenderStill(const uint8_t* frame, int imageWidth, int imageHeight,
                                 int canvasWidth, int canvasHeight, Fit fit,
                                 uint8_t r, uint8_t g, uint8_t b, double position = 0.5);

// GIF に書かれた待ち時間（秒）を、実際に待つ時間にする。
// 10ms 以下はブラウザと同じく 100ms。速さの倍率は 0.25〜4 に収める
double FrameDelay(double written, double speed = 1);

// 動かすか止めるかを決める材料
struct PauseReasons {
    bool occluded = false;        // その画面がほかの窓で全部隠れている
    bool displayOff = false;      // 画面が消えている
    bool locked = false;          // ロック中・ユーザーの切り替え中
    bool energySaver = false;     // 省エネモード
    bool pauseOnEnergySaver = true;  // 省エネモードのときに止めるか（設定。既定は止める）
    bool userPaused = false;      // 設定画面の「一時停止」

    bool ShouldPause() const {
        return userPaused || occluded || displayOff || locked || (energySaver && pauseOnEnergySaver);
    }
};

// GIF のフレームを重ねて、元の大きさの完成した絵にする。
// 画素は premultiplied の BGRA（Direct2D と WIC の 32bppPBGRA と同じ並び）
class GifCompositor {
public:
    // GIF の Disposal Method
    enum class Disposal : uint8_t { None = 0, Keep = 1, Background = 2, Previous = 3 };

    struct SubFrame {
        int left = 0;
        int top = 0;
        int width = 0;
        int height = 0;
        Disposal disposal = Disposal::None;
        const uint8_t* pixels = nullptr;  // width * height * 4 バイト、行の間に隙間なし
    };

    GifCompositor(int width, int height);

    // 1枚重ねて、できあがった絵（width * height * 4 バイト）を返す。
    // 返した後に、そのフレームの disposal を当てて次に備える
    std::vector<uint8_t> Add(const SubFrame& frame);

private:
    int width_;
    int height_;
    std::vector<uint8_t> canvas_;
};

}  // namespace pixwall
