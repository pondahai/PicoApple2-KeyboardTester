# PicoApple2 Keyboard Tester

在 RP2040 上跑的實體鍵盤檢查器，用來回答一個問題：

> **8×8 掃描矩陣的座標 `[row][col]`，與實體鍵盤上那顆鍵，究竟怎麼對應？**

這是 [PicoApple2](https://github.com/pondahai/PicoApple2) 的姊妹工具。主專案的鍵盤轉換表是多年前寫下的，沒人確定它是否真的貼合硬體佈線 —— 這支程式把答案量出來。

不含 6502 模擬、磁碟、音訊，只有鍵盤掃描與 LCD 顯示。

## 設計原則：表是待驗證的基準，不是待修的東西

`keymap_base` / `keymap_shift` 兩張表與掃描時序，皆自 `PicoApple2.ino` **原封搬移，一個位元都沒改**。

> **注意：兩張表是「修正前」的基準快照。** 測出來的錯位都已回饋進主韌體，
> `PicoApple2.ino` 現在的表跟這裡的副本有 7 格不同。檢查器刻意**不跟進** —— 保留當初
> 那份表，才能重現當時的判斷、也才能拿來驗下一台機器的佈線是不是同一種接法。
>
> **`translate()` 則相反，與主韌體同步。** 它不是待驗證的對象，而是量測的工具：
> 要看「這顆鍵現在真的會送出什麼」，就得用現行的轉換鏈。所以 `206` → `0x09`（TAB）、
> `208` → CapsLock 這兩個分支有跟上。唯一的差異是 `208` 在這裡**不切狀態只顯示** ——
> 畫面每次重繪都會呼叫 `translate()` 三次（PLAIN / SHIFT / CTRL），帶副作用的話畫一次
> 就把 CapsLock 翻三次。這也符合檢查器既有的原則：Fn 組合鍵同樣只解讀不執行。
>
> 逐項狀態見〈實測結果〉。

程式把兩件事**分開儲存**，衝突才會自己浮出來：

| | 意義 |
|---|---|
| `expect` | **表怎麼說** —— 照 `keymap_base` 的內容推論，這顆鍵應該落在哪一格 |
| `rc` | **硬體實際如何** —— 實測確認的矩陣座標 |

兩者不一致，就是轉換表與實體佈線錯位。

## 實測結果

在一台 64 鍵（5 列 × 13 格，SPACE 佔兩格 = 恰好 8×8）的機器上跑完，`map is complete and one-to-one`。發現如下。

**回饋狀態一覽**（對照 `PicoApple2.ino` 目前的表與轉換鏈）：

| 發現 | 狀態 |
|---|---|
| ① S / X 對調 | ✅ 已修 |
| ② `/` 與 PGUP 錯位 | ✅ 已修 |
| ③ `=` 與 PGDN 錯位 | ✅ 已修 |
| ④ DEL 未定義 | ✅ 已修（`[4][7]` 現為 `0x7F`） |
| 孤兒碼 `206`（TAB） | ✅ 已修（`translate()` 加分支 → `0x09`） |
| 孤兒碼 `208`（CAPS LOCK） | ✅ 已修（`translate()` 加分支 → 切 CapsLock） |

六項全數回饋完畢，來源是 `PicoApple2` 的 commit
`67610a5 fix(keyboard): keymap 依實機量測校正 —— 四組錯位、三顆無主的鍵`。

注意這兩個孤兒碼的修法跟前四項不同：**表裡的值不動**（`[0][6]` 仍是 `206`、
`[2][7]` 仍是 `208`），改的是 `translate()` 的轉換鏈。所以光比對 `keymap_base`
會誤判成「還沒修」—— 要看轉換鏈。

### 兩個孤兒解開了 —— 已修正

表中有兩個保留碼從來沒有對應的轉換分支，按下去會把原始值當 ASCII 送出。
主韌體已補上分支（表值不動，改轉換鏈），以下是當時測到的狀態：

| 表中值 | 位置 | 實際是 | 目前送出 |
|---|---|---|---|
| `206` | `[0][6]` | **TAB** | `0xCE` |
| `208` | `[2][7]` | **CAPS LOCK** | `0xD0` |

### 4 組錯位 —— 皆已修正

以下四項描述的是**當時測到的狀態**（「按 `/` 什麼都不送」等），四項都已回饋進
`PicoApple2.ino`，在現在的主韌體上不再成立。保留原文是為了留下當初的推論過程。

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

## Shift 排列：現代，不是 Apple II+

**這是刻意的，不是錯位。** `keymap_shift` 的數字列照現代 PC 鍵盤排，鍵帽印什麼就出什麼 ——
配合這台是現代鍵帽的實體鍵盤。真正的 Apple II+ 排法不同，六處有差：

| 鍵 | Apple II+ | 這台機器 |
|---|---|---|
| shift-1 | `!` | `!` |
| shift-2 | `"` | **`@`** |
| shift-3 | `#` | `#` |
| shift-4 | `$` | `$` |
| shift-5 | `%` | `%` |
| shift-6 | `&` | **`^`** |
| shift-7 | `'` | **`&`** |
| shift-8 | `(` | **`*`** |
| shift-9 | `)` | **`(`** |
| shift-0 | — | **`)`** |
| shift-`-` | `=` | **`_`** |

Apple II+ 用的是 **bit-paired**（位元配對）鍵盤，源自 ASR-33 電傳打字機。規則很機械：
shift 就是把字元的 ASCII **XOR 0x10**。

```
'2' 0x32 ^ 0x10 = 0x22 = "
'7' 0x37 ^ 0x10 = 0x27 = '
'9' 0x39 ^ 0x10 = 0x29 = )
'-' 0x2D ^ 0x10 = 0x3D = =
```

同一條規則也解釋了 Apple II 上那幾個有名的怪組合：`shift-P` = `@`（`0x50^0x10=0x40`）、
`shift-N` = `^`、`shift-M` = `]`。現代鍵盤是 typewriter-paired，符號位置照打字機習慣排，
跟 ASCII 碼值無關。

實務差別：在真 II+ 上打字串的 `"` 要按 `shift-2`，在這台要按 `shift-'`。

檢查器的 FREE 模式會把 `SENDS SHIFT` 一起顯示出來，所以這個差異是**看得見的**。
記在這裡是為了下次看到 `shift-2` 出 `@` 時，不必再懷疑一次轉換表 —— 它是對的。

（注意別跟錯位混為一談：檢查器內建的表是修正前的快照，`=` 在那份表裡送不出東西；
主韌體已修，現在 `=` / `+` 是正常的。詳見上面〈實測結果〉的回饋狀態一覽。）

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

### 選配硬體修改：QH 與 SER 共用 GP15（`KBD_SHARED_DATA`）

為了空出 GP27 當 ADC：165 的 QH 割離 GP27，改串 1 kΩ 接到 GP15，與 595 的 SER 共用。
寫 595 時 GP15 為輸出（隔著 1 kΩ 蓋過 QH），讀 165 前切成輸入、等 1 µs 再讀；GP15 關掉內建下拉
（約 50k，會和 1 kΩ 分壓），GP27 設成無上下拉的輸入。

- 編譯開關 `KBD_SHARED_DATA`，預設 0 = 原接線。改過硬體的板子要編 1：
  `arduino-cli compile ... --build-property "compiler.cpp.extra_flags=-DKBD_SHARED_DATA=1"`，
  或 cmd 下 `set "KBD_SHARED_DATA=1" & build_offset.bat`。
- 燒錄後序列埠開機會印 `kbd QH on GP15` / `kbd QH on GP27`，畫面資訊列右上角顯示 `QH GP15` / `QH GP27`。
- 時序：CLOCK 共用，讀 165 的 8 個時脈也會把 QH 推進 595 移位暫存器。`scanMatrix()` 每列都是
  「寫滿 16 bit → LATCH↑ → 165 載入（第二次 LATCH↑，此時移位暫存器仍是剛寫的資料）→ 讀 8 bit」，
  下一次 LATCH↑ 前一定重新寫滿，所以垃圾永遠不會被鎖存。**改掃描順序時要守住這條**：
  讀完 165 之後不可以沒重寫就拉 LATCH。

## 建置

### 1. 字元 ROM

畫面文字用真正的 Apple II 字元 ROM 渲染。**ROM 受著作權保護，不在本倉庫內**，請自備 `apple2_char.rom`（2048 bytes）：

```bash
python tools/rom2h.py /path/to/apple2_char.rom
```

產生的 `KeyboardTester/apple2_char_rom.h` 已列入 `.gitignore` —— 它只是 ROM 的另一種編碼，同樣不能公開。

跟 PicoApple2 主專案用的是同一顆：主專案已就緒的話，直接 `python tools/rom2h.py ../PicoApple2/apple2_core/src/apple2_char.rom` 即可。

沒有 ROM 也能編譯（`__has_include` 會退回全零佔位），只是畫面沒有文字、只剩鍵盤框框。編譯時出現 `apple2_char_rom.h not found` 警告就是這個情況 —— 換電腦 clone 後最容易漏掉。

### 2. 編譯

一般版，連結在 `0x10000000`，可獨立開機：

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipico KeyboardTester
```

### 3. 偏移版 / standalone 版（搭配 retro-loader）

若板子的前 16KB 放了 [rp2040-retro-loader](https://github.com/pondahai/rp2040-retro-loader) 這類 SD 卡載入器，
要改連結到 `0x10004000` 才不會蓋掉它。

**一行搞定**（Windows）：

```bash
build_offset.bat
```

腳本會依序做完下面步驟 3、4 的全部事情：產生 linker script → 偏移編譯 → 檢查 flash 佈局 →
合併 trampoline → 順便把選單圖示複製成配對的檔名。載入器倉庫預設找 `..
p2040-retro-loader`，
放別處就 `build_offset.bat <loader repo path>`。`arduino-cli` 會自動找 Arduino IDE 2.x 內建的那支，
要指定別的就先設 `ARDUINO_CLI_PATH`。

以下是它實際做的事，手動跑或想改流程時看：

```bash
python loader_offset/gen_app_ld.py    # 依你的 arduino-pico 版本重新產生 linker script
arduino-cli compile --fqbn rp2040:rp2040:rpipico \
  --build-property "recipe.hooks.linking.prelink.1.pattern=\"{runtime.tools.pqt-python3.path}/python3\" -I \"{runtime.platform.path}/tools/simplesub.py\" --input \"$PWD/loader_offset/memmap_app_arduino.ld\" --out \"{build.path}/memmap_default.ld\" --sub __FLASH_LENGTH__ {build.flash_length} --sub __EEPROM_START__ {build.eeprom_start} --sub __FS_START__ {build.fs_start} --sub __FS_END__ {build.fs_end} --sub __RAM_LENGTH__ {build.ram_length} --sub __PSRAM_LENGTH__ {build.psram_length}" \
  --output-dir build_offset KeyboardTester
python loader_offset/check_flash_layout.py build_offset/KeyboardTester.ino.uf2
```

產出的 `build_offset/KeyboardTester.ino.uf2` 只含 `0x10004000` 之後的區塊，前 16KB 是空的。
它**不能單獨用 USB 燒錄** —— `gen_app_ld.py` 會主動丟掉 `.boot2`，而 RP2040 的 boot ROM
只認 flash 最前面 256 bytes，那裡沒東西就開不了機。這份檔案只在「載入器已經在板子上」時才成立。

### 4. 合併 trampoline —— 兩種用法都吃得下的那一份

把載入器的 trampoline（一小段落在 `0x10000000` 的 boot2 + 跳轉樁）接在偏移本體前面：

```bash
python ../rp2040-retro-loader/tools/merge_uf2.py ../rp2040-retro-loader/build/trampoline.uf2 build_offset/KeyboardTester.ino.uf2 -o build_offset/KeyboardTester_standalone.uf2
```

合併版兩條路都走得通：

* **USB BOOTSEL 拖進去** → 跳板落在 `0x10000000`，開機後跳 `0x10004000`
* **放進 SD 卡給載入器** → 載入器丟掉 `0x10004000` 以下的 block，其餘照寫，載入器本身不會被蓋掉

所以 SD 卡上放合併版就好，不必為兩種用法各準備一個檔案。

三種產物的定位：

| 檔案 | USB 直燒 | 放 SD 給 loader |
|---|---|---|
| 一般版（步驟 2，`0x10000000`） | ✅ | ❌ 會蓋掉載入器 |
| 偏移版 `KeyboardTester.ino.uf2` | ❌ 前 16KB 空的，不開機 | ✅ |
| **standalone `KeyboardTester_standalone.uf2`** | ✅ | ✅ |

`check_flash_layout.py` 是驗**裸偏移本體**用的，對合併版會回報「image 起點是 `0x10000000`，應該是 `0x10004000`」——
那是正常的，別拿它驗 standalone。

RP2040 跑 XIP，每個位址都在編譯時就烙進機器碼裡，所以搬檔案沒用，映像必須重新連結。

## 授權

GPL-3.0（見 `LICENSE`）。`TFT_DMA.cpp/.h` 取自 PicoApple2。

## 選單圖示（retro-loader）

`assets/KeyboardTester.ino.RAW` 是給 [rp2040-retro-loader](https://github.com/pondahai/rp2040-retro-loader)
選單用的封面圖：96x96 RGB565 big-endian、無標頭、固定 18432 bytes。載入器只用檔案長度驗證，
所以長度必須剛好。

用法：把它跟 `KeyboardTester.ino.uf2` 一起放 SD 卡**根目錄**（不支援子目錄）。檔名規則是
UF2 去掉副檔名再接 `.RAW`，也就是 `KeyboardTester.ino.uf2` 配 `KeyboardTester.ino.RAW`。
若放的是 standalone 版，圖示要跟著改名成 `KeyboardTester_standalone.RAW`。

要從原圖重新產生：

```bash
python ../rp2040-retro-loader/tools/make_thumb.py assets/kbtester_icon.jpg --fit -o assets/KeyboardTester.ino.RAW
```
