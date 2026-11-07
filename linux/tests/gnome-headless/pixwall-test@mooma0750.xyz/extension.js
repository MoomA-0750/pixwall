// 試すときだけ使う: シェルの中で JS を評価する・画面を撮る（headless の試験用。本物の環境には入れない）
import Gio from 'gi://Gio';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
const XML = `<node><interface name="xyz.mooma0750.pixwallTest">
<method name="Eval"><arg type="s" direction="in"/><arg type="s" direction="out"/></method>
<method name="Shot"><arg type="s" direction="in"/></method></interface></node>`;
export default class extends Extension {
  enable() {
    this._obj = Gio.DBusExportedObject.wrapJSObject(XML, {
      Eval: code => { try { return String(eval(code)); } catch (e) { return 'ERROR ' + e; } },
      ShotAsync: ([path], inv) => {
        const shot = new Shell.Screenshot();
        const stream = Gio.File.new_for_path(path).replace(null, false, Gio.FileCreateFlags.NONE, null);
        shot.screenshot(false, stream, (s, res) => { try { s.screenshot_finish(res); stream.close(null); inv.return_value(null); } catch (e) { inv.return_dbus_error('x.Error', String(e)); } });
      },
    });
    globalThis.Main = Main;
    this._obj.export(Gio.DBus.session, '/xyz/mooma0750/pixwallTest');
    this._id = Gio.bus_own_name_on_connection(Gio.DBus.session, 'xyz.mooma0750.pixwallTest', 0, null, null);
  }
  disable() { this._obj.unexport(); Gio.bus_unown_name(this._id); }
}
