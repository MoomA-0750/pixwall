// 拡張機能の計算（lib.js）のテスト。linux/ で gjs -m gnome-extension/tests/run.js（make test から動かす）。
// C++ の core（build/layout_dump）と同じ答えになるかを、ばらばらな値でたくさん確かめる
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {layout, frameDelay, uncoveredFraction, parseFrames, parseResolved} from '../pixwall@mooma0750.xyz/lib.js';

let failures = 0;
const check = (ok, what) => {
    if (!ok) {
        failures++;
        printerr(`FAIL: ${what}`);
    }
};

// 決まった種から作る乱数（毎回同じ値で確かめる）
let seed = 12345;
const random = () => {
    seed = (seed * 1103515245 + 12345) % 2147483648;
    return seed / 2147483648;
};
const int = (lo, hi) => lo + Math.floor(random() * (hi - lo + 1));
const pick = list => list[int(0, list.length - 1)];

function run(argv, input) {
    const proc = Gio.Subprocess.new(argv, Gio.SubprocessFlags.STDIN_PIPE | Gio.SubprocessFlags.STDOUT_PIPE);
    const [, out] = proc.communicate_utf8(input, null);
    return out;
}

const cases = [];
const lines = [];
for (let i = 0; i < 3000; i++) {
    const placement = {
        fit: pick(['integer', 'fit', 'fill', 'free']),
        position: pick([0, 0.5, 1, random()]),
        freeX: pick([0, 0.5, 1, random()]),
        freeY: pick([0, 0.5, 1, random()]),
        freeSize: pick([0.1, 1, 3, 0.1 + random() * 2.9]),
    };
    const args = [int(1, 600), int(1, 600), int(1, 4000), int(1, 3000)];
    cases.push({kind: 'layout', args, placement});
    lines.push(`layout ${args.join(' ')} ${placement.fit} ${placement.position} ${placement.freeX} ${placement.freeY} ${placement.freeSize}`);
}
for (let i = 0; i < 1000; i++) {
    const target = {x: int(-100, 100), y: int(-100, 100), width: int(0, 500), height: int(0, 500)};
    const rects = [];
    for (let n = int(0, 6); n > 0; n--)
        rects.push({x: int(-200, 400), y: int(-200, 400), width: int(0, 500), height: int(0, 500)});
    cases.push({kind: 'cover', target, rects});
    lines.push(`cover ${target.x} ${target.y} ${target.width} ${target.height} ${rects.length} ${rects.map(r => `${r.x} ${r.y} ${r.width} ${r.height}`).join(' ')}`);
}
for (const written of [0, 0.01, 0.02, 0.1, 1.5])
    for (const speed of [0.1, 0.25, 1, 4, 8]) {
        cases.push({kind: 'delay', written, speed});
        lines.push(`delay ${written} ${speed}`);
    }

const answers = run(['build/layout_dump'], `${lines.join('\n')}\n`).trim().split('\n');
check(answers.length === cases.length, `layout_dump answered ${answers.length} of ${cases.length}`);
cases.forEach((c, i) => {
    const want = answers[i];
    if (c.kind === 'layout') {
        const r = layout(...c.args, c.placement);
        const got = `${r.x} ${r.y} ${r.width} ${r.height}`;
        check(got === want, `${lines[i]}: js ${got}, c++ ${want}`);
    } else if (c.kind === 'cover') {
        const got = uncoveredFraction(c.target, c.rects);
        check(Math.abs(got - parseFloat(want)) < 1e-9, `${lines[i]}: js ${got}, c++ ${want}`);
    } else {
        const got = frameDelay(c.written, c.speed);
        check(Math.abs(got - parseFloat(want)) < 1e-9, `${lines[i]}: js ${got}, c++ ${want}`);
    }
});

// pixwallctl の拡張機能向けの出力を読める
const dir = GLib.dir_make_tmp('pixwall-ext-XXXXXX');
const launcher = new Gio.SubprocessLauncher({flags: Gio.SubprocessFlags.STDOUT_PIPE});
launcher.setenv('XDG_CONFIG_HOME', dir, true);
const sample = GLib.canonicalize_filename('../samples/dot-64x64-30f.gif', null);
let proc = launcher.spawnv(['build/pixwallctl', '--frames', sample]);
let [, bytes] = proc.communicate(null, null);
const frames = parseFrames(bytes.toArray());
check(frames && frames.width === 64 && frames.height === 64 && frames.frames.length === 30 &&
    frames.delays.length === 30 && frames.frames[0].length === 64 * 64 * 4, 'parse --frames of the sample');
check(frames && frames.autoColor === 0xf05050, 'auto color of the sample');
check(parseFrames(new Uint8Array([1, 2, 3])) === null, 'junk is not frames');

proc = launcher.spawnv(['build/pixwallctl', 'set', 'image', sample, 'switchBy', 'time']);
proc.wait(null);
proc = launcher.spawnv(['build/pixwallctl', '--resolve', '300', '0']);
[, bytes] = proc.communicate(null, null);
const resolved = parseResolved(new TextDecoder().decode(bytes.toArray()));
// 6:00 と 18:00 の2つができ、5:00 なら前の日の 18:00 のもの。次は 1 時間後
check(resolved && resolved.next === 60 && resolved.config.image === sample && resolved.config.fit === 'integer',
    `parse --resolve: ${JSON.stringify(resolved)}`);
check(parseResolved('junk') === null, 'junk is not resolved');
GLib.spawn_command_line_sync(`rm -rf ${dir}`);

if (failures) {
    printerr(`${failures} failed`);
    imports.system.exit(1);
}
print('extension tests: all passed');
