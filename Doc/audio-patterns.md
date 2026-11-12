# 音再現プリセットの使い方

既存の`igbt`/`gto`に加えて、公開設定から基本波・キャリア対応を移植した`jr-fall`/`pmsm-toshiba`を選択できる。以前の仮設計曲線は廃止した。出典、全周波数表、元設定と異なる部分は[公開データの移植仕様](preset-sources.md)を参照。

目的は音・PWMパターンの再現。PMSM実モーター用のFOC・電流制御・位置推定は含まない。メーカーの設計データではなく公開シミュレーターの作者による再現設定が出典。

## USB操作

```text
profiles
stop
profile jr-fall
r 65
r 0
```

`r 65`は5 Hz/sで加速、`r 0`は5 Hz/sで減速する。JRは上昇中に力行曲線、下降中に減速曲線を使い、定速保持中も直前の方向を保持する。ボタンUP/DOWNでも同様。物理ボタンが押されるとUSBの目標保持を解除する。

```text
stop
profile pmsm-toshiba
r 85
```

- `profiles`: 名前の一覧。
- `profile 名前`: 現在値・目標値とも0 Hzの停止中だけ選択できる。
- `f HZ`: 測定用の即時設定。前の基本波より低い値なら減速、高い値なら力行と判定。
- `stop`: 0 Hz・三相指令Low。全素子OFFの指示ではない。
- `m 値` / `m auto`: 測定用の振幅固定/解除。[角度追従の検証](amplitude-tracking.md)を参照。
- 起動時は`jr-fall`、選択は不揮発保存しない。
- `pmsm-demo`は`pmsm-toshiba`の互換名。

## ログの読み方

`profile`、`direction=power/brake`、基本波`f`、生振幅/所望値`m_cmd`、比較器振幅`m_ref`、パルス名`N`、三角波キャリア`fc`、生成方式`wave`、処理時間、`fault`を出す。

| wave | 生成方式 | Nとfcの意味 |
|---|---|---|
| spwm | 正弦波と三角波の比較SPWM | 非同期N=0、同期ならfc=N×f1 |
| wide3 | 共通の広域3パルス | N=3はパターン名。三角波を使わずfc=0。角度係数はプロファイル指定 |
| one-pulse | 1パルス矩形波 | N=1、三角波を使わずfc=0 |

公開プリセットの`m_cmd`は元データの生振幅であり、基本波の実効振幅そのものではない。過変調では1を超えることもある。通常SPWMのN=3補正`sync3_reference`は広域3パルスへ適用しない。

## 編集場所

全プリセットは`src/profiles/presets.c`へ集約した。`src/control/profiles.c`はデータを評価する共通処理で、車両名による分岐は持たない。追加方法と角度係数の意味は[プロファイルのカスタマイズ](profile-customization.md)を参照。参照設定を変更するときは`Doc/preset-sources.json`の原データとの差分も記録する。

## 取得ツール

```sh
cc -Wall -Wextra -Werror tools/capture_logic.c -o /tmp/vvvf_capture $(pkg-config --cflags --libs libsigrok)
python3 tools/measure.py --tag jr-repeat --profile jr-fall
python3 tools/measure.py --tag pmsm-repeat --profile pmsm-toshiba
```

CH0=U(GP22)、CH1=V(GP21)、CH2=W(GP20)、共通GNDを接続する。PulseViewは取得中の専有を解くため終了する。停止時がLowでない場合や動作時が固定入力ならエラーにする。ツール終了時はjr-fall・0 Hzへ戻す。

このツールの定常点測定は主に力行側であり、連続減速や実際の音響比較は別途行う。今回の方向別測定ファイルは[移植仕様](preset-sources.md)に記載した。
