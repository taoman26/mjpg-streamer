# input_haiku_media

Haiku OS 専用の mjpg-streamer 入力プラグインです。
BubiCam の [`WebcamKit`](https://github.com/atomozero/BubiCam) ライブラリ (`libwebcam.so`) を
経由して USB ウェブカメラのフレームを取得し、JPEG にエンコードしてストリーミングします。

## 概要 / Overview

Haiku には V4L2 が存在しないため、mjpg-streamer の既存 `input_uvc` プラグインは動作しません。

このプラグインは以前、Haiku の Media Kit (`BMediaRoster`) を自前で叩いて
`haiku-uvc-webcam` アドオンに直接接続していましたが、ノード登録〜接続の
ハンドシェイク中にカーネルパニックを再現性高く引き起こすことが分かりました
(おそらく `BMediaEventLooper` の配線漏れ — `NodeRegistered()`/`Run()` が
無く、ノード自身の制御スレッドが起動していなかったため)。

現在は、同じ webcam 接続処理を安定して実行できている BubiCam の
`WebcamKit` (`WebcamRoster` / `WebcamDevice` / `VideoConsumer`) を
`libwebcam.so` としてリンクし、その上に薄いブリッジを被せる方式に
書き換えています。プラグイン自身が行うのは、デバイスの列挙、
`WebcamDevice::StartCapture()` の呼び出し、受信した `BBitmap` フレームの
JPEG エンコードと mjpg-streamer の共有バッファへの書き込みだけです。

## 必要条件 / Requirements

- **OS**: Haiku R1 Beta 4 以降
- **BubiCam**: このリポジトリと同じ階層(`../../BubiCam`、既定では
  `~/git/BubiCam`)に [`BubiCam`](https://github.com/atomozero/BubiCam) を
  clone してビルド済みであること(`lib/libwebcam/objects.x86_64-cc13-release/libwebcam.so`
  が存在している必要があります)
- **メディアアドオン**: [`haiku-uvc-webcam`](https://github.com/lamat/haiku-uvc-webcam) が
  インストール済みで `media_server` に組み込まれていること(WebcamKit が内部で使用します)
- **ビルドツール**: gcc 13+, cmake 3.x

## ビルド / Build

```sh
# 先に BubiCam 側の libwebcam.so をビルドしておく
cd ~/git/BubiCam/lib/libwebcam && make

cd ~/git/mjpg-streamer/mjpg-streamer-experimental
mkdir -p _build && cd _build
cmake ..
make input_haiku_media
```

`libwebcam` が見つからない場合、cmake の出力に
`- input_haiku_media  (missing: ... webcam=WEBCAM_LIB-NOTFOUND)`
と表示され、このプラグインはビルド対象から外れます。

## 使い方 / Usage

```sh
cd mjpg-streamer-experimental/_build
./mjpg_streamer \
    -i "./plugins/input_haiku_media/input_haiku_media.so" \
    -o "./plugins/output_http/output_http.so -p 8090"
```

ブラウザで `http://<HaikuのIP>:8090/?action=stream` を開くとストリームを、
`http://<HaikuのIP>:8090/?action=snapshot` で単発の JPEG を確認できます。

### オプション

| オプション | 説明 | デフォルト |
|-----------|------|-----------|
| `-d <node_name>` | 接続するデバイス名(部分一致) | (最初に見つかったウェブカメラ) |

解像度・フレームレート・JPEG品質は `WebcamDevice` 側の自動ネゴシエーションに
委ねており、このプラグインからは指定できません。

## 仕組み / How it works

1. `BApplication` を初期化して Media Kit を有効化
2. `WebcamRoster::EnumerateDevices()` でウェブカメラを列挙し、1台選択
3. mjpg-streamer 用の小さな `BLooper` (`MjpgLooper`) を起動
4. `WebcamDevice::StartCapture(looper)` を呼び出し、以降 `MjpgLooper` に
   `MSG_WEBCAM_FRAME` メッセージで `BBitmap` フレームが届く
5. 受信した `BBitmap` を `BTranslatorRoster` で JPEG へエンコード
6. mjpg-streamer の共有バッファ (`pglobal->in[id].buf`) に格納してストリーミング

デバイス検出・ノード接続・バッファグループの管理など、クラッシュの原因に
なりやすい低レベルの Media Kit 処理はすべて BubiCam 側の実装に任せています。

## 注意事項 / Notes

- 映像品質(乱れ・ティアリング)は USB 帯域不足による isochronous 転送の
  問題であり、このプラグイン固有の不具合ではありません
- `libwebcam.so` の実行時ロードには `BUILD_RPATH` を利用しているため、
  `LD_LIBRARY_PATH` の設定なしにビルドディレクトリからそのまま実行できます
