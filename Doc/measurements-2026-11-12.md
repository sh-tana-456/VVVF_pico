# 2026-11-12 実測記録

## 接続と方法

- Raspberry Pi Pico / RP2040、clk_sys=125 MHz、Pico SDK 2.3.1。
- USB ID 0925:3881のSaleae互換ロジアナ（libsigrok fx2lafw）。PulseViewは取得中の専有を解くため終了してもらい、同じlibsigrokで直接取得した。
- CH0=U(GP22)、CH1=V(GP21)、CH2=W(GP20)。ユーザー確認によりロジアナだけを接続し、電力段は未接続。
- 修正前は2 MHz、修正後は12 MHzで取得。保存した`.sr`はlibsigrokで読み込めることを確認した。PulseViewでもFile → Openで再生できる。
- `f HZ`で保持、`r HZ`で5 Hz/sランプ、`stop`で停止。各取得前に約120 ms待った。測定後は0 Hzに戻した。

## 原因と修正

### 低変調率の過大出力

最初のPIO実装は出力を10 µs単位に丸めていた。1 kHzキャリアで微小な比較器指令を入れると、線間パルスの多くが10 µsから小さくならなかった。そのため0.5 Hzと1 Hzで指令が2倍違っても、基本波の振幅はほぼ同じだった。

修正後は10 µsごとの比較器計算を維持し、フレーム内の正弦波・三角波交点を線形補間する。PIOにピン値と継続時間を渡し、125 MHzのクロック単位でエッジを出す。8 ns刻み、最短区間24 nsの設計であり、今回の測定器の分解能は83.3 ns。最短24 ns自体の測定確認はしていない。

0.5 Hz時のU-Vパルス中央値は **10.0 µs → 1.5 µs**、最大は **10.0 µs → 2.17 µs**になった。

### 3パルスの位相と基本波ゲイン

以前のキャリア位相引き継ぎ方式は、N=3で基本波の振幅と位相がモード切り替え履歴に依存した。同期のキャリア位相を `3×基本波位相+0xC0000000` に固定し、低キャリア比特有の基本波ゲインを逆変換表で補正した。N=9/N=15でも同じ規定の位相関係を使う。

ログの`m_cmd`は所望基本波電圧率、`m_ref`は比較器へ入れる振幅率。3パルスでは両者が異なる。例: 55 Hzでm_cmd=0.55000、m_ref=0.46944。

## 基本波振幅の比較

GPIOの0/1を理想的な0/Vdcとみなし、U-Vを100 µsごとに積分して指定周波数へ射影した。基本波ピークを `sqrt(3)/2 × Vdc` で割った値を表に示す。これは論理信号から求めた電圧率で、アナログ電圧やモーター電流の測定ではない。

| 基本波 Hz | 指令電圧率 | 修正前実測 | 最終版実測 |
|---:|---:|---:|---:|
| 0.5 | 0.005 | 0.02547 | 0.004985 |
| 1 | 0.010 | 0.02546 | 0.009992 |
| 5 | 0.050 | 0.05591 | 0.04997 |
| 20 | 0.200 | 0.19346 | 0.19995 |
| 45（N=9） | 0.450 | 0.45009 | 0.44997 |
| 55（N=3） | 0.550 | 0.62249 | 0.55002 |
| 100（N=3） | 1.000 | 1.20433 | 0.99984 |

修正前のN=3の値は今回のモード切り替え順での値であり、常に同じゲインではなかった。最終版は位相関係を固定した。

55 Hzで各線間基本波の振幅は約0.47633×Vdcでそろい、位相は約-148.496° / 91.502° / -28.498°、相互差は約120°だった。位相の絶対値は取得開始時刻に依存する。

0.05/0.1 Hzも取得したが2秒の記録は基本波1周期を含まないため、この表の基本波振幅比較には使っていない。

## 0 Hzからの起動・停止

[最終版の起動・停止キャプチャ](captures/final-startup.sr)は3秒、12 MHz。取得開始後約0.2秒で`r 5`、約2秒で`stop`を送った。

- 初期0 Hzは三相Low。
- 約1秒で0→5 Hzへランプし、5 Hzを保持。ログは約1.435→3.944→5.000 Hz、V/f値も連動した。
- 起動時には全Lowから約50%の共通モードPWMへ切り替わる。これは比較式の中心であり、変調率50%という意味ではない。線間の差分を確認する必要がある。
- 停止後は三相Low。先頭・末尾の無出力区間をキャプチャで確認した。
- 修正前の10 µs下限は解消し、起動直後から微小な差動パルスを出す。

起動時の詳細な時刻とパルス幅は[startup-analysis.json](captures/startup-analysis.json)、コマンドとPicoログは[final-startup.json](captures/final-startup.json)を参照。USB送信時刻とハードウェアの取得開始には多少の差がある。

## 処理時間と検証範囲

最終版の実測`render_max`は1122 µs、バッファの再利用間隔は2560 µs。今回測った0～100 Hzの各動作点と起動・停止では`fault=0`を維持した。現在の構成ではPico 2やデバッガへの切り替えは不要と判断した。ただしrender_maxにIRQ開始前の遅延は含まれず、長時間耐久や故障注入を保証する値ではない。

確認済み: IGBTの非同期・9パルス・3パルスの定常波形、基本波振幅と位相、0 Hz停止、0→5 Hzランプ起動、停止、ホストASan/UBSan、Pico向けビルド、USB経由の書き込み検証。

未実施: GTO実機測定、全境界の連続加減速、長時間連続運転、故障注入、電力段接続。同期モード変更時のキャリア位相の飛びやパルスの連続化は今後の項目。

## 保存ファイルと再現

- `captures/before-*Hz.sr`: 最初の10 µs刻み実装。
- `captures/timed-*Hz.sr`: エッジ時間指定のみを修正した中間版。
- `captures/final-*Hz.sr`: 位相固定とN=3電圧補正も含む最終版。
- [解析値一覧](captures/analysis.json)、[最終版のPicoログ](captures/final-summary.json)。

```sh
# libsigrok開発ヘッダーとpkg-configが必要
cc -Wall -Wextra -Werror tools/capture_logic.c -o /tmp/vvvf_capture $(pkg-config --cflags --libs libsigrok)
# PulseViewを終了し、ロジアナだけにつないだ状態で実行する
python3 tools/measure.py --tag repeat
python3 tools/measure.py --tag repeat --startup
python3 tools/analyze_capture.py Doc/captures/final-0.5Hz.sr --hz 0.5
# N=3の逆ゲイン表の再生成（ハードウェアを使わない）
cc -std=c11 -O2 -Iinclude tools/generate_sync3_table.c src/control/control.c src/control/programmed.c src/control/profiles.c src/profiles/presets.c src/control/voltage.c src/control/modulator.c -lm -o /tmp/vvvf-gain
/tmp/vvvf-gain
```
