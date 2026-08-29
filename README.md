# PicoApple2 Keyboard Tester

在 RP2040 上跑的實體鍵盤檢查器，用來回答一個問題：

> **8×8 掃描矩陣的座標 `[row][col]`，與實體鍵盤上那顆鍵，究竟怎麼對應？**

這是 [PicoApple2](https://github.com/pondahai/PicoApple2) 的姊妹工具。主專案的鍵盤轉換表是多年前寫下的，沒人確定它是否真的貼合硬體佈線 —— 這支程式把答案量出來。

不含 6502 模擬、磁碟、音訊，只有鍵盤掃描與 LCD 顯示。

## 設計原則：表是待驗證的基準，不是待修的東西

`keymap_base` / `keymap_shift` 兩張表與掃描時序，皆自 `PicoApple2.ino` **原封搬移，一個位元都沒改**。

程式把兩件事**分開儲存**，衝突才會自己浮出來：

| | 意義 |
|---|---|
| `expect` | **表怎麼說** —— 照 `keymap_base` 的內容推論，這顆鍵應該落在哪一格 |
| `rc` | **硬體實際如何** —— 實測確認的矩陣座標 |

兩者不一致，就是轉換表與實體佈線錯位。

## 實測結果

在一台 64 鍵（5 列 × 13 格，SPACE 佔兩格 = 恰好 8×8）的機器上跑完，`map is complete and one-to-one`。發現如下。

### 兩個孤兒解開了

表中有兩個保留碼從來沒有對應的轉換分支，按下去會把原始值當 ASCII 送出：

| 表中值 | 位置 | 實際是 | 目前送出 |
|---|---|---|---|
| `206` | `[0][6]` | **TAB** | `0xCE` |
| `208` | `[2][7]` | **CAPS LOCK** | `0xD0` |

### 4 組錯位

**① S / X 對調**

```
[6][0]  硬體 = S    表寫 'x'
[7][0]  硬體 = X    表寫 's'
```

**② `/` 與 PGUP 連鎖錯位**

```
[7][4]  硬體 = /     表寫 213 (BTN0)  → 按 / 什麼都不送（k 被歸零）
[5][7]  硬體 = PGUP  表寫 '/'         → 按 PGUP 會送出 /
```

**③ `=` 與 PGDN 連鎖錯位**

```
[4][5]  硬體 = =     表寫 214 (BTN1)  → 按 = 什麼都不送
[6][7]  硬體 = PGDN  表寫 '='         → 按 PGDN 會送出 =
```

②③ 是同一種模式：搖桿鈕 213/214 被放在實際上是 `/` 和 `=` 的位置，而 `/` 和 `=` 又被推到 PGUP/PGDN 的位置上。

**④ DEL 未定義**

`[4][7]` 硬體是 DEL，表中為 `0` → 按下去不送任何東西。（`[3][7]`=FN、`[7][7]`=ALT 同樣是 `0`，但那是對的：兩者由韌體另外處理。）

### 完整真值表

```
實體鍵 → 矩陣座標
ESC [1][6]   TAB [0][6]   CAPS [2][7]  SHIFT [3][5]  CTRL [2][6]
1   [0][0]   Q   [1][0]   A    [2][0]  Z     [3][0]  FN   [3][7]
2   [4][0]   W   [5][0]   S    [6][0]  X     [7][0]  ALT  [7][7]
3   [0][1]   E   [1][1]   D    [2][1]  C     [3][1]  PGUP [5][7]
4   [4][1]   R   [5][1]   F    [6][1]  V     [7][1]  `    [4][6]
5   [0][2]   T   [1][2]   G    [2][2]  B     [3][2]  SPC  [7][5]
6   [4][2]   Y   [5][2]   H    [6][2]  N     [7][2]  =    [4][5]
7   [0][3]   U   [1][3]   J    [2][3]  M     [3][3]  PGDN [6][7]
8   [4][3]   I   [5][3]   K    [6][3]  ,     [7][3]  DEL  [4][7]
9   [0][4]   O   [1][4]   L    [2][4]  .     [3][4]  LEFT [7][6]
0   [4][4]   P   [5][4]   ;    [6][4]  /     [7][4]  DOWN [3][6]
-   [0][5]   [   [1][5]   '    [2][5]  UP    [5][6]  RIGHT[6][6]
BS  [0][7]   ]   [5][5]   \    [1][7]  ENTER [6][5]
```

佈局規律是**奇偶列交錯**：r0/r4 是數字、r1/r5 是 QWERTY 上排、r2/r6 是中排、r3/r7 是下排。col 5–7 集中放修飾鍵與功能鍵。

## 兩種模式

序列埠 115200，單鍵命令：

| 鍵 | 作用 |
|---|---|
| `F` | FREE 自由按壓 |
| `G` | GUIDED 導引學習 |
| `S` | 導引模式中跳過目前這顆鍵 |
| `D` | 印出學到的對照表 |
| `M` | 印出「表 vs 硬體」分析與完整性檢查 |
| `R` | 清空已學結果 |

### FREE —— 看單顆鍵的完整履歷

按下任一鍵，畫面顯示：

- `MATRIX [r][c]` —— 掃描實際看到的座標
- `TABLE BASE / SHFT` —— 表中該格的原始值
- `SENDS PLAIN / SHIFT / CTRL` —— 經過轉換鏈後真正會餵進 Apple II 閂鎖的 byte

圖上同時高亮：**綠**＝位置已實測確認，**橘**＝位置照表推論（未經驗證）。橘色亮到錯的鍵上，就是當場看見錯位。

### GUIDED —— 從零建出真值表

掃描只知道 `[r][c]`，不知道你按的是哪顆實體鍵，所以無法單靠按壓反推。導引模式把 64 顆鍵依序標紅要你照著按，逐一記錄，走完一輪即得完整對應。

按 `M` 會做完整性檢查：64 格是否被一對一佔滿、有沒有無主的格子或衝突。

### Fn 組合鍵

`Fn+1~9` = F1~F9、`Fn+0` = F10（F11 / F12 未定義），`Fn+C` = CapsLock 切換。

檢查器**不執行**這些動作，只把它解讀出來顯示，底層掃描結果照舊呈現 —— 這樣才看得到真相。括號內標出主韌體實際會做的事（F1 warm reset、F2 cold reset、F3 磁碟選單、F4 joystick、F5 變速）。

## 顏色

鍵帽有兩個獨立維度：**填色**是學習／按壓狀態，**外框**是這顆鍵在 Apple II+ 上存不存在。

### 填色 —— 狀態

| 色 | 意義 |
|---|---|
| 深灰 | 尚未學到 |
| 深藍 | 已學到 |
| 紅 | 導引模式的目標鍵 |
| 綠 | 正按住（依已學到的真值表） |
| 橘 | 正按住（照表推論，未經實測） |

### 外框 —— 是不是 Apple II+ 原生鍵

| 色 | 意義 |
|---|---|
| 淺灰 | Apple II+ 鍵盤上就有的鍵（48 顆） |
| 暗灰 | 這台機器加的，II+ 沒有（16 顆） |

PicoApple2 模擬的是 **Apple II+**：`apple2_sys.rom` 是 `$D000-$FFFF` 共 12KB（IIe 是 16KB），
字元 ROM 是無小寫的大寫 64 字集，開機進 Applesoft 的 `]` 提示符。II+ 的鍵盤是：

```
ESC 1 2 3 4 5 6 7 8 9 0 : -  RESET
    Q W E R T Y U I O P      REPT  RETURN
CTRL A S D F G H J K L ;  <- ->
SHIFT Z X C V B N M , . /  SHIFT
              SPACE
```

所以這台 64 鍵機器上，`BS TAB [ ] CAP ' \ UP FN ALT PGU ` = PGD DEL DN` 這 16 顆是後加的。
反過來 II+ 有而這台沒有的（RESET、REPT、獨立的 `:` 鍵）不在檢查器範圍內 —— 兩邊不是一一對應。

這只是視覺標記，**不影響掃描或轉換**：外框暗灰的鍵照樣會掃、照樣會依表送出 byte。
標出來是因為那些鍵送進 Applesoft 多半只會換到 `SYNTAX ERROR`，知道歸屬才不會把它誤判成佈線問題。

## 硬體

Raspberry Pi Pico (RP2040) + ILI9341 320×240 SPI 顯示器 + 8×8 鍵盤矩陣（74HC595 驅動列、74HC165 讀行）。接腳與 PicoApple2 完全相同：

| 用途 | GPIO |
|---|---|
| 矩陣 DATA_OUT / LATCH / CLOCK / DATA_IN | 15 / 14 / 26 / 27 |
| 顯示器 SCK / MOSI / CS / DC / RST / BL | 18 / 19 / 17 / 20 / 21 / 22 |

## 建置

### 1. 字元 ROM

畫面文字用真正的 Apple II 字元 ROM 渲染。**ROM 受著作權保護，不在本倉庫內**，請自備 `apple2_char.rom`（2048 bytes）：

```bash
python tools/rom2h.py /path/to/apple2_char.rom
```

產生的 `KeyboardTester/apple2_char_rom.h` 已列入 `.gitignore` —— 它只是 ROM 的另一種編碼，同樣不能公開。

沒有 ROM 也能編譯（`__has_include` 會退回全零佔位），只是畫面沒有文字。

### 2. 編譯

一般版，連結在 `0x10000000`，可獨立開機：

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipico KeyboardTester
```

### 3. 偏移版（可選）

若板子的前 16KB 放了 [rp2040-retro-loader](https://github.com/pondahai/rp2040-retro-loader) 這類 SD 卡載入器，要改連結到 `0x10004000` 才不會蓋掉它：

```bash
python loader_offset/gen_app_ld.py    # 依你的 arduino-pico 版本重新產生 linker script
arduino-cli compile --fqbn rp2040:rp2040:rpipico \
  --build-property "recipe.hooks.linking.prelink.1.pattern=\"{runtime.tools.pqt-python3.path}/python3\" -I \"{runtime.platform.path}/tools/simplesub.py\" --input \"$PWD/loader_offset/memmap_app_arduino.ld\" --out \"{build.path}/memmap_default.ld\" --sub __FLASH_LENGTH__ {build.flash_length} --sub __EEPROM_START__ {build.eeprom_start} --sub __FS_START__ {build.fs_start} --sub __FS_END__ {build.fs_end} --sub __RAM_LENGTH__ {build.ram_length} --sub __PSRAM_LENGTH__ {build.psram_length}" \
  --output-dir build_offset KeyboardTester
python loader_offset/check_flash_layout.py build_offset/KeyboardTester.ino.uf2
```

產出的 uf2 只寫 `0x10004000` 之後的區塊，前 16KB 的載入器原封不動 —— 直接拖進 BOOTSEL 磁碟即可。

RP2040 跑 XIP，每個位址都在編譯時就烙進機器碼裡，所以搬檔案沒用，映像必須重新連結。

## 授權

GPL-3.0（見 `LICENSE`）。`TFT_DMA.cpp/.h` 取自 PicoApple2。

## 選單圖示（retro-loader）

`assets/KeyboardTester.ino.RAW` 是給 [rp2040-retro-loader](https://github.com/pondahai/rp2040-retro-loader)
選單用的封面圖：96x96 RGB565 big-endian、無標頭、固定 18432 bytes。載入器只用檔案長度驗證，
所以長度必須剛好。

用法：把它跟 `KeyboardTester.ino.uf2` 一起放 SD 卡**根目錄**（不支援子目錄）。檔名規則是
UF2 去掉副檔名再接 `.RAW`，也就是 `KeyboardTester.ino.uf2` 配 `KeyboardTester.ino.RAW`。

要從原圖重新產生：

```bash
python ../rp2040-retro-loader/tools/make_thumb.py assets/kbtester_icon.jpg --fit -o assets/KeyboardTester.ino.RAW
```
