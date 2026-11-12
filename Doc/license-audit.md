# 公開前の参照元・ライセンス確認

確認日: 2026-11-12。対象は現在のソース、プリセット抜粋データ、ドキュメント、Pico向けUF2とホスト測定ツール。結論は**表示を整備済みの部分と、再配布許諾が未確認の部分がある**。この文書は公開・商用利用の包括的な許諾証明ではない。

## 確認結果

| 参照元・依存物 | 確認した条件 | このプロジェクトとの関係 | 状態 |
|---|---|---|---|
| VvvfGeeks/YAML-Files | 対象コミットの全ツリーにLICENSE/COPYINGなし。READMEにも再配布ライセンスなし | JR基本波/キャリア/振幅表、JSON抜粋、関連試験・記録 | 許諾未確認 |
| VvvfGeeks/VVVF-Simulator | READMEに非商用利用の条件と、生成動画等へのURL表示の指定。標準OSSライセンスなし | SYNC3 Alt1の数式対応を確認。C#コードやアセンブリは取り込まずCで実装 | 適用範囲・派生配布条件の確認が必要 |
| naoto64/VVVF-Simulator | MIT、Copyright (c) 2026 naoto64 | PMSMプリセット、速度/基本波換算、広域3角度モデルを参照・適用 | 原文と帰属表示を保存 |
| Raspberry Pi Pico SDK 2.3.1 | 主にBSD-3-Clause。printf、USB descriptor等にMIT部分あり | SDK importファイル、ビルド/リンクするランタイム | 元表示維持・原文と個別ヘッダー表示を保存 |
| TinyUSB | MIT、複数の個別ファイル著作権表示 | USB CDC機能としてSDK経由で使用 | 原文と個別ヘッダー表示を保存 |
| Arm GNU Toolchain / Newlib / libgcc | Newlibは複数の条件、libgccにはGPLとRuntime Library Exception等 | UF2へC/mathランタイム・libgccオブジェクトがリンクされる | 配布元のライセンス集と例外原文を保存。ツールチェーン自体は同梱しない |
| libsigrok | GPL-3.0-or-later | ホスト側capture_logic.cがAPI利用。ファームウェアとは別プログラム | ホスト実行ファイルを配布する場合は別途GPL条件への対応が必要 |
| 本プロジェクトの元コード・新規コード | ルートのLICENSEは未設定 | ユーザー提供元コード、追加C/Python/PIO等 | 公開時に独自部分のライセンス選択が必要 |

著作権・条件の通知はルートの[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)へ集約した。これを置くこと自体が未許諾データの再配布を許可するわけではない。

## JR関連で未解決の点

### 公開されていることと再配布許諾は別

[YAML-FilesのREADME](https://github.com/VvvfGeeks/YAML-Files/blob/2f83dc2cd0466b35ce9720d5101164656064f70c/README.md)は共有用サンプルとの説明と、実車と完全一致しない旨だけを示す。GitHubのリポジトリ情報でもライセンスはnull。対象ツリーに明示的な許諾文書は見つからなかった。

[GitHubのライセンス説明](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository)も、公開リポジトリでの閲覧・forkと、一般的な複製/配布/改変の許諾を区別している。公開されているからMIT等で再配布できるとは扱わない。

数値や数式などの事実・アイデアと、著作物であるコードやデータ集合は区別する必要がある。一方、今回のプリセットは元の選択・区分を参照しており、JSONには抜粋も含む。数値をCで書き直しただけで許諾問題が必ず解消する、とは判断していない。

### シミュレーター本体の条件をYAMLへ自動適用しない

[本体READMEの利用規約](https://github.com/VvvfGeeks/VVVF-Simulator/blob/fb8d0c99e0ccc3708fa3429f71bc9270db460193/README_JPN.md)には「非商用に限り、無償」とある。生成動画等には参照URLを示す指定もある。別リポジトリのYAMLにもそのまま同じ許諾がある、あるいは非商用なら派生コード/設定データの再配布も無条件で可能、と拡張して解釈しない。

本プロジェクトはC#の原文を転載していないが、数式対応の確認に実装を参照した。参照事実と条件を残し、データ集合の利用・再配布や派生物への条件適用範囲を確認する必要がある。

### 該当するローカル範囲

- `src/profiles/presets.c`のJR力行/減速パラメータ。
- `src/control/control.c` / `src/control/programmed.c`のJR広域3パルス式の対応部分。
- `Doc/preset-sources.json`のJR抜粋と、出典の数値を整理したドキュメント/テスト。
- これらのプリセットを搭載したUF2や記録を配布する際の権利関係。

許諾を確認するまで、この範囲まで含めた「一括MIT」「商用利用自由」「権利確認済み」の表示は付けていない。現在はローカル作業のみで、GitHubへpush・公開・作者への連絡は実施していない。

## ライセンス表示を整備した範囲

### naoto64由来のPMSM部分

[LICENSE](https://github.com/naoto64/VVVF-Simulator/blob/9335c10a135e383e60dc4a1d1ba22063eee40325/LICENSE)を確認し、既存の`Doc/third-party/naoto64-MIT.txt`と一致することを確認した。MITの著作権/許諾文/免責文を保持する。第三者からのMIT許諾を、本プロジェクト全体へのライセンス付与と混同しない。

### SDKとバイナリ配布

Pico SDKの`LICENSE.TXT`、pico_printfのMIT、TinyUSBのMITを同梱した。USB descriptorのMicroPython由来MITや、TinyUSB各ファイルの追加著作権表示も、ビルド対象89ソースのヘッダーから保存した。原文は`Doc/third-party/`、一覧・チェックサムは`license-evidence.json`にある。

`pico_sdk_import.cmake`の元のBSD表示は変更していない。SDKのBluetooth/Wi-Fi等を本プロジェクトへ丸ごと同梱する操作はしていない。現在のRP2040ビルドにCMSISを別途取り込んでいないが、Pico 2等へ構成変更する際はライセンス一覧も再点検する。

リンクマップからNewlibのC/mathオブジェクトとlibgccの利用を確認した。Arm GNU Toolchainに添付された完全な`license.txt`を保存した。このファイルにはコンパイラ、デバッガ、ドキュメント等、UF2へリンクされない成分の条件も含む。原文を保存したことは、それらすべてがファームウェアにリンクされる/すべての条件が一律適用される、という意味ではない。GCC Runtime Library Exception 3.1も別ファイルに保存した。

UF2を単独で配布すると通知文が落ちるので、関連する`THIRD_PARTY_NOTICES.md`と`Doc/third-party/`等の通知資料を伴わせる。ライセンス原文を揃えたことと、上記JR関連の未解決点が解消したことは別である。

### ホストツールとファームウェアを分ける

[libsigrokの公式説明](https://sigrok.org/wiki/Libsigrok)とローカルヘッダーでGPL-3.0-or-laterを確認した。ホストの`capture_logic.c`はlibsigrok/GLibへリンクするため、コンパイル済みの測定ツールを配布する場合はGPL条件と対応ソース提供を別途確認する。

現在の配布対象ソース内にlibsigrok本体や`/tmp/vvvf_capture`は入れていない。ファームウェアのリンク対象にもlibsigrokはない。ファームウェアまで一律にGPLになるとする判断はしていない。PulseViewやロジアナのUSBファームウェアも同梱していない。

## 調査したが取り込んでいないリポジトリ

- `VvvfGeeks/RPi-Pico-Drive`: 通信/再生方式の参照調査のみ。ソースは取り込んでいない。
- `datacrystals/VVVFSimulator`: サンプル探索時のREADME確認のみ。AGPLコードや設定を取り込んでいない。
- `intel713/VVVF_sound_generator`、`S2KTS/Arduino-N-gauge-Power-Pack`: 候補探索/ツリー確認のみ。コード・音源・設定を取り込んでいない。

技術説明に参照したRaspberry Pi、Microchip、imperix等の文書から、図版・音声・実装コードを転載していない。外部サイトの音源もリポジトリへダウンロードして配布していない。

## 公開前に残る作業

1. JRデータと参照実装について、公開・設定抜粋/変換データ・UF2の再配布・商用/非商用・必要表示の範囲を権利者へ確認し、回答を保存する。依頼文案は[publication-permission-draft.md](publication-permission-draft.md)。この文案は未送信。
2. 独自部分の公開ライセンスを選ぶ。ユーザー提供の既存コード`old/`も含めて自分が許諾できる範囲を確認し、第三者部分と適用範囲を分けてルートLICENSEを設定する。
3. 実際に配布するソース/UF2/ホスト実行ファイルに応じて、上記の通知と対応ソースを添付する。依存バージョン変更時は通知資料を更新する。

これらの判断は公開資料に基づく技術的な整理であり、特定の法域での権利関係まで確定する法律判断ではない。
