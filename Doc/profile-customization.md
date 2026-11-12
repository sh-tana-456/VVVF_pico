# プロファイルの追加と編集

全プリセットと登録一覧は **`src/profiles/presets.c`** にまとめている。構造体は`include/vvvf/profiles.h`、共通評価は`src/control/profiles.c`。車両名・出典文字列は動作を選択しない。新規追加はデータ配列と登録一覧の追記だけでよく、ID列挙や評価関数への分岐追加は不要。

## データの構成

| 構造体／項目 | 意味 |
|---|---|
| `vvvf_profile_definition_t` | 名前、別名、入力単位、振幅モード・上限、力行／減速の区間一覧、出典 |
| `vvvf_profile_segment_t` | 開始点`from`、波形、同期数、キャリア曲線、振幅曲線、角度係数 |
| `vvvf_curve_t` | 定数、直線、べき乗の曲線。区間の境界と曲線の終点は独立 |
| `vvvf_angle_law_t` | 広域3パルスの最大角度と指数 |

入力は`x = 基本波Hz / hz_per_unit`。Hz軸なら`hz_per_unit=1`、速度軸ならHz/(km/h)を指定する。同期キャリアは軸の単位によらず`fc = pulses × 基本波Hz`。

`from`は下端を含み、次の区間の下端を含まない。先頭は0、以降は昇順。`brake`を省略すれば減速にも`power`を使う。起動時に登録済み定義を検証し、不正な定義なら起動を止める。

## 追加例

`presets.c`内の既存マクロを利用できる。次の配列を登録一覧より前に置く。

```c
static const vvvf_profile_segment_t custom_power[]={
    {.from=0,.waveform=VVVF_SPWM,
     .carrier=LINEAR(0,800,40,1200),
     .amplitude=CLAMPED_LINEAR(0,0,80,1)},
    {.from=40,.waveform=VVVF_SPWM,.pulses=9,
     .amplitude=CLAMPED_LINEAR(0,0,80,1)},
    {.from=60,.waveform=VVVF_WIDE3,.pulses=3,
     .amplitude=CLAMPED_LINEAR(60,.7f,80,1),
     .angle_law={.maximum_degrees=60,.exponent=1}},
    {.from=80,.waveform=VVVF_ONE_PULSE,.pulses=1,
     .amplitude=CONSTANT(1)}
};
```

`vvvf_profile_definitions[]`の末尾に追加する。

```c
{.name="custom",.input_unit="Hz",.hz_per_unit=1,
 .amplitude_scale=1,.amplitude_mode=VVVF_RAW_REFERENCE,.amplitude_limit=1.4f,
 .power=SEQUENCE(custom_power),.source="自作設定"},
```

ビルド後にUSBで`stop`、`profile custom`を送る。既定にする場合は`include/vvvf/config.h`の`VVVF_DEFAULT_PROFILE_NAME`を`"custom"`へ変更する。現在の既定は`"jr-fall"`。

`CONSTANT(y)`は定数、`LINEAR(x0,y0,x1,y1)`は外挿も行う直線。`CLAMPED_LINEAR`は端点で入力を制限する。`CLAMPED_POWER(...,p)`は正規化入力tを0～1に制限し`y=y0+(y1-y0)*t^p`。区間切替点と曲線終点を同一にする必要はない。JR設定の48.9→49 Hzなどの差は意図的に保持している。

SPWMの`pulses=0`は非同期で`carrier`を使用する。正の値は通常の同期SPWM。`VVVF_WIDE3`は`pulses=3`、`VVVF_ONE_PULSE`は`pulses=1`を指定する。広域3パルスと1パルスは三角波キャリアを使わない。

`VVVF_RAW_REFERENCE`は曲線値をそのまま比較器／角度式の振幅mとして使う。`VVVF_VOLTAGE_TARGET`は従来V/f用で、通常SPWMの同期3パルスに基本波ゲイン補正を行う。両者のmは同じ実効電圧を意味しない。振幅は`amplitude_limit`までに制限され、USBの`m`固定もこの上限に従う。電圧目標の上限は1。広域3パルスではmを0～1に制限して角度を求める。

## 旧HITACHIとNAOTOの違い

これらは旧実装で角度式を区別するために使っていた名前で、モーター種別を選択する方式名ではない。現在は波形名を`VVVF_WIDE3`／ログ`wide3`へ統一し、違いをデータとして表す。

`α = maximum_degrees × (1 − clamp(m,0,1))^exponent`

| 現在のプリセット | 最大角度 | 指数 | 旧表記 |
|---|---:|---:|---|
| jr-fall | 60° | 1 | HITACHI |
| pmsm-toshiba | 38° | 0.72 | NAOTO |

たとえばm=0.7ならそれぞれ18°と約15.97°となり、補助パルス幅や高調波が異なる。U相High区間は`[α,180°−α)`、`[180°,180°+α)`、`[360°−α,360°)`。V/Wは120°ずつずらす。m=1ではα=0になり1パルスへ合流する。38°モデルはm=0でも電圧がゼロにならないため、低速からそのまま適用する前提の式ではない。

**PMSMだから38°／0.72の式が必須ということはない。** 今回は参照プリセットの音を再現するために採用している。60°／1などへ変更もできるが、音や基本波電圧が変わる。実PMSMの駆動に必要な電流制御・位置推定等はこのプロジェクトに含まれない。

作者名は波形名から除いたが、出典・MIT表示は帰属として保持する。JR参照元の許諾未確認事項も従来どおり。[出典と移植差分](preset-sources.md)、[ライセンス確認](license-audit.md)を参照。PMSM参照元の特殊同期9パルス、HFI、音響平滑化は未移植。

## 検証

`./tests/run.sh`で既存プリセットの公開設定3,321ケース、角度・振幅追従、独自プロファイルの単位換算・減速フォールバック・角度係数・不正設定を検証する。`cmake --build build -j4`でUF2を生成する。

2026-11-12: リファクタ後のUF2をRP2040へ書き込み・照合済み。USBで両プリセットを65 Hzにして、JRのα=6.750001°、PMSMのα=10.696334°、双方`wave=wide3`・`fault=0`を確認。生成処理の最大時間は1,205 µs（バッファ周期2,560 µs）。JR・0 Hzへ戻した。[USB実測ログ](profile-refactor-device-log.txt)。今回は波形の再取得は行わず、波形・角度の回帰は上記ホストテストで確認した。


## ランダムキャリアとモーター調整倍率

いずれも`src/profiles/presets.c`で設定する。IGBTの非同期区間の初期値は次のとおり。

```c
.random_carrier={.deviation_hz=100,.update_hz=1000},
```

基準キャリア1,000 Hzに一様な±100 Hzの偏差を加え、1 msごとに更新する。`deviation_hz=0`または項目省略で無効。`update_hz`は1～100,000 Hzで、実際の保持時間は100 kHzの制御フレーム数へ丸める。三相共通のキャリア周波数だけを変え、キャリア位相はリセットしない。固定シードのxorshift32により起動から再現可能。DMAバッファやメインループの更新周期によらず保持時間を数える。同期SPWM・広域3パルス・1パルスへの指定は不正設定として扱う。キャリアの偏差込みの下限は0より大きく、上限は50 kHz未満であること。JR・GTO・PMSMの既存設定にはランダム変調を加えていない。

登録一覧の各プロファイルには次の倍率を指定する。

```c
.amplitude_scale=1.0f, /* 0.8なら曲線値の80%、1.2なら120% */
```

倍率は明示指定する（省略時のC初期値0は倍率0）。自動曲線値とUSBの`m`固定値の両方に適用し、`m_effective=clamp(m_input*amplitude_scale,0,amplitude_limit)`をQ15化する。`m_cmd`ログは倍率・制限後の値。角度も倍率適用後のmから再計算する。位相やキャリア周波数は倍率で変化しない。USB固定値を再設定しても倍率は一度だけ掛かる。

これは振幅パラメータの調整で、実モーターの電圧自動校正ではない。過変調・広域3パルスでは基本波電圧は倍率に比例しない。また1パルス矩形波は角度0固定なので倍率で電圧を下げられず、38°モデルはm=0でもゼロ電圧にならない。**倍率0は出力停止を意味しない**。停止には`stop`を使う。全域の基本波電圧を一定比で制限する制御は別途必要になる。

追加テストでは偏差上下限、1 ms保持、平均周波数、通常／PIO用生成間の位相一致、同期モードでの無効化、倍率・飽和・角度追従を確認する。

実機確認: 追加版をPicoへ書き込み、IGBT非同期20 Hz→同期40 HzをUSBで確認。`fault=0`、最大生成時間1,217 µs（周期2,560 µs）。[ログ](random-carrier-device-log.txt)。ログの`fc`は基準キャリアで、ランダム偏差を含む瞬時値ではない。今回ロジアナでの再取得はしていない。終了時はJR・0 Hzへ戻した。
