// pixwall の GNOME 版の常駐側。GNOME（Mutter）はほかのアプリの窓を背景に置かせないので、シェルの中で描く。
// 画面ごとに、シェルの背景の上（窓の下）に画像の面を置き、フレームを GPU に置いて、絵が変わる瞬間だけ差し替える。
// 設定を読む・画像を読む重い計算は pixwallctl（--resolve・--frames）に任せ、シェルの中では並べる計算だけをする
import Clutter from 'gi://Clutter';
import Cogl from 'gi://Cogl';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GnomeDesktop from 'gi://GnomeDesktop?version=4.0';
import Meta from 'gi://Meta';
import St from 'gi://St';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

import {frameDelay, isCovered, layout, parseFrames, parseResolved} from './lib.js';

const BUS_NAME = 'xyz.mooma0750.pixwall';
const BUS_PATH = '/xyz/mooma0750/pixwall';
// linux/src/bus.cpp と同じ形
const BUS_XML = `<node>
  <interface name="xyz.mooma0750.pixwall">
    <method name="Reload"/>
    <method name="Refresh"/>
    <method name="SetPaused"><arg type="b" name="paused" direction="in"/></method>
    <method name="IsPaused"><arg type="b" name="paused" direction="out"/></method>
    <method name="Quit"/>
  </interface>
</node>`;

const POWER_XML = `<node>
  <interface name="net.hadess.PowerProfiles">
    <property name="ActiveProfile" type="s" access="read"/>
  </interface>
</node>`;
const PowerProxy = Gio.DBusProxy.makeProxyWrapper(POWER_XML);

// pixwallctl の場所。シェルの PATH に ~/.local/bin が無いこともある
function findCtl() {
    return GLib.find_program_in_path('pixwallctl') ??
        GLib.build_filenamev([GLib.get_home_dir(), '.local', 'bin', 'pixwallctl']);
}

function run(argv) {
    return new Promise((resolve, reject) => {
        const proc = Gio.Subprocess.new(argv, Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_SILENCE);
        proc.communicate_async(null, null, (p, result) => {
            try {
                const [, stdout] = p.communicate_finish(result);
                resolve(p.get_successful() ? stdout.toArray() : null);
            } catch (e) {
                reject(e);
            }
        });
    });
}

// unix（秒）を渡すとその時刻の、渡さなければ今の、0 時からの分
function nowMinute(unix = null) {
    const now = unix === null ? GLib.DateTime.new_now_local() : GLib.DateTime.new_from_unix_local(unix);
    return now.get_hour() * 60 + now.get_minute();
}

class Wall {
    constructor(extension) {
        this._extension = extension;
        this._ctl = findCtl();
        this._interfaceSettings = new Gio.Settings({schema_id: 'org.gnome.desktop.interface'});
        this._config = null;
        this._next = -1;
        this._switchAt = 0;      // 次に切り替わる時刻（UNIX 秒）。0 なら切り替えない
        this._imagePath = null;
        this._image = null;      // {width, height, autoColor, delays, contents}
        this._index = 0;
        this._timer = 0;
        this._userPaused = false;
        this._lowPower = false;
        this._covered = false;
        this._coverCheck = 0;
        this._generation = 0;
        this._monitors = [];
        this._signals = [];
        this._stillTimer = 0;
        this._stillRunning = false;
        this._stillAgain = false;

        // シェルの背景（Meta.BackgroundGroup）の中の一番上に置く。窓の下で、Mutter は背景として扱う
        this._group = new Meta.BackgroundGroup();
        this._backgroundGroup = Main.layoutManager._backgroundGroup;
        this._backgroundGroup.add_child(this._group);
        // 背景が作り直されたときも、上にいる
        this._connect(this._backgroundGroup, 'child-added', () => {
            this._backgroundGroup.set_child_above_sibling(this._group, null);
        });

        this._connect(Main.layoutManager, 'monitors-changed', () => this._build());
        this._connect(this._interfaceSettings, 'changed::color-scheme', () => this._reload());
        // 毎分の時計（パネルの時計と同じもの。新しく起きる回数を増やさない）。時間帯の変更も来る
        this._clock = new GnomeDesktop.WallClock();
        this._connect(this._clock, 'notify::clock', () => {
            // 時計の知らせは分の変わり目ちょうどか、わずかに早く来ることがあるので、2 秒の幅をみて、
            // 切り替わる時刻として選び直す（今の時刻で読むと、まだ前の分のことがある）
            const now = GLib.DateTime.new_now_utc().to_unix();
            if (this._switchAt && now >= this._switchAt - 2)
                this._reload(Math.max(now, this._switchAt));
        });
        this._connect(this._clock, 'notify::timezone', () => this._reload());
        // 覆われているか見直すきっかけ
        for (const [object, signal] of [
            [global.display, 'restacked'], [global.display, 'in-fullscreen-changed'],
            [global.workspace_manager, 'active-workspace-changed'],
            [global.window_manager, 'size-changed'], [global.window_manager, 'minimize'],
            [global.window_manager, 'unminimize'], [global.window_manager, 'map'],
            [global.window_manager, 'destroy'], [Main.overview, 'showing'], [Main.overview, 'hidden'],
            [Main.sessionMode, 'updated'], [global.display, 'grab-op-end'],
        ])
            this._connect(object, signal, () => this._queueCoverCheck());

        this._sleepSignal = Gio.DBus.system.signal_subscribe('org.freedesktop.login1',
            'org.freedesktop.login1.Manager', 'PrepareForSleep', '/org/freedesktop/login1', null,
            Gio.DBusSignalFlags.NONE, (c, s, p, i, n, params) => {
                // スリープから戻った
                if (!params.deepUnpack()[0])
                    this._reload();
            });
        try {
            this._power = new PowerProxy(Gio.DBus.system, 'net.hadess.PowerProfiles', '/net/hadess/PowerProfiles',
                (proxy, error) => {
                    if (!error)
                        this._updatePower();
                });
            this._power.connectObject('g-properties-changed', () => this._updatePower(), this);
        } catch {
            this._power = null;
        }

        this._dbus = Gio.DBusExportedObject.wrapJSObject(BUS_XML, this);
        this._dbus.export(Gio.DBus.session, BUS_PATH);
        this._busOwner = Gio.bus_own_name_on_connection(Gio.DBus.session, BUS_NAME,
            Gio.BusNameOwnerFlags.NONE, null, null);

        this._build();
        this._reload();
    }

    _connect(object, signal, callback) {
        this._signals.push([object, object.connect(signal, callback)]);
    }

    // ---- D-Bus で受ける頼みごと（linux/src/bus.h） ----

    Reload() {
        this._reload();
    }

    Refresh() {
        this._build();
    }

    SetPaused(paused) {
        this._userPaused = paused;
        this._schedule();
    }

    IsPaused() {
        return this._userPaused;
    }

    Quit() {
        // 拡張機能を止める（設定画面の「開始」で、また動かせる）
        GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            Main.extensionManager.disableExtension(this._extension.uuid);
            return GLib.SOURCE_REMOVE;
        });
    }

    // ---- 設定と画像 ----

    async _reload(at = null) {
        const generation = ++this._generation;
        const dark = this._interfaceSettings.get_string('color-scheme') === 'prefer-dark';
        const minute = nowMinute(at);
        let resolved = null;
        try {
            const out = await run([this._ctl, '--resolve', String(minute), dark ? '1' : '0']);
            resolved = out && parseResolved(new TextDecoder().decode(out));
        } catch (e) {
            console.warn(`pixwall: cannot run ${this._ctl}: ${e.message}`);
        }
        if (generation !== this._generation || !resolved)
            return;
        this._config = resolved.config;
        this._resolvedAt = {minute, dark};
        this._next = resolved.next;
        const base = at ?? GLib.DateTime.new_now_utc().to_unix();
        this._switchAt = this._next > 0 ? Math.floor(base / 60) * 60 + this._next * 60 : 0;
        const path = this._config.image ?? null;
        if (path !== this._imagePath)
            await this._loadImage(path, generation);
        if (generation !== this._generation)
            return;
        this._layoutAll();
        this._schedule();
        this._queueStill();
    }

    async _loadImage(path, generation) {
        let parsed = null;
        if (path) {
            try {
                const out = await run([this._ctl, '--frames', path]);
                parsed = out && parseFrames(out);
            } catch (e) {
                console.warn(`pixwall: cannot read ${path}: ${e.message}`);
            }
        }
        if (generation !== this._generation)
            return;
        this._imagePath = path;
        this._index = 0;
        if (!parsed) {
            this._image = null;
            return;
        }
        // フレームを GPU に置く。元のバイト列はここで手放す
        const cogl = global.stage.context.get_backend().get_cogl_context();
        const contents = parsed.frames.map(frame => {
            const content = St.ImageContent.new_with_preferred_size(parsed.width, parsed.height);
            content.set_bytes(cogl, new GLib.Bytes(frame), Cogl.PixelFormat.BGRA_8888_PRE,
                parsed.width, parsed.height, parsed.width * 4);
            return content;
        });
        this._image = {width: parsed.width, height: parsed.height, autoColor: parsed.autoColor,
            delays: parsed.delays, contents};
        for (const m of this._monitors)
            m.picture.set_content(contents[0]);
    }

    // ---- 画面ごとの面 ----

    _build() {
        for (const m of this._monitors)
            m.box.destroy();
        this._monitors = Main.layoutManager.monitors.map(monitor => {
            const box = new Clutter.Actor({clip_to_allocation: true, reactive: false});
            const picture = new Clutter.Actor({reactive: false});
            // ドットをぼかさない
            picture.set_content_scaling_filters(Clutter.ScalingFilter.NEAREST, Clutter.ScalingFilter.NEAREST);
            box.add_child(picture);
            this._group.add_child(box);
            return {monitor, box, picture};
        });
        if (this._image) {
            for (const m of this._monitors)
                m.picture.set_content(this._image.contents[this._index]);
        }
        this._layoutAll();
        this._queueCoverCheck();
        // 設定を読み終える前（起動したばかり）は、読み終えてから
        if (this._config)
            this._queueStill();
    }

    _background() {
        const c = this._config;
        if (c?.background === 'custom' && /^#[0-9a-fA-F]{6}$/.test(c.backgroundColor ?? ''))
            return parseInt(c.backgroundColor.slice(1), 16);
        return this._image?.autoColor ?? 0;
    }

    _layoutAll() {
        const shown = !!this._image;
        const rgb = this._background();
        const [, color] = Cogl.Color.from_string(`#${rgb.toString(16).padStart(6, '0')}ff`);
        for (const m of this._monitors) {
            const {x, y, width, height} = m.monitor;
            m.box.visible = shown;
            m.box.set_position(x, y);
            m.box.set_size(width, height);
            m.box.background_color = color;
            if (!shown)
                continue;
            // 物理ピクセルで並べ、論理の座標に戻す
            const scale = m.monitor.geometry_scale ?? 1;
            const r = layout(this._image.width, this._image.height,
                Math.round(width * scale), Math.round(height * scale), this._config);
            m.picture.set_position(r.x / scale, r.y / scale);
            m.picture.set_size(r.width / scale, r.height / scale);
        }
    }

    // ---- GNOME の壁紙 ----

    // GNOME の壁紙にも今の画像の1枚目を入れる（概観・ロック画面・壁紙をぼかすパネルでも同じ絵に）。
    // 描くのは pixwallctl。並べ方を続けて変えている間は待ち、変え終わって 0.5 秒してから1回だけ
    _queueStill() {
        if (this._stillTimer)
            GLib.source_remove(this._stillTimer);
        this._stillTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 500, () => {
            this._stillTimer = 0;
            this._writeStill();
            return GLib.SOURCE_REMOVE;
        });
    }

    async _writeStill() {
        if (this._stillRunning) {
            this._stillAgain = true;
            return;
        }
        this._stillRunning = true;
        const monitor = Main.layoutManager.primaryMonitor;
        // 今出している設定を選んだときと同じ時刻・外観で選ぶ
        const {minute, dark} = this._resolvedAt;
        const argv = [this._ctl, '--gnome-wallpaper', String(minute), dark ? '1' : '0'];
        if (this._image && monitor) {
            const scale = monitor.geometry_scale ?? 1;
            argv.push(String(Math.round(monitor.width * scale)), String(Math.round(monitor.height * scale)));
        } else {
            // 出す画像が無い: 元の壁紙に戻す
            argv.splice(1, argv.length - 1, '--gnome-wallpaper-restore');
        }
        try {
            await run(argv);
        } catch (e) {
            console.warn(`pixwall: cannot run ${this._ctl}: ${e.message}`);
        }
        this._stillRunning = false;
        if (this._stillAgain && !this._destroyed) {
            this._stillAgain = false;
            this._writeStill();
        }
    }

    // ---- 動かす・止める ----

    _animating() {
        return this._image && this._image.contents.length > 1 && !this._userPaused && !this._covered &&
            !(this._lowPower && this._config?.pauseOnLowPower !== false);
    }

    _schedule() {
        if (this._timer) {
            GLib.source_remove(this._timer);
            this._timer = 0;
        }
        if (!this._animating())
            return;
        const delay = frameDelay(this._image.delays[this._index], this._config.speed ?? 1);
        this._timer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, Math.max(1, Math.round(delay * 1000)), () => {
            this._timer = 0;
            this._index = (this._index + 1) % this._image.contents.length;
            const content = this._image.contents[this._index];
            for (const m of this._monitors)
                m.picture.set_content(content);
            this._schedule();
            return GLib.SOURCE_REMOVE;
        });
    }

    _updatePower() {
        const low = this._power?.ActiveProfile === 'power-saver';
        if (low === this._lowPower)
            return;
        this._lowPower = low;
        this._schedule();
    }

    _queueCoverCheck() {
        if (this._coverCheck)
            return;
        this._coverCheck = GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => {
            this._coverCheck = 0;
            const covered = this._allCovered();
            if (covered !== this._covered) {
                this._covered = covered;
                this._schedule();
            }
            return GLib.SOURCE_REMOVE;
        });
    }

    // すべての画面がほかの窓で隠れているか。概観（Activities）とロック中は壁紙が見えないので隠れているとする
    _allCovered() {
        if (Main.overview.visible || Main.sessionMode.isLocked)
            return true;
        const workspace = global.workspace_manager.get_active_workspace();
        const windows = global.get_window_actors()
            .map(a => a.meta_window)
            .filter(w => w && w.showing_on_its_workspace() && !w.minimized && w.located_on_workspace(workspace) &&
                w.get_window_type() !== Meta.WindowType.DESKTOP);
        return this._monitors.length > 0 && this._monitors.every((m, i) => {
            if (global.display.get_monitor_in_fullscreen(i))
                return true;
            // パネルのまわりだけが見えていても、隠れていると扱う（作業領域で比べる）
            const target = Main.layoutManager.getWorkAreaForMonitor(i);
            const rects = windows.filter(w => w.get_monitor() === i).map(w => w.get_frame_rect());
            return isCovered(target, rects);
        });
    }

    destroy() {
        this._destroyed = true;
        this._generation++;
        if (this._stillTimer)
            GLib.source_remove(this._stillTimer);
        // 止めたら GNOME の壁紙を元に戻す。画面をロックしたとき（GNOME はロック中、拡張機能を止める）は戻さない
        // （ロック画面にも同じ絵を出し、ロックを解いたら描き直さずに続ける）。ログアウトでは止まらないので戻さない
        if (!Main.sessionMode.isLocked) {
            try {
                Gio.Subprocess.new([this._ctl, '--gnome-wallpaper-restore'], Gio.SubprocessFlags.NONE);
            } catch (e) {
                console.warn(`pixwall: cannot run ${this._ctl}: ${e.message}`);
            }
        }
        if (this._timer)
            GLib.source_remove(this._timer);
        if (this._coverCheck)
            GLib.source_remove(this._coverCheck);
        for (const [object, id] of this._signals)
            object.disconnect(id);
        this._power?.disconnectObject(this);
        Gio.DBus.system.signal_unsubscribe(this._sleepSignal);
        Gio.bus_unown_name(this._busOwner);
        this._dbus.unexport();
        this._group.destroy();
        this._monitors = [];
        this._image = null;
    }
}

export default class PixwallExtension extends Extension {
    enable() {
        this._wall = new Wall(this);
    }

    disable() {
        this._wall?.destroy();
        this._wall = null;
    }
}
