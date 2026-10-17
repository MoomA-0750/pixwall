/// GIF に書かれた待ち時間（秒）を、実際に待つ時間にする。
/// 10ms 以下はブラウザと同じく 100ms。速さの倍率は 0.25〜4 に収める
public func frameDelay(_ written: Double, speed: Double = 1) -> Double {
    let delay = written <= 0.01 ? 0.1 : written
    return delay / min(max(speed, 0.25), 4)
}

/// 動かすか止めるかを決める材料
public struct PauseReasons: Equatable {
    /// その画面がほかの窓で全部隠れている
    public var occluded = false
    /// 画面が消えている
    public var screensAsleep = false
    /// ロック中・ユーザーの切り替え中
    public var locked = false
    /// 低電力モード
    public var lowPower = false
    /// 低電力モードのときに止めるか（設定。既定は止める）
    public var pauseOnLowPower = true

    public init() {}

    public var shouldPause: Bool {
        occluded || screensAsleep || locked || (lowPower && pauseOnLowPower)
    }
}
