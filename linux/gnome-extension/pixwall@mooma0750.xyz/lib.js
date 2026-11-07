// シェルに触らない計算。Windows 版の core.cpp（Linux の常駐側も使う）の Layout・FrameDelay・UncoveredFraction と
// 同じ決まり。tests/run.js が C++ の結果と突き合わせる

export const FREE_SIZE_MIN = 0.1;
export const FREE_SIZE_MAX = 3;

const clamp = (v, lo, hi) => Math.min(Math.max(v, lo), hi);

function scaled(w, h, scale) {
    return [Math.max(1, Math.round(w * scale)), Math.max(1, Math.round(h * scale))];
}

// 画像を画面のどこにどの大きさで置くかを、物理ピクセルで決める（core.h の Layout）。
// placement は config.json と同じ名前の項目（fit・position・freeX・freeY・freeSize）を持つ
export function layout(imageWidth, imageHeight, screenWidth, screenHeight, placement) {
    const fit = placement.fit;
    if (imageWidth <= 0 || imageHeight <= 0 || screenWidth <= 0 || screenHeight <= 0)
        return {x: 0, y: 0, width: 0, height: 0};
    const sx = screenWidth / imageWidth;
    const sy = screenHeight / imageHeight;
    let width, height;
    if (fit === 'integer' && Math.min(sx, sy) >= 1) {
        const scale = Math.min(Math.trunc(screenWidth / imageWidth), Math.trunc(screenHeight / imageHeight));
        width = imageWidth * scale;
        height = imageHeight * scale;
    } else if (fit === 'fill') {
        [width, height] = scaled(imageWidth, imageHeight, Math.max(sx, sy));
    } else if (fit === 'free') {
        const size = clamp(placement.freeSize, FREE_SIZE_MIN, FREE_SIZE_MAX);
        [width, height] = scaled(imageWidth, imageHeight, Math.min(sx, sy) * size);
    } else {
        [width, height] = scaled(imageWidth, imageHeight, Math.min(sx, sy));
    }
    let px = placement.position;
    let py = placement.position;
    if (fit === 'integer')
        px = py = 0.5;
    if (fit === 'free') {
        px = placement.freeX;
        py = placement.freeY;
    }
    // 余白×位置を 0 の側へ切り捨てる
    const place = (slack, p) => Math.trunc(slack * clamp(p, 0, 1)) || 0;
    return {x: place(screenWidth - width, px), y: place(screenHeight - height, py), width, height};
}

// GIF に書かれた待ち時間（秒）を、実際に待つ時間（秒）にする。10ms 以下は 100ms
export function frameDelay(written, speed = 1) {
    const delay = written <= 0.01 ? 0.1 : written;
    return delay / clamp(speed, 0.25, 4);
}

function subtract(cover, rect, out) {
    const x0 = Math.max(rect.x, cover.x);
    const y0 = Math.max(rect.y, cover.y);
    const x1 = Math.min(rect.x + rect.width, cover.x + cover.width);
    const y1 = Math.min(rect.y + rect.height, cover.y + cover.height);
    if (x0 >= x1 || y0 >= y1) {
        out.push(rect);
        return;
    }
    if (y0 > rect.y)
        out.push({x: rect.x, y: rect.y, width: rect.width, height: y0 - rect.y});
    if (y1 < rect.y + rect.height)
        out.push({x: rect.x, y: y1, width: rect.width, height: rect.y + rect.height - y1});
    if (x0 > rect.x)
        out.push({x: rect.x, y: y0, width: x0 - rect.x, height: y1 - y0});
    if (x1 < rect.x + rect.width)
        out.push({x: x1, y: y0, width: rect.x + rect.width - x1, height: y1 - y0});
}

// target のうち、rects のどれにも覆われていない部分の面積の割合（0〜1）
export function uncoveredFraction(target, rects) {
    if (target.width <= 0 || target.height <= 0)
        return 0;
    let rest = [target];
    for (const cover of rects) {
        if (cover.width <= 0 || cover.height <= 0)
            continue;
        const next = [];
        for (const r of rest)
            subtract(cover, r, next);
        rest = next;
        if (rest.length === 0)
            return 0;
    }
    const left = rest.reduce((sum, r) => sum + r.width * r.height, 0);
    return left / (target.width * target.height);
}

// 並べた窓の間のすき間くらい（見える部分が 5% 以下）なら覆われたことにする
export function isCovered(target, rects, tolerance = 0.05) {
    return uncoveredFraction(target, rects) <= tolerance;
}

// pixwallctl --frames の出力を読む。{width, height, autoColor, delays, frames（Uint8Array の BGRA）}。形が違えば null
export function parseFrames(bytes) {
    if (bytes.length < 20 || String.fromCharCode(...bytes.subarray(0, 4)) !== 'PXWF')
        return null;
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const width = view.getUint32(4, true);
    const height = view.getUint32(8, true);
    const count = view.getUint32(12, true);
    const autoColor = view.getUint32(16, true);
    const frameSize = width * height * 4;
    let offset = 20;
    if (width === 0 || height === 0 || count === 0 || bytes.length !== offset + count * 8 + count * frameSize)
        return null;
    const delays = [];
    for (let i = 0; i < count; i++, offset += 8)
        delays.push(view.getFloat64(offset, true));
    const frames = [];
    for (let i = 0; i < count; i++, offset += frameSize)
        frames.push(bytes.subarray(offset, offset + frameSize));
    return {width, height, autoColor, delays, frames};
}

// pixwallctl --resolve の出力を読む。{next（分。-1 は切り替えない）, config}
export function parseResolved(text) {
    const newline = text.indexOf('\n');
    if (newline < 0)
        return null;
    try {
        return {next: parseInt(text.slice(0, newline)), config: JSON.parse(text.slice(newline + 1))};
    } catch {
        return null;
    }
}
