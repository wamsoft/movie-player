# About

WebM 動画再生ライブラリ作成用の作業レポジトリです。

Android は MediaExtractor/MediaCodec を使用するため、
WebM 以外の各種動画フォーマットにも対応します。

# 状況

細かい事項については各アーキテクチャごとに後ろの方に別項があるので参照してください。

現状のコードは音声対応に際して Windows 版のみ対応作業が完了しており、
Android 版については broken な状態です。

- Android
  - NDK の MediaExtractor+MediaCodec で対応
  - ビデオのみ動作。オーディオ非対応
  - **音声対応作業に際して、一旦対応を放置しているためビルドできない状態**
- Windows
  - nestegg + libvpx で cpu でのデコードで対応
  - miniaudio を内蔵して音声対応
  - CPU ベース処理で generic な作りなので、別アーキテクチャでも動作可能
    - `windows/` ディレクトリでやってるので、本格的に linux とか MacOS への対応する場合はディレクトリ名をなんか考えたい
- Linux
  - Windows コードでそのまま動作
  - GUI の確認環境が現状手元にないので、movie_player_sample の動作は未確認
  - **ビルド完走＋ movie_exporter で画像が出ることを確認**
    - WSL 上で movie_player だと画面描画が更新されないが確認した WSL 環境での OpenGL の問題かも

## 把握している問題

- Windows
  - pause/resume したときに、復帰後の数フレームがフレームスキップ扱いになる
- Android
  - 音声対応版で行った変更に未対応なためビルドできない状態
- Linux
  - 確認した WSL 環境では movie_player_test での描画が正しく行われない

## 最近の大きな変更

- 動画取得処理は OnVideoDecoded の形に再修正
- 生成時に指定したビデオフォーマットでのビットマップを書き戻しの形
- ※ YUVテクスチャ処理が対応できてない。現在 ARBG 以外だと動作不良
- GetVideoFrame() 系メソッドは廃止
- 音声データも同様にできるように修正予定だが現状はまだ（音声は別スレッドから吸い上げる可能性が高い）
- テストコードがあわせた修正ができてないので要対応

- Video/Audio の情報取得が個別メソッドから `IMoviePlayer::GetVideoFormat()`、
  `IMoviePlayer::GetAudioFormat()` に集約されています。

## 音声対応

Windows 版ビルドでのみ、miniaudio で対応済みです。

一応アプリ側にオーディオエンジンを持つ形にも対応できるようにはしてありますが
テストコードを用意できていないので実際の動作については未確認です。

# ディレクトリ構造

- `src/`
- `include/`
  - ライブラリコードです。
- `extlibs/`
  - 使用している外部ライブラリコードです。
- `test/`
  - 各機種用の再生テストコードです。
- `test/dat`
  - 再生テスト用の webm 動画です。プロジェクトの都合上、
    テストプログラムのツリー側にコピーして使ってたりします(Android とか)。

# コード構造

## 共通構造

`include/MoviePlayer.h` に `IMoviePlayer` を定義してあります。
これを継承して各アーキテクチャ用の `MoviePlayer.h`に
実装クラスの定義を配置してあります。

### 利用方法

`IMoviePlayer::CreateMovePlayer(const char *filename, InitParam &param)`

で、`IMoviePlayer`のインスタンスを作成して使用します。
`param.videoColorFormat` に出力したいカラーフォーマットを指定してください。

※現時点ではアーカイブ中のファイルの再生は未対応です。

## Windows 対応について

### 設計方針

CPU 処理する 汎用ライブラリを使用して c++11 の std メインで組んでいるので
Windows 以外にもそのまま持っていけるように作っています。

内部の構造としては、Android が stagefright 由来ぽい形に
なっているので、Win 版もそれに寄せた作りにしてあります
(Extractor ＋ Decoder)。

#### Seek 処理について

Seek は最近傍の Cue ポイント(キーフレーム)へジャンプする仕様になっています。
デコーダの仕様上、キーフレームに飛ばないと正しくデコードできないためです。
(これは Win 版に限らずこのようになっています)

### ライブラリのビルド方法

vcpkg + cmake の環境を想定しています。
ルートの CMakeLists.txt を使用してビルドしてください。

### テストコード

テストコードは現状 2 つ用意してあります。

- `tests/windows/movie_player_test.cpp`
  - OpenGL で描画するテスト
    - LEFT/RIGHT キー: 2 秒毎のシーク
      - ※直近キーフレームへのシークなのでシーク間隔が大きすぎると期待した動作をしません(5 秒間隔だったりすると、2 秒シークでは最近傍が一生現在地に吸い付きます)
    - SPACE: ポーズトグル
    - ENTER: 先頭へ巻き戻し
    - ESCAPE: 再生停止してそのままテストプログラムが終了
    - マウスホイール上下: 音声ボリューム上下
- `tests/windows/movie_exporter.cpp`
  - 動画を一定間隔(1 秒)ごとに BMP 出力するテスト

`tests/windows/CMakeLists.txt` で両方同時にビルドされるようにしてあります。

### 採用ライブラリ等

- libvpx / vcpkg / Windows 版のみ
- libogg / vcpkg / Windows 版のみ
- libvorbis / vcpkg / Windows 版のみ
- libopus / vcpkg / Windows 版のみ
- libyuv / 自前(`extlibs/`のもの)
- nestegg / 自前(`extlibs/`のもの) / Windows 版のみ
- miniaudio / 自前(`extlibs/`のもの)

なるべく vcpkg で揃える方針で作業しています。
libyuv については、他アーキテクチャの対応もありどのみち自前で抱えているので、
それらに揃えるということで Win でも自前で抱えたものを参照するようにしてあります。

### libyuv の VisualStudio ビルドに関する問題

libyuv には SSE/AVX のアセンブリ実装が含まれるのですが、
これらは **VisualStudio では 32bit ビルド以外は非対応** となっています。
インラインアセンブラで書かれているせいだと思われます。
MSVC 用のイントリンジクスによるコードも部分的にあるようなのですが、
本ライブラリの利用範囲では対応していません。

clang ではコンパイルできるコードが用意されているため、
Windows/x64 に関してのみ、clang で prebuild したバイナリを参照するような
形にしてあります。

`extlibs/prebuild/win64` が当該バイナリの配置場所となります。

### libyuv の prebuild バイナリの更新方法

Visual Studio 2019 を想定しています。
Clang サポートは C++ 環境でも標準ではインストールされないと思うので
Visual Studio Installer から追加インストールしておいてください。
以下の 2 パッケージを導入すれば問題ないと思います。

- v142 ビルドツール用 C++ Clang-cl (x64/x86)
- Windows 用 C++ Clang コンパイラ

VS2019 以降の環境ではどうなるかは不明ですが、
相当するパッケージを導入して対応可能だと思います。

`extlibs/make_prebuild.sh` を vcvars64(VisualStudio の x64 用
コマンドプロンプト環境セットアップバッチ)
相当の環境にした msys あるいは cygwin 上で実行してください。
必要なビルドを行い、`extlibs/prebuild/win64` 配下のバイナリファイルを更新します。

VS のコマンドプロンプト(いわゆる DOS 窓)から作業を行う場合は
シェルスクリプト内のコマンドを適宜コピペ実行するなり
バッチ(.bat)化するなりして対応してください。

現在 git ツリーに配置してあるバイナリは VS2019 の Clang(12.0.0)
で作成したものとなります。

## Linux(Ununtu)対応について

Windows 版は、実態としては Windows に依存するコードは含まないか
あるいは部分的に ifdef で対応しているため、vcpkg の対応している環境上では
そのままビルドできる状態になっています。
これにより Linux でのビルドについても一応対応してあります。

※WSL2(WSLg)環境で movie_player_test が動作することまでは確認していますが、
描画が正しく行われていないのと、手元の WSLg 環境で音声出力できない状態のため
動作確認までは行えていない状態です。

※ソース類のディレクトリ名が`windows/`のまま参照しています。
将来的には `generic/` とか `common/` とかあるいは `src/` 直下に展開とか
なんかそういう感じに移動したいですが、暫定的にそのままのディレクトリ名です。

以下、WSL2 の Ubuntu22.04 でライブラリ部をビルド確認した際の
ステップバイステップの記録です。

- vcpkg を導入する
  - https://github.com/microsoft/vcpkg のドキュメント通りに導入します
  - 必要な前提パッケージについてもドキュメント内にあるので参照してください
- `VCPKG_ROOT`設定
  - `export VCPKG_ROOT=/foo/bar/vcpkg`
- 依存ライブラリをインストールする
  - `./vcpkg/vcpkg install libvpx libogg libvorbis opus`
  - libvpx が nasm と pkgconfig を要求するので導入
    - `sudo apt-get install nasm pkgconfig`
- cmake が無いのでインストール
  - `sudo apt-get install cmake`
- プロジェクトルートにビルド用の一時ディレクトリを作成してビルド
  - `mkdir ubuntu`
  - `cd ubuntu/`
  - `cmake ..`
  - `cmake --build . -j`
- テストプログラムのビルドはさらに OpenGL 関係のパッケージを導入
  - `./vcpkg/vcpkg install glfw3 glew`
    - 必要なら以下のパッケージを導入
    - glfw3 が要求
      - `sudo apt-get install libxinerama-dev libxcursor-dev xorg-dev libglu1-mesa-dev`
    - glew が要求
      - `sudo apt-get install libxmu-dev libxi-dev libgl-dev`
  - `cd test/`
  - `mkdir ubuntu`
  - `cd ubuntu/`
  - `cmake ../windows`
  - `cmake --build . -j`

## Android 対応について

**注意：現状ビルド不可能な状態です**

NDK の MediaExtractor + MediaCodec を使用してフル C++で組んであります。

デコーダは内部で std::thread を使用してスレッド化してあります。
MoviewPlayer のインスタンスごとにスレッドが生まれる感じになります。

### 要求 API バージョン

基本機能として API 21 (Lollipop / Android 5.0) 以上を要求します。

テストプログラムでは、バイナリ列入力で API28 を要求する関係上
`test/android/app/build.gradle` の minSdk は 28 になっていますが、
現状ではバイナリ列入力は動作しないため(詳細は後の記述参考)
実際の利用ケースでは 21 で問題ないはずです。

### ライブラリのビルド方法

vcpkg + cmake の環境を想定しています。
ルートの CMakeLists.txt を使用してビルドしてください

### テストプログラム

`test/android` を AndroidStudio で開いてビルド、実行してください。

### libyuv

MediaCodec の出力は YUV なので RGB への間関に libyuv を導入してあります(`extlibs/libyuv`)。

テストコードはエミュレータ(Pixel3)でのみ確認していますが
実機だとなにか想定外のフォーマットで出力されてくる可能性があります。

### 問題点

バイト列を入力とするインタフェースがうまく動作しない(API がエラーを返す)。

またこの機能自体、WEB や github にも全然利用情報がなく、唯一見つかったのが
自分と同じ状況のエラーになりますという stackoverflow の投稿で、
しかもまったくリプライが付いていないという状態です。
[これ](https://stackoverflow.com/questions/60932644/using-a-custom-amediadatasource-with-ndkmediacodec)

そもそもの機能実装のためには API28(Android OS Pie)以上を要求されることもあり
fd(ファイルディスクリプタ)を渡して動画を開く方法でなんとかする方向で
利用してもらえればと思います。

どうしてもバイト列を入力として与えるインタフェースが必要な場合は
MediaExtractor + MediaCodec による実装はあきらめて他の方法を採用する
必要があります。

あるいは、MediaExtractor 相当の機能を自力で用意して、ビデオストリームの
デコードだけ MediaCodec でハードウェア支援させるという方法もあるかも？
(このあたりは未調査)
