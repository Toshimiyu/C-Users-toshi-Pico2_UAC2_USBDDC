# Pico2 USB-DDC : LUFA → TinyUSB UAC2 移行 仕様書（作業指示書 Rev.3 Final 対応）

ベース: 添付の TinyUSB `examples/device/uac2_speaker_fb`（`tusb_config.h` / `usb_descriptors.*` / `common_types.h` / `uac2_callbacks.c`）。
DSP / DMA / PIO-I2S / RingBuffer / FFT / 表示（変更禁止リスト）は **1 行も変更していない**。

---
## 0. 実装前に知っておくべき事項（重要）

1. **Full-Speed 帯域の物理制約（Rev.3 で正式な対応範囲として確定）**
   RP2350 の USB は Full-Speed 専用で、Isochronous の最大パケットは 1023 byte/ms。
   | | 16bit | 24bit | 32bit |
   |---|---|---|---|
   | 44.1/48k | 192B | 288B | 384B |
   | 88.2/96k | 384B | 576B | 768B |
   | 176.4/192k | 768B ○ | 1058–1152B ✕ | 1411–1536B ✕ |

   → **176.4k/192k が使えるのは 16bit のみ**。24/32bit は 96k まで。
   記述子はこの範囲で宣言（EP サイズ 772 / 582 / 776）。ホストが 24/32bit + 176.4k/192k を選ぶとデバイスは
   `rate_limit_violation` を加算して無音（IDLE）にする。alt を開いた後にレートだけ変更された場合も同様に再判定する
   （Rev.3 チェックリスト「帯域制限確認」に対応）。Rev.3 では 24/32bit × 176.4k/192k は正式対応対象外。
2. **TinyUSB のバージョン**: ソースは添付ファイルと同じ master 系の名前（`TUD_AUDIO20_*` / `AUDIO20_*`）で記述し、
   `uac2_compat.h` で pico-sdk 2.3.1 同梱の TinyUSB（旧名 `AUDIO_*`）にも対応させた。**SDK 同梱版・TinyUSB master のどちらでも
   ビルドでき、生成される構成記述子（258 B）はバイト単位で一致**することを確認済み。外部の TinyUSB を使う場合のみ
   `PICO_TINYUSB_PATH` を指定する（§9）。
3. **実機未検証**: 検証したのは (a) SDK 同梱版 / TinyUSB master の両方に対する `-Wall -Wextra` コンパイル、
   (b) スタブ付きリンク、(c) 生成された構成記述子の構造解析（総長 258B、3 alt、EP 属性）まで。
   Windows 実機再生・長時間試験は未実施（§12 の対応表で「実機確認」と記した項目がすべて該当）。
4. **既存コードとの衝突（変更禁止のため未対応）**: `transmit_to_dac.c` は入力枯渇時に `enable_output=false → dma_stop_and_clear()`
   を実行する。「DMA/PIO 停止禁止・無音出力継続」は、この既存挙動と完全には両立しない（Rev.3 でも「目標」「Phase2 課題」扱い）。
   USB 側の状態機械は UNDERFLOW を検出して再蓄積するが、DMA 停止そのものは既存ロジックのまま。必要なら Phase2 で
   `transmit_to_dac.c` に無音注入を追加する。
5. **既存の潜在不具合（未変更）**: `ringbuffer.c` の `inner_*_point > size_buffer`（`>=` であるべき）。`write_point==size_buffer`
   となる境界があり、単サンプル API を併用すると範囲外書込みの恐れ。本移行ではバッファ API を配列書込みのみで使用。

---
## 1. 変更ファイル一覧

| ファイル | 内容 |
|---|---|
| `main.c` | `boot_wait_ms()` 追加（起動演出中も `tud_task` を回す）、メインループ先頭に `usb_audio_task()` |
| `CMakeLists.txt`（top） | `PICO_USBDEV_*` 定義を削除、`PICO_TINYUSB_PATH` 自動設定を追加 |
| `src/CMakeLists.txt` | `usb_descriptors.c` `uac2_callbacks.c` 追加、`lufa` include 削除、`usb_device` → `tinyusb_device` `pico_unique_id` |
| `usb_device_control.c/.h` | **全面書換**（LUFA / pico-extras usb_device を完全排除。PCM 受信・Feedback 制御・状態機械） |

## 2. 新規作成ファイル一覧

| ファイル | 内容 |
|---|---|
| `tusb_config.h` | 添付ベース。FS 固定、Audio RX / Feedback EP 有効、SW FIFO = 16 × 776 byte |
| `usb_descriptors.h/.c` | 添付ベース。UAC2 FS、3 alt（16/24/32bit）、Feedback EP 4B、シリアル = RP2350 固有 ID |
| `uac2_callbacks.c` | 添付の同名ファイルを置換（クロック / フィーチャユニット / alt / Feedback 設定） |
| `tools/fb_sim.py` | Feedback 制御（PI）の簡易シミュレータ。期待ログの確認・定数調整用（ホストは Feedback に理想的に追従すると仮定） |
| `uac2_compat.h` | 旧 TinyUSB（SDK 2.3.1 同梱）向け名前変換。新しい TinyUSB では何もしない |
| `common_types.h` | 添付のまま |

削除対象: LUFA 関連（`AudioClassCommon.h`、`../lufa`）、添付の `main_1.c`（サンプルの main。不使用）。
添付の旧 `uac2_callbacks.c` は存在しない `audio_ringbuf.h` / `fft_task.h` を参照し、`tud_audio_feedback_packet_rate_cb`
という存在しない API を使い、満杯時にデータを破棄して stream を落とす実装だったため使用していない。

VID/PID: VID `0x2e8a`（従来どおり）、PID を **`0xfede` に変更**（UAC1→UAC2 でクラスが変わるため、Windows の旧ドライバ
キャッシュを避ける）。シリアルも固有 ID に変更。

---
## 3. USB Descriptor 構成図

```
Device  (USB 2.0, Misc/Common/IAD, VID 2e8a PID fede)
└ Configuration 1  (258 B, 100 mA)
  └ IAD (IF0-1)
    ├ IF0  AudioControl (UAC2)
    │   ├ Clock Source  ID4  内部可変, 周波数 R/W
    │   ├ Input Terminal ID1  USB Streaming, 2ch
    │   ├ Feature Unit  ID2  Master: Mute + Volume（ch1/ch2 制御なし）
    │   └ Output Terminal ID3 Speaker
    └ IF1  AudioStreaming
        ├ alt0  帯域 0
        ├ alt1  16bit (slot 2B)  OUT EP1 iso/async 772B  + FB EP 0x81 iso/feedback 4B, 1ms
        ├ alt2  24bit (slot 3B)  OUT EP1 iso/async 582B  + FB EP 0x81
        └ alt3  32bit (slot 4B)  OUT EP1 iso/async 776B  + FB EP 0x81
```
対応レート: 44100 / 48000 / 88200 / 96000 / 176400 / 192000（クロックソースの RANGE で列挙）。

## 4. データフロー図

```
Windows ─UAC2 iso OUT─▶ TinyUSB 内部 SW FIFO (12.4 KB, ISR で充填)
                              │  usb_audio_task() / Core0 メインループ
                              ▼
                  PCM 変換 (16/24/32bit → float, int32 フルスケール)
                  × (vol_float × DEFAULT_GAIN_RATIO × FIR ゲイン補正)
                  ピーク計算 → display_rta_set_peak()
                              ▼
        buffer_ep_Lch / buffer_ep_Rch   ← 既存 DSP との接続点（維持）
                              ▼
                 upsampling_process_core0()（既存）
                              ▼
   buffer_upsr_data_Lch_0 / Rch_0 ──▶ Core1 dma_tx_start()（既存）─▶ DMA 2ch チェーン ─▶ PIO-I2S 32bit ─▶ PCM5102A
              │
              └─ 充填量 ─▶ PI 制御 ─▶ tud_audio_fb_set() ─▶ Feedback EP ─▶ Windows
```
`buffer_ep_*` への書込みは Core0 メインループのみ（旧実装は USB 割込み内）。読出し側も Core0 のため、書込み側は
排他不要になり、旧実装の割込み/メイン間レースがなくなった。`buffer_ep` が満杯なら FIFO に残して背圧とする。

## 5. TinyUSB コールバック一覧

| コールバック | 場所 | 役割 |
|---|---|---|
| `tud_audio_get_req_entity_cb` | uac2_callbacks.c | クロック CUR/RANGE/VALID、Mute/Volume CUR/RANGE |
| `tud_audio_set_req_entity_cb` | 〃 | サンプルレート設定 → `usb_audio_apply_sample_rate()`、Mute/Volume 設定 |
| `tud_audio_set_itf_cb` | 〃 | alt1-3 → `usb_audio_stream_open(alt)`、alt0 → close |
| `tud_audio_set_itf_close_ep_cb` | 〃 | alt0 でストリーム終了 |
| `tud_audio_feedback_params_cb` | 〃 | 方式 = `AUDIO_FEEDBACK_METHOD_DISABLED`（標準の FIFO カウント方式を使わない） |
| `tud_umount_cb` | 〃 | 抜去時に IDLE へ |
| （ポーリング）`tud_task` / `tud_audio_read` / `tud_audio_fb_set` | usb_device_control.c | メインループから呼出し |

## 6. Feedback 制御仕様書

- 制御量: `buffer_upsr_data_Lch_0` の使用量（Core0 アップサンプル後サンプル数）
- 目標: `SIZE_BUFFER_FB_THRESHOLD`（= 4096 / 8192 = 50 %）
- 出力: 要求サンプルレート `fs + adj`（Hz）→ 16.16 形式 `fs_req × 65.536` を 1 ms ごとに `tud_audio_fb_set()`（4 byte）
- 更新周期 1 ms。レベルは 64 ms 時定数のローパスで平滑（Core0/DMA のブロック単位アクセスによるノコギリ波を除去）
- 誤差 `e = (TARGET − level_f) / ratio` [入力サンプル]（正 = 不足 → 多く要求）
- `adj = Kp·e + ∫Ki·e dt`、`Kp = 0.50 Hz/sample`、`Ki = 0.0625 Hz/(sample·s)`（ζ≈1, ωn≈0.25 rad/s）
- 補正量は **±0.3 %（3000 ppm）かつ ±`FB_ADJ_LIMIT`(1000 Hz) 以内**に制限。飽和時は積分を停止（アンチワインドアップ）
- USB ストリームが開いている間は BUFFERING / STREAMING の両方で同じ水位 PI 制御を継続し、STRM 遷移時に積分値を維持する（アンダーフロー復帰時に公称レートだけで水位が目標へ戻らなくなるのを防ぐ）
- 旧実装は `(閾値 − 使用量)/ratio` を ±1000 Hz にクランプする純比例制御（最大 ±2 % 超の大振幅）だった。
- 禁止事項の確認: 固定値でない / FIFO 空・満杯を直接使わない / 大振幅補正なし
- 調整用定数は `usb_device_control.c` 冒頭（`FB_KP` `FB_KI` `FB_LEVEL_TAU_S` `FB_LIMIT_PPM`）

## 7. サンプルレート切替仕様書

`SET_CUR(clock)` を受信 → 対応レート以外は STALL。変更ありの場合:
1. 状態を IDLE にして新規データを止める
2. `audio_state.freq` 更新
3. （ES9038Q2M 使用時）`ess_dac_mute()`
4. `renew_clock(is_high_power_mode)`：`set_sys_clock_khz()`（44.1k 系 = `SYS_CLOCK_KHZ_44K` / 48k 系 = `SYS_CLOCK_KHZ_48K`）→ `dma_stop_and_clear()` → `reset_i2s_freq()`
5. `buffer_ep_L/R`・`buffer_upsr_data_L/R_0` クリア、`clear_bq_filter_delay()`、TinyUSB FIFO クリア
6. Feedback 制御リセット
7. ストリーム open 中なら BUFFERING（規定水位到達で STREAMING）

alt 変更（ビット深度変更）時は `buffer_ep_*` と FIFO をクリアして BUFFERING。`audio_state.bit_depth` を更新し、
既存の表示更新（main.c）がそのまま動作する。

## 8. 状態遷移図 / RingBuffer 構成図

```
          alt1-3 open                level ≥ TARGET(4096)
 IDLE ───────────────▶ BUFFERING ─────────────────────────▶ STREAMING
  ▲  ▲                    ▲  ▲                                 │   │
  │  │ alt0 / 抜去         │  │ level ≤ TARGET+1024             │   │ level ≤ 256 (1/32)
  │  └────(全状態から)─────┘  └───── OVERFLOW_FATAL ◀──────────┘   ▼
  │                         │        level ≥ 7936 (31/32)    UNDERFLOW_FATAL
  │                         └──────────────────────────────────── │
  │                              level ≥ TARGET/2 (2048) で BUFFERING
```
- OVERFLOW_FATAL: 新規 USB データを破棄。既存バッファは DMA が消費し続ける（DMA/PIO は停止しない）
- UNDERFLOW_FATAL: USB データは蓄積し続ける。（DMA 停止については §0-4）

```
TinyUSB SW FIFO (12.4KB, ISR) → buffer_ep_L/R (896 samples, 排他不要) → buffer_upsr_L/R_0 (8192)
                                                                     0 ──256──────4096(TARGET)────7936── 8192
                                                                      UNDER   BUFFERING/STREAMING 域  OVER
```
RingBuffer 実装・サイズ（`SIZE_EP_BUFFER` `SIZE_UPSAMPLE_CORE0`）は不変。mutex / semaphore / queue への置換なし。

---
## 9. コンパイル手順

1. 変更・新規ファイルを `src/` に、top の `CMakeLists.txt` を差し替えて反映（`tusb_config.h` は `src/` に置く。
   `target_include_directories` の `${CMAKE_CURRENT_LIST_DIR}` で解決される）
2. **Pico SDK 2.3.1 同梱の TinyUSB でそのままビルドできる**（追加作業なし）。
   新しい TinyUSB を使いたい場合のみ、プロジェクト直下に `git clone https://github.com/hathach/tinyusb tinyusb` を置くか
   `-DPICO_TINYUSB_PATH=...` を指定（top の CMakeLists.txt は直下に `tinyusb/` があれば自動使用）
3. `lufa/` ディレクトリと `AudioClassCommon.h` はもう不要（削除可）。CMake を再構成してビルド
4. 診断出力: `-DUSB_AUDIO_DEBUG_PRINT=1`（または `usb_device_control.c` 冒頭の定義）で UART に 1 行/秒。
   **水位推移の評価（最重要項目）はこの出力で行う**（読み方は §13）

## 10. 動作確認手順

1. 書込み後、Windows のデバイスマネージャで「サウンド、ビデオ、およびゲーム コントローラー」に UAC2 として認識されること
   （PID 変更により新規デバイス扱い）。サウンド設定で 44.1〜96 kHz × 16/24/32bit、および 16bit のみ 176.4/192 kHz が選べる
2. 各フォーマットで再生し、`USB_AUDIO_DEBUG_PRINT=1` で `st=2`（STREAMING）、水位が 4096 付近で安定、
   `ovf=0 udf=0 rsy=0` であることを確認
3. 水位の min/max が ±数百サンプル以内に収まること（収束までは 1 分程度。徐々に FB 値が一定に近づく）
4. レート切替（44.1 ↔ 48 ↔ 176.4/192k）を繰り返し、ポップ音・停止がないこと、PCM5102A 出力が正常であること
5. 1 時間以上の連続再生で `ovf` `udf` `rsy` が増えないこと
6. FFT 表示（L/R 独立）が更新されること、再生停止 200 ms 後にレベルがリセットされること
7. 24/32bit で 176.4k/192k を強制選択した場合（alt 選択後のレート変更も含む）に `rate_limit_violation` が増え、無音になる（ハング・再起動しない）こと

## 11. 未検証・既知のリスク

- 実機（Windows 11 / UAC2 FS / 4B 16.16 Feedback）での列挙と同期動作。TinyUSB のドキュメントでは Windows/Linux で 16.16・4B の組合せ動作とされているが、本機では未確認
- メインループ 1 周が長い構成（Core0 FFT-FIR 比率 > 1、TFT 描画）では `tud_task` の呼出し間隔が伸びる。FIFO は 16 ms 分確保してあり、
  溢れそうな場合は `fifo_resync_count` で検出して再同期する（L/R 入れ替わり防止）
- Windows の音量 UI は 1 dB 刻み・-64〜0 dB（従来仕様を踏襲）。ミュート解除時に音量が復帰しない旧実装の不具合を、`volume_control()` を
  ミュート ON/OFF 双方で呼ぶよう修正
- `common.h` の `inline` 関数（`-Ofast` 前提でインライン展開される）や `ringbuffer.c` は変更していない

---
## 12. Rev.3 チェックリスト対応表

凡例: ◎ ビルド・構造解析で確認済み / ○ 実装済み（実機確認が必要） / △ 既存コード制約により部分対応 / － 既存機能（未変更）

| 項目 | 状態 | 備考 |
|---|---|---|
| SDK 2.3.1 でビルド / TinyUSB 有効化 | ◎ | 同梱 TinyUSB・master の両方で確認（スタブリンクまで） |
| UAC2 認識・AC/AS/Feedback EP・名称 | ◎/○ | 記述子構造は解析済み。Windows での認識は実機確認 |
| 16/24/32bit alt、全レート宣言 | ◎ | alt1-3、クロック RANGE に 6 レート |
| `tud_audio_read()` 受信、16/24/32bit→内部形式 | ○ | 変換式は旧実装と同一（24bit は左詰め） |
| `buffer_ep_L/R` 投入、L/R 分離、左右逆転なし | ○ | フレーム単位で処理、FIFO 異常時は再同期 |
| `audio_state.freq/bit_depth` 更新 | ◎ | |
| 44.1k系 336 MHz / 48k系 360 MHz、`reset_i2s_freq()` | ○ | `renew_clock()` 経由（既存関数をそのまま使用） |
| 切替時のバッファクリア・Feedback リセット・BUFFERING 復帰 | ○ | §7 の順序 |
| DSP / FFT / Core1 / DMA / PIO-I2S 維持 | － | 該当ファイルは無変更 |
| Async Feedback（水位 PI 制御、固定値でない、FIFO 空/満杯非依存） | ○ | §6。水位推移は UART 診断で確認 |
| 状態機械 5 状態と 6 遷移 | ○ | §8 |
| OVERFLOW：新規データ破棄・既存バッファ消費継続 | ○ | |
| OVERFLOW/UNDERFLOW 中の DMA/PIO 継続・無音継続 | △ | 既存 `dma_tx_start()` が枯渇時に停止（§0-4、Phase2） |
| 24/32bit × 176.4k/192k 要求時の安全動作・`rate_limit_violation` | ○ | 無音（IDLE）、再判定ロジックを今回追加 |
| Audio > FFT > Display | － | `usb_audio_task()` は表示処理より前に実行 |
| 長時間試験（30 分〜8 時間）、ポップノイズ、再接続 | 実機 | |

---
## 13. 実機評価手順（水位ログの読み方）

評価の本体は **`ovf/udf` ではなく水位の推移**。`-DUSB_AUDIO_DEBUG_PRINT=1` でビルドし、UART ログを取る。

```
usb st=2 fs=44100 bit=16 avg=4096 d=+0 [4048..4144] integ=8.82 fb=44108.82 ovf=0 udf=0 rsy=0 lim=0 lvl=4101
```

### 13.1 見る順番
1. `st` … 2（STREAMING）の行だけを評価対象にする（`avg/d/min/max` は STREAMING 中のみ有効）
2. `avg` … 1 秒窓平均。TARGET（4096）付近か
3. `d` … `avg` の 1 秒あたり変化量。符号が一方向に続けばドリフト
4. `integ` … PI 積分項 [Hz]。ホストとのクロック差を学習した値。一定値に落ち着くか
5. `fb` … 要求レート [Hz]。`fs ± 数十 Hz` で安定するか
6. `ovf / udf / rsy / lim` … 最後に確認

### 13.2 期待値（`tools/fb_sim.py` の結果。理想ホスト）
- 収束時間の目安は 20〜40 秒（PI ゲインを旧値から4倍に変更）。**最初の 60 秒の `d` の符号変化は判定に使わない**（過渡応答）。
- 収束後: `avg` ≒ 4096（±数サンプル）、`d` ≒ 0、`integ` = クロック誤差 × fs（例: 44.1 kHz・200 ppm → 約 8.8 Hz、192 kHz・300 ppm → 約 58 Hz）
- `min/max` の幅: 44.1 kHz で ±50、192 kHz で ±220 程度（Core1 の 2.2 ms ブロック単位の読み出しによる。TARGET ± 数百以内なら正常）

### 13.3 異常パターンと原因の推定
| 症状 | 推定 |
|---|---|
| `avg` が一方向にドリフトし `integ` が上限（±fs×3000ppm）に張り付く | 制御限界。`fb` が変化していても止まらない → ホストが Feedback を無視／読み違え |
| `fb` が公称レート付近のまま動かない（`integ` も動かない） | 制御が働いていない（水位計測・状態の確認） |
| STREAMING 直後に OVERFLOW（FIXED でも公称値で再現） | 16.16 / 10.14 の解釈違い（4 倍ずれ）の疑い |
| `avg` が TARGET 付近で安定、`integ` が極端に大きい値（数千 ppm 相当）で固定 | Feedback 形式のスケールずれの疑い（制御の見かけ上の成功に注意） |
| `rsy` が増える | メインループが詰まっている（FIFO 再同期。L/R 入替わり防止のため破棄） |
| `lim` が増える | 24/32bit で 176.4k/192k が要求されている（仕様どおり無音） |

### 13.4 診断専用ビルド `FIXED_FEEDBACK_TEST`（製品では使用禁止）
Feedback 制御を OFF にし、`fs + FIXED_FEEDBACK_OFFSET_HZ` を固定で要求する。ログの先頭が `usb(FIXED_FB)` になる。
コンパイル時に `#warning` が出る（意図した警告）。
```
-DUSB_AUDIO_DEBUG_PRINT=1 -DFIXED_FEEDBACK_TEST=1 -DFIXED_FEEDBACK_OFFSET_HZ=0.0f      # ① 自然ドリフトの測定
-DUSB_AUDIO_DEBUG_PRINT=1 -DFIXED_FEEDBACK_TEST=1 -DFIXED_FEEDBACK_OFFSET_HZ=100.0f    # ② 既知オフセットへの応答
```
- ①の `d` が自然ドリフト（≒ −fs × クロック誤差）。OVERFLOW/UNDERFLOW に達するまでに状態機械の保護が働く
- ②の `d` が ① から **約 +100 samples/s（ratio=1 のとき）** 変われば、ホストは Feedback を読み、スケールも正しい
- 変わらなければ Feedback 無視、4 倍/¼ 倍なら形式の解釈違い
- 通常ビルド（PI 制御）で ① と比べ、`avg` が収束すれば「Feedback が効いている」と判断できる

---
## 14. 実機で起動・認識しない場合の切り分け（デバッグ機材なし）

`CMakeLists.txt`（top）の `USB_BRINGUP_LEVEL` を変えて 3 回書き込み、結果を比べる。

| レベル | 内容 | 確認すること |
|---|---|---|
| 0 | USB 完全 OFF（元の起動フローのみ） | 起動画面 → FFT 表示まで進むか。**進まないなら USB コード以外の問題** |
| 1 | TinyUSB 初期化 + `tud_task()` のみ | PC のデバイスマネージャに UAC2 機器が現れるか |
| 2 | フル（既定） | 再生・水位ゲージ |

- 起動の流れ: 電源 ON → 起動画面 → 裏で USB 接続 → 起動画面終了 → FFT 表示（`boot_wait_ms()` が待機中も USB を処理する）
- UART は使用しない: `stdout_uart_init()` と `printf` 診断出力を削除、`pico_enable_stdio_uart/usb(... 0)` を有効化。
  水位の確認は LCD のゲージ（`debug_gauge.c`）で行う
- Core0/Core1 のスタックを 2 KB → 4 KB に拡張（`PICO_STACK_SIZE` / `PICO_CORE1_STACK_SIZE`）。
  既存の TFT 描画（`lcd_draw_char`）だけで約 1.3 KB を使い、USB/タイマ割り込みが重なると 2 KB では余裕がないため
- 既知の点: `debug_gauge.c` の初版は `snprintf` を使っており、このプロジェクトのリンク設定では未解決参照になる（修正済み）
