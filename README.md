# VVVF Pico

RP2040 / Raspberry Pi Pico向けの三相PWM信号生成。起動時は`jr-fall`（JR墜落プリセット）を選択し、0 Hz・三相指令Lowで待機します。PIO＋DMAにより、PWMのTOP割り込みに依存せず非同期・同期キャリアを生成します。

- [公開前のライセンス確認・未解決事項](Doc/license-audit.md)
- [第三者の帰属・ライセンス表示](THIRD_PARTY_NOTICES.md)
- [仕様と構成](Doc/specification.md)
- [PIO・DMA・割り込みの構成図](Doc/architecture.md)
- [プロファイルの追加・カスタマイズ](Doc/profile-customization.md)
- [追加音プリセットと切り替え方](Doc/audio-patterns.md)
- [公開プリセットの出典と基本波・キャリア対応](Doc/preset-sources.md)
- [変調率変更とスイッチング角の追従](Doc/amplitude-tracking.md)
- [ビルド・実機確認](Doc/validation.md)
- [2026-11-12 実測記録とPulseView波形](Doc/measurements-2026-11-12.md)
- 起動点: `src/main.c`、設定: `include/vvvf/config.h`
- 元コード: `old/vvvf-pico-original.c`、以前の別案: `old/main.c`（ともにビルド対象外）

```sh
cmake -S . -B build -DPICO_BOARD=pico
cmake --build build -j4
./tests/run.sh
```

生成物は `build/vvvf-pico.uf2`。RP2040 Picoとロジアナで基本動作・振幅を測定済み。現在のエッジ指定は8 ns刻み（125 MHz時）。電力段では未検証。出力は三相信号のみで、相補ゲート信号・デッドタイム・過電流保護は含みません。

公開条件: PMSM参照元のMIT表示およびSDK等の通知は同梱しています。JRプリセット参照元の再配布許諾と、プロジェクト独自部分のライセンスは未確定です。現時点で全体をMIT等として再配布可能と表示していません。詳細は上記のライセンス確認を参照してください。
