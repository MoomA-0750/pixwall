// gnome-extension/tests/run.js が JS の計算と突き合わせるための、core の Layout・UncoveredFraction・FrameDelay の結果。
// 1行に「layout 画像の幅 高さ 画面の幅 高さ fit position freeX freeY freeSize」→「x y 幅 高さ」、
// 「cover 対象の x y 幅 高さ 窓の数 (x y 幅 高さ)…」→ 割合、「delay 書かれた値 速さ」→ 秒
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core.h"

using namespace pixwall;

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string kind;
        in >> kind;
        if (kind == "layout") {
            int iw, ih, sw, sh;
            std::string fit;
            Placement p;
            in >> iw >> ih >> sw >> sh >> fit >> p.position >> p.freeX >> p.freeY >> p.freeSize;
            p.fit = fit == "fit" ? Fit::Fit : fit == "fill" ? Fit::Fill : fit == "free" ? Fit::Free : Fit::Integer;
            const PixelRect r = Layout(iw, ih, sw, sh, p);
            std::printf("%d %d %d %d\n", r.x, r.y, r.width, r.height);
        } else if (kind == "cover") {
            PixelRect t;
            int n = 0;
            in >> t.x >> t.y >> t.width >> t.height >> n;
            std::vector<PixelRect> rects(n);
            for (auto& r : rects) in >> r.x >> r.y >> r.width >> r.height;
            std::printf("%.12f\n", UncoveredFraction(t, rects));
        } else if (kind == "delay") {
            double written, speed;
            in >> written >> speed;
            std::printf("%.12f\n", FrameDelay(written, speed));
        }
        std::fflush(stdout);
    }
}
