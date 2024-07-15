# emdevice_toolkit

EasyMeasure's deviceのためのツールキット

## 1. はじめに

このツールキットは、EasyMeasure's deviceを管理するために使用されます。デバイスのセンサー設定を管理し、設定に基づいてセンサーデータを取得することができます。

このツールキットは、EasyMeasure's deviceのAPIに基づいています。

## 2. インストール

### 2.1. ツールキットのインストール

```bash
$ git clone https://github.com/toorpia/emdevice_toolkit.git
$ cd emdevice_toolkit
$ ./build_and_install.sh
```

build_and_install.shスクリプトは以下の操作を行います：
- 必要な依存関係（libyaml-dev, libsndfile1-dev, python3-ruamel.yaml）をインストール
- emgetdataをビルドしてインストール
- calibrate.pyを/usr/local/binにコピー

## 3. 使用方法

### 3.1. センサーデータの取得

```bash
$ emgetdata [-f config_file] [-t <duration>] [-s <sensor>]
```

#### 3.1.1. オプション

* -f config_file: センサーデータの設定ファイル。デフォルトは "config.yml"
* -t duration: センサーデータの取得時間（秒）。デフォルトは10秒
* -s sensor: データを取得するセンサー。指定しない場合は全センサーのデータを取得
* -h: ヘルプメッセージを表示
* -v: バージョンを表示

#### 3.1.2. 設定ファイル

設定ファイルはYAML形式です。センサーの設定が含まれています。

設定ファイルの例：

```yaml
afe_ip: 192.168.3.3
afe_port: 50000
sensors: # センサー名, ブロック: A-E, チャンネル: 1-4, ゲイン: 0, 1, 2, 5, 10, 20, 50, 100
  - {label: "S01", block: "A", channel: "1", gain: 5}
  - {label: "S02", block: "A", channel: "2", gain: 10}
  # ... 他のセンサー設定 ...
sampling_rate: 10000 # Hz
```

### 3.2 設定ファイルのセンサーゲインのキャリブレーション

```bash
calibrate.py config_file sensor_label wav_file1 wav_file2 [...]
```

#### 3.2.1. オプション

* config_file: センサーデータの設定ファイル（例："config.yml"）
* sensor_label: 設定ファイル内のセンサーラベル
* wav_file1, wav_file2, ...: キャリブレーション用のWAVファイル

## 4. プロジェクト構造

```
emdevice_toolkit/
├── License.txt
├── README.md
├── build_and_install.sh
├── calibrate/
│   └── calibrate.py
└── emgetdata/
    ├── Makefile
    ├── config.yml.template
    ├── debug.h
    └── emgetdata.c
```

- `build_and_install.sh`: ツールキットのビルドとインストールスクリプト
- `calibrate/calibrate.py`: センサーゲインのキャリブレーションスクリプト
- `emgetdata/`: センサーデータ取得プログラムのソースコードと関連ファイル
  - `emgetdata.c`: メインのC言語ソースコード
  - `config.yml.template`: 設定ファイルのテンプレート

## 5. 主な機能

- 複数のセンサーからのデータ同時取得
- センサー設定のカスタマイズ（ゲイン、サンプリングレートなど）
- WAVファイル形式でのデータ保存
- センサーゲインのキャリブレーション

## 6. 依存関係

- libyaml-dev
- libsndfile1-dev
- python3-ruamel.yaml

## 7. ライセンス

Copyright (C) 2023 Tokuyama Corporation, Easy Measure Inc., and toor Inc. All rights reserved.

このソフトウェアは、徳山株式会社、Easy Measure株式会社、およびtoor株式会社間の契約条件に基づいて、徳山株式会社に独占的にライセンスされています。
このソフトウェアの無許可での使用、コピー、配布、または修正は厳重に禁止されています。

注意：このライセンス情報は草案段階であり、最終的な使用条件については関係者間で確認が必要です。



