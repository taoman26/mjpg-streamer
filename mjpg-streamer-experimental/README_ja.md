mjpg-streamer
=============

現在、既知の問題はありませんが、このソフトウェアはまだ若く、広く使用されていないため、問題が発生する可能性があります。このソフトウェアを使用する場合は、実際に何をしているのか理解している必要があります。ソフトウェアを使用する場合、ソースコードが期待通りに動作するかを確認し、使用するリスクは自己責任となります。


使用方法
=====

mjpg-streamerを起動する際、1つ以上の入力プラグインと1つの出力プラグインを指定します。例えば、V4L互換のウェブカメラをHTTPサーバー経由でストリーミングする場合（最も一般的な使用例）、次のようにできます：

	mjpg_streamer -i input_uvc.so -o output_http.so

各プラグインは様々なオプションをサポートしており、プラグインの`--help`オプションを使用してオプションを確認できます：

	mjpg_streamer -i 'input_uvc.so --help'


その他の例はstart.shバッシュスクリプトで確認できます。

プラグインのドキュメント
====================

入力プラグイン：

* input_file
* input_http
* input_opencv ([ドキュメント](plugins/input_opencv/README.md))
* input_ptp2
* input_raspicam ([ドキュメント](plugins/input_raspicam/README.md))
* input_uvc ([ドキュメント](plugins/input_uvc/README.md))

出力プラグイン：

* output_file
* output_http ([ドキュメント](plugins/output_http/README.md))
* ~output_rtsp~ (機能しません)
* ~output_udp~ (機能しません)
* output_viewer ([ドキュメント](plugins/output_viewer/README.md))

コントロール画面の日本語化
====================

Webインターフェースは通常英語で表示されますが、コントロール画面のみ日本語版（control_ja.htm）も利用できるようにしました。
「コントロール」のリンクをクリックすれば、カメラの設定項目や選択肢が日本語で表示されます。

翻訳はクライアントサイドで実装されているため、新たなコントロール項目を日本語化したい場合は、翻訳辞書に対応する日本語訳を追加するだけで対応できます。
Haikuでのビルド
=================

mjpg-streamerは [Haiku OS](https://www.haiku-os.org/) でビルドできます。

**Haikuで動作するもの：**
- コアバイナリ（`mjpg_streamer`）
- `input_http` プラグイン
- `output_file`、`output_http`、`output_rtsp`、`output_udp` プラグイン

**Haikuで利用できないもの（Linux専用）：**
- `input_uvc` — Video4Linux2（`linux/videodev2.h`）が必要
- `input_file` — `sys/inotify.h` が必要
- `input_raspicam` — Raspberry Pi専用
- `output_viewer` — SDL が必要
- `output_zmqserver` — ZeroMQ が必要

**Haiku対応のために行った修正：**
- `cmake_minimum_required` を VERSION 3.5 に更新（新しいCMakeで必須）
- `compat/linux/` に互換シムヘッダを追加（`linux/types.h`、`linux/videodev2.h`、`linux/version.h`）
- `utils.c` の修正：`<wait.h>` を `<sys/wait.h>` に変更、`<linux/stat.h>` を削除
- 非Linuxプラットフォームで `-ldl` リンクフラグを除去（HaikuはlibRootに動的リンク機能が内蔵）

**ビルド方法：**

    mkdir _build && cd _build && cmake -DCMAKE_BUILD_TYPE=Release .. && make

