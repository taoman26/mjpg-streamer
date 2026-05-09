# input_haiku_media

Haiku OS 専用の mjpg-streamer 入力プラグインです。  
Haiku の Media Kit (BMediaRoster) を通じて `haiku-uvc-webcam` メディアサーバーアドオンに接続し、USB ウェブカメラのフレームを JPEG にエンコードしてストリーミングします。

## 概要 / Overview

Haiku には V4L2 が存在しないため、mjpg-streamer の既存 `input_uvc` プラグインは動作しません。  
このプラグインは Haiku の Media Kit を使い、`haiku-uvc-webcam` が公開している B_RGB32 フレームを受け取り、Translation Kit で JPEG へ変換します。

## 必要条件 / Requirements

- **OS**: Haiku R1 Beta 4 以降
- **メディアアドオン**: [`haiku-uvc-webcam`](https://github.com/lamat/haiku-uvc-webcam) がインストール済みで `media_server` に組み込まれていること
- **ビルドツール**: gcc 13+, cmake 3.x

## ビルド / Build

```sh
cd mjpg-streamer-experimental
mkdir -p _build && cd _build
cmake ..
make input_haiku_media
```

## 使い方 / Usage

```sh
cd mjpg-streamer-experimental/_build
./mjpg_streamer \
    -i "./plugins/input_haiku_media/input_haiku_media.so" \
    -o "./plugins/output_http/output_http.so -w ./www"
```

ブラウザで `http://<HaikuのIP>:8080/?action=stream` を開くとストリームを確認できます。

### オプション

| オプション | 説明 | デフォルト |
|-----------|------|-----------|
| `-d <node_name>` | 接続するメディアノード名 | (最初に見つかったビデオプロデューサー) |
| `-f <fps>` | フレームレート | 30 |
| `-q <quality>` | JPEG 品質 (1–100) | 80 |

## 仕組み / How it works

1. `BApplication` を初期化して Media Kit を有効化
2. `BMediaRoster` で `haiku-uvc-webcam` ノードを検索
3. `MjpgConsumer` (`BBufferConsumer` + `BMediaEventLooper`) を生成・接続
4. 受信した B_RGB32 バッファを `BBitmap` + `BTranslatorRoster` で JPEG へエンコード
5. mjpg-streamer の共有バッファ (`pglobal->in[id].buf`) に格納してストリーミング

## 注意事項 / Notes

- B_RGB32 は Haiku では BGRA バイト順です（Translation Kit が自動処理します）
- `haiku-uvc-webcam` アドオンは `media_server` に登録されている必要があります  
  (`media_server &` で起動後、アドオンを `/boot/home/config/non-packaged/add-ons/media/` に配置してください)
