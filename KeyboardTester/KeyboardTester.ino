// ============================================================================
// PicoApple2 Keyboard Tester —— 獨立的實體鍵盤檢查器
//
// 目的：找出「8x8 掃描矩陣座標 [row][col]」與「實體鍵盤上那顆鍵」之間的
//       真實對應關係，用來驗證 PicoApple2.ino 裡的 keymap_base / keymap_shift
//       是否貼合硬體佈線。
//
// 重要：keymap_base / keymap_shift 兩張表與 scan_matrix() 的掃描時序，
//       皆自 PicoApple2.ino 原封搬移，一個位元都沒有改動。
//       它們是「待驗證的參考基準」，不是待修的東西。
//
// 兩種模式（用序列埠切換）：
//   [F] FREE   自由按壓：按任何鍵，畫面即顯示它掃到的 [r][c]、表中原始值、
//              以及經過 Shift/Ctrl/CapsLock 轉換鏈後真正送出的 byte。
//   [G] GUIDED 導引學習：畫面依序高亮 64 顆實體鍵，你照著按，程式記錄
//              「這顆實體鍵 → 掃到哪個 [r][c]」，走完一輪即得到完整真值表。
//              按 [D] 可把結果以 C 陣列格式印到序列埠。
// ============================================================================

#include <Arduino.h>
#include "TFT_DMA.h"

// ---- 顯示器接腳（同 PicoApple2.ino）----
#define PIN_DISPLAY_SCK  18
#define PIN_DISPLAY_MOSI 19
#define PIN_DISPLAY_CS   17
#define PIN_DISPLAY_DC   20
#define PIN_DISPLAY_RST  21
#define PIN_DISPLAY_BL   22

// ---- 鍵盤矩陣接腳（同 PicoApple2.ino）----
#define DATA_OUT_PIN     15
#define LATCH_PIN        14
#define CLOCK_PIN        26
#define DATA_IN_PIN      27

TFT_DMA tft(PIN_DISPLAY_CS, PIN_DISPLAY_DC, PIN_DISPLAY_RST, PIN_DISPLAY_MOSI, PIN_DISPLAY_SCK);

// ---- Apple II 字元 ROM ----
// 由 tools/rom2h.py 從 apple2_char.rom 產生（受著作權，不入版控）。
// 尚未產生時以全零佔位，程式仍可編譯，只是文字不顯示。
#if __has_include("apple2_char_rom.h")
  #include "apple2_char_rom.h"
#else
  static const uint8_t APPLE2_CHAR_ROM[2048] = {0};
  #warning "apple2_char_rom.h not found - text will be blank. Run tools/rom2h.py"
#endif

// ---- 補字：Apple II 字元 ROM 沒有的字形 ------------------------------------
// 那顆 ROM 只有 128 個相異字形（2048 bytes 裡上下半完全重複），內容是經典的
// 大寫 64 字集：@ A-Z [ \ ] ^ _ 與 ASCII 0x20-0x3F。沒有小寫，也沒有 ` ~ { } |。
// drawChar() 把 0x60~0x7E 減 32 折進去，小寫折成大寫是刻意的，但 ` ~ { } |
// 會分別撞到 @ ^ [ ] \ 的格子上，畫出來是別的字。
//
// 解法：開一份 RAM 字型，把這五個字形塞進 ROM 用不到的 slot 1~5。
// 為什麼是 1~5：drawChar() 的折疊只會產生 0~31 與 64~127 兩段，而 0~31 這段
// 在 ROM 裡只是 64~95 的重複、且正常字串不含控制碼，所以空著沒人用。
// 畫的時候把 ` 轉成 GLYPH_BQUOTE、~ 轉成 GLYPH_TILDE…，就繞過折疊直接命中自繪字形。
#define GLYPH_BQUOTE 1
#define GLYPH_TILDE  2
#define GLYPH_LBRACE 3
#define GLYPH_RBRACE 4
#define GLYPH_PIPE   5

// 7 px 寬（bit6~bit0）、8 列。ROM 原生字形高位元是反白旗標，這裡一律留 0。
static const uint8_t EXTRA_GLYPHS[5][8] = {
  /* ` */ { 0x00, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 },
  /* ~ */ { 0x00, 0x00, 0x00, 0x32, 0x4C, 0x00, 0x00, 0x00 },
  /* { */ { 0x00, 0x0C, 0x10, 0x10, 0x20, 0x10, 0x10, 0x0C },
  /* } */ { 0x00, 0x18, 0x04, 0x04, 0x02, 0x04, 0x04, 0x18 },
  /* | */ { 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08 },
};

// drawChar() 折疊後最大索引是 127，所以只需要前 1024 bytes。
static uint8_t g_font[128 * 8];

void initFont() {
  memcpy(g_font, APPLE2_CHAR_ROM, sizeof(g_font));
  memcpy(&g_font[GLYPH_BQUOTE * 8], EXTRA_GLYPHS, sizeof(EXTRA_GLYPHS));
}

// 把 ROM 畫不出來的字換成自繪字形的控制碼；其餘原樣通過。
char fontSubst(char c) {
  switch (c) {
    case '`': return (char)GLYPH_BQUOTE;
    case '~': return (char)GLYPH_TILDE;
    case '{': return (char)GLYPH_LBRACE;
    case '}': return (char)GLYPH_RBRACE;
    case '|': return (char)GLYPH_PIPE;
    default:  return c;
  }
}

// ============================================================================
// 以下兩張表自 PicoApple2.ino 原封複製 —— 禁止修改
// ============================================================================
const uint8_t keymap_base[8][8] = {
  { '1', '3', '5', '7', '9', '-', 206, 204 }, { 'q', 'e', 't', 'u', 'o', '[', 207, '\\' },
  { 'a', 'd', 'g', 'j', 'l', '\'', 205, 208 }, { 'z', 'c', 'b', 'm', '.', 202, 210, 0 },
  { '2', '4', '6', '8', '0', 214, '`', 0 }, { 'w', 'r', 'y', 'i', 'p', ']', 209, '/' },
  { 'x', 'f', 'h', 'k', ';', 203, 212, '=' }, { 's', 'v', 'n', ',', 213, ' ', 211, 0 }
};

const uint8_t keymap_shift[8][8] = {
  { '!', '#', '%', '&', '(', '_', 206, 204 }, { 'Q', 'E', 'T', 'U', 'O', '{', 207, '|' },
  { 'A', 'D', 'G', 'J', 'L', '"', 205, 208 }, { 'Z', 'C', 'B', 'M', '>', 202, 210, 0 },
  { '@', '$', '^', '*', ')', 214, '~', 0 }, { 'W', 'R', 'Y', 'I', 'P', '}', 209, '?' },
  { 'X', 'F', 'H', 'K', ':', 203, 212, '+' }, { 'S', 'V', 'N', '<', 213, ' ', 211, 0 }
};

// ============================================================================
// 掃描（時序自 PicoApple2.ino scan_matrix() 原封搬移）
// ============================================================================
uint8_t keyState[8][8] = {0};
uint8_t lastKeyState[8][8] = {0};
unsigned long lastKeyTime[8][8] = {0};

inline void fastWrite(uint pin, bool val) { gpio_put(pin, val); }
inline bool fastRead(uint pin) { return gpio_get(pin); }

byte myShiftIn(uint8_t dP, uint8_t cP) {
  byte data = 0;
  for (int i = 0; i < 8; i++) { if (fastRead(dP)) { data |= (1 << i); } fastWrite(cP, 1); delayMicroseconds(1); fastWrite(cP, 0); }
  return data;
}

void scanMatrix() {
  for (int row = 0; row < 8; row++) {
    byte rS = (1 << row); fastWrite(LATCH_PIN, 0); shiftOut(DATA_OUT_PIN, CLOCK_PIN, MSBFIRST, 0); shiftOut(DATA_OUT_PIN, CLOCK_PIN, MSBFIRST, rS);
    fastWrite(LATCH_PIN, 1); delayMicroseconds(5); fastWrite(LATCH_PIN, 0); delayMicroseconds(1); fastWrite(LATCH_PIN, 1);
    byte colData = myShiftIn(DATA_IN_PIN, CLOCK_PIN);
    for (int col = 0; col < 8; col++) keyState[row][7 - col] = (colData & (1 << col));
  }
  fastWrite(CLOCK_PIN, 1); delayMicroseconds(2); fastWrite(CLOCK_PIN, 0);
}

// ============================================================================
// 實體鍵盤佈局（依照片：5 列，13 格寬，共 64 鍵 = 恰好 8x8）
// ============================================================================
// 每顆實體鍵帶兩份資訊，刻意分開存放：
//
//   expect —— 「表怎麼說」：照 keymap_base 的內容推論，這顆鍵應該落在哪一格。
//             EXP_UNKNOWN 表示表裡沒有可辨識的值（懸空的 206/208/213/214 與三個 0）。
//   rc     —— 「硬體實際如何」：實測確認的矩陣座標。RC_UNKNOWN 表示尚未確認，
//             此時退回用 expect 做值查表。
//
// 兩者不一致 = 轉換表與實體佈線錯位，程式會把它標出來。
#define EXP_UNKNOWN 0xFF
#define RC_UNKNOWN  0xFF
#define RC(r, c) ((uint8_t)((r) * 8 + (c)))

struct PhysKey { const char* label; const char* name; uint8_t span; uint8_t expect; uint8_t rc; };

const PhysKey PHYS_R0[] = {
  {"ESC","ESC",1,207,RC_UNKNOWN},{"1","1 !",1,'1',RC_UNKNOWN},{"2","2 @",1,'2',RC_UNKNOWN},{"3","3 #",1,'3',RC_UNKNOWN},
  {"4","4 $",1,'4',RC_UNKNOWN},{"5","5 %",1,'5',RC_UNKNOWN},{"6","6 ^",1,'6',RC_UNKNOWN},{"7","7 &",1,'7',RC_UNKNOWN},
  {"8","8 *",1,'8',RC_UNKNOWN},{"9","9 (",1,'9',RC_UNKNOWN},{"0","0 )",1,'0',RC_UNKNOWN},{"-","- _",1,'-',RC_UNKNOWN},
  {"BS","BACKSPACE",1,204,RC_UNKNOWN}};

const PhysKey PHYS_R1[] = {
  {"TAB","TAB",1,EXP_UNKNOWN,RC(0,6)},{"Q","Q",1,'q',RC_UNKNOWN},{"W","W",1,'w',RC_UNKNOWN},{"E","E",1,'e',RC_UNKNOWN},
  {"R","R",1,'r',RC_UNKNOWN},{"T","T",1,'t',RC_UNKNOWN},{"Y","Y",1,'y',RC_UNKNOWN},{"U","U",1,'u',RC_UNKNOWN},
  {"I","I",1,'i',RC_UNKNOWN},{"O","O",1,'o',RC_UNKNOWN},{"P","P",1,'p',RC_UNKNOWN},{"[","[ {",1,'[',RC_UNKNOWN},
  {"]","] }",1,']',RC_UNKNOWN}};

const PhysKey PHYS_R2[] = {
  {"CAP","CAPS LOCK",1,EXP_UNKNOWN,RC(2,7)},{"A","A",1,'a',RC_UNKNOWN},{"S","S",1,'s',RC(6,0)},{"D","D",1,'d',RC_UNKNOWN},
  {"F","F",1,'f',RC_UNKNOWN},{"G","G",1,'g',RC_UNKNOWN},{"H","H",1,'h',RC_UNKNOWN},{"J","J",1,'j',RC_UNKNOWN},
  {"K","K",1,'k',RC_UNKNOWN},{"L","L",1,'l',RC_UNKNOWN},{";","; :",1,';',RC_UNKNOWN},{"'","' \"",1,'\'',RC_UNKNOWN},
  {"\\","\\ |",1,'\\',RC_UNKNOWN}};

const PhysKey PHYS_R3[] = {
  {"SHF","SHIFT",1,202,RC_UNKNOWN},{"Z","Z",1,'z',RC_UNKNOWN},{"X","X",1,'x',RC(7,0)},{"C","C",1,'c',RC_UNKNOWN},
  {"V","V",1,'v',RC_UNKNOWN},{"B","B",1,'b',RC_UNKNOWN},{"N","N",1,'n',RC_UNKNOWN},{"M","M",1,'m',RC_UNKNOWN},
  {",",", <",1,',',RC_UNKNOWN},{".",". >",1,'.',RC_UNKNOWN},{"/","/ ?",1,'/',RC(7,4)},{"UP","UP ARROW",1,209,RC_UNKNOWN},
  {"ENT","ENTER",1,203,RC_UNKNOWN}};

const PhysKey PHYS_R4[] = {
  {"CTL","CTRL",1,205,RC_UNKNOWN},{"FN","FN",1,EXP_UNKNOWN,RC(3,7)},{"ALT","ALT",1,EXP_UNKNOWN,RC(7,7)},
  {"PGU","PGUP",1,EXP_UNKNOWN,RC(5,7)},{"`","` ~",1,'`',RC_UNKNOWN},{"SPACE","SPACE",2,' ',RC_UNKNOWN},
  {"=","= +",1,'=',RC(4,5)},{"PGD","PGDN",1,EXP_UNKNOWN,RC(6,7)},{"DEL","DELETE",1,EXP_UNKNOWN,RC(4,7)},
  {"LFT","LEFT ARROW",1,211,RC_UNKNOWN},{"DN","DOWN ARROW",1,210,RC_UNKNOWN},{"RGT","RIGHT ARROW",1,212,RC_UNKNOWN}};

const PhysKey* PHYS_ROW[5] = { PHYS_R0, PHYS_R1, PHYS_R2, PHYS_R3, PHYS_R4 };
const uint8_t  PHYS_CNT[5] = { 13, 13, 13, 13, 12 };
#define PHYS_TOTAL 64

// 幾何：320x240 橫向
#define KX0 4
#define KY0 6
#define KW  22
#define KH  22
#define KGAP 2

// 學習到的對照：learned[flatIndex] = row*8+col，未學到為 -1
int8_t learned[PHYS_TOTAL];

int physFlat(int r, int i) { int f = 0; for (int x = 0; x < r; x++) f += PHYS_CNT[x]; return f + i; }

void physFromFlat(int flat, int* r, int* i) {
  int f = flat;
  for (int x = 0; x < 5; x++) { if (f < PHYS_CNT[x]) { *r = x; *i = f; return; } f -= PHYS_CNT[x]; }
  *r = 0; *i = 0;
}

int physSlotX(int r, int i) {
  int slot = 0;
  for (int x = 0; x < i; x++) slot += PHYS_ROW[r][x].span;
  return KX0 + slot * (KW + KGAP);
}

String describe(uint8_t v); // 定義在下方轉換鏈那一段

// 在 keymap_base 裡找某個值落在哪一格；找不到回 -1。
int lookupByValue(uint8_t v) {
  if (v == 0) return -1;
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      if (keymap_base[r][c] == v) return r * 8 + c;
  return -1;
}

uint8_t phys_rc[PHYS_TOTAL];  // 解析後：每顆實體鍵的矩陣座標，0xFF = 仍未知
int8_t  rc_to_phys[64];       // 反查：矩陣座標 → 實體鍵，-1 = 無主

// 解析順序：實測座標優先，其次才照表用值反查。
void resolvePhysMap() {
  for (int i = 0; i < 64; i++) rc_to_phys[i] = -1;
  for (int r = 0; r < 5; r++) {
    for (int i = 0; i < PHYS_CNT[r]; i++) {
      const PhysKey& k = PHYS_ROW[r][i];
      int flat = physFlat(r, i);
      int rc = -1;
      if (k.rc != RC_UNKNOWN) rc = k.rc;
      else if (k.expect != EXP_UNKNOWN) rc = lookupByValue(k.expect);
      phys_rc[flat] = (rc >= 0) ? (uint8_t)rc : 0xFF;
      if (rc >= 0 && rc_to_phys[rc] < 0) rc_to_phys[rc] = (int8_t)flat;
    }
  }
}

int physFromTable(int rc) { return (rc >= 0 && rc < 64) ? rc_to_phys[rc] : -1; }

// 對照分析：表宣稱的位置 vs 實測位置，不一致就是錯位。
void dumpAnalysis() {
  Serial.println();
  Serial.println("=== TABLE vs HARDWARE ===");
  int bad = 0, unknown = 0;
  for (int r = 0; r < 5; r++) {
    for (int i = 0; i < PHYS_CNT[r]; i++) {
      const PhysKey& k = PHYS_ROW[r][i];
      if (k.rc == RC_UNKNOWN) { if (k.expect == EXP_UNKNOWN) unknown++; continue; }
      int table_rc = (k.expect == EXP_UNKNOWN) ? -1 : lookupByValue(k.expect);
      int hw_r = k.rc >> 3, hw_c = k.rc & 7;
      if (table_rc < 0) {
        Serial.printf("  %-12s hw=[%d][%d] table has %s here (was unassigned)\n",
                      k.name, hw_r, hw_c, describe(keymap_base[hw_r][hw_c]).c_str());
      } else if (table_rc != k.rc) {
        bad++;
        Serial.printf("  %-12s hw=[%d][%d] but table puts it at [%d][%d]  <-- MISMATCH\n",
                      k.name, hw_r, hw_c, table_rc >> 3, table_rc & 7);
        Serial.printf("  %-12s   [%d][%d] currently holds %s\n", "",
                      hw_r, hw_c, describe(keymap_base[hw_r][hw_c]).c_str());
      }
    }
  }
  Serial.printf("  %d mismatch(es), %d key(s) still unidentified\n", bad, unknown);

  // 完整性檢查：64 顆實體鍵應該一對一佔滿 64 個矩陣格
  int claimed = 0, dup = 0;
  uint8_t owners[64] = {0};
  for (int i = 0; i < PHYS_TOTAL; i++) if (phys_rc[i] != 0xFF) { owners[phys_rc[i]]++; claimed++; }
  Serial.printf("  coverage: %d/64 keys placed\n", claimed);
  for (int rc = 0; rc < 64; rc++) {
    if (owners[rc] == 0) Serial.printf("  [%d][%d] has NO physical key (table holds %s)\n", rc >> 3, rc & 7, describe(keymap_base[rc >> 3][rc & 7]).c_str());
    if (owners[rc] > 1) { dup++; Serial.printf("  [%d][%d] claimed by %d keys  <-- CONFLICT\n", rc >> 3, rc & 7, owners[rc]); }
  }
  if (claimed == 64 && dup == 0) Serial.println("  map is complete and one-to-one.");
  Serial.println();
}

// ============================================================================
// 繪圖
// ============================================================================
void drawString(uint16_t x, uint16_t y, String s, uint16_t color, uint16_t bg) {
  s.toUpperCase();
  for (unsigned int i = 0; i < s.length(); i++)
    tft.drawChar(x + (i * 7), y, fontSubst(s[i]), color, bg, g_font);
}

#define C_BG      0x0000
#define C_KEY     0x2104  // 深灰：未按
#define C_KEY_TXT 0xFFFF
#define C_HIT     0x07E0  // 綠：目前按住（依已學到的真值表）
#define C_GUESS   0xFD20  // 橘：目前按住（照表推論，尚未實測驗證）
#define C_TARGET  0xF800  // 紅：導引模式的目標鍵
#define C_DONE    0x0010  // 深藍：已學到
#define C_LABEL   0xFFE0

// 外框：這顆實體鍵在 Apple II+ 上存不存在。跟填色（學習/按壓狀態）是兩個獨立
// 維度，所以外框刻意用中性灰階，不跟紅/綠/橘/藍那幾個狀態色搶顏色。
#define C_EDGE_A2    0xAD55  // 淺灰：Apple II+ 原生鍵
#define C_EDGE_EXTRA 0x3186  // 暗灰：這台機器加的鍵

// PicoApple2 模擬的是 Apple II+（apple2_sys.rom 為 $D000-$FFFF 共 12KB，
// 字元 ROM 也是無小寫的大寫 64 字集）。II+ 的鍵盤是：
//
//   ESC 1 2 3 4 5 6 7 8 9 0 : -  RESET
//       Q W E R T Y U I O P      REPT  RETURN
//   CTRL A S D F G H J K L ;  <- ->
//   SHIFT Z X C V B N M , . /  SHIFT
//                 SPACE
//
// 下面這些是這台 64 鍵機器多出來、II+ 沒有的鍵。反過來 II+ 有而這台沒有的
// （RESET / REPT / 獨立的 : 鍵）不在檢查器的範圍內，所以兩邊不是一一對應。
const char* const EXTRA_KEYS[] = {
  "BS", "TAB", "[", "]", "CAP", "'", "\\", "UP",
  "FN", "ALT", "PGU", "`", "=", "PGD", "DEL", "DN"
};

// 收 label 而不是 const PhysKey&：.ino 的前處理會把函式原型自動提到檔案最
// 上方，那個位置 PhysKey 還沒宣告，用它當參數型別會編不過。
bool isAppleIIPlusKey(const char* label) {
  for (unsigned i = 0; i < sizeof(EXTRA_KEYS) / sizeof(EXTRA_KEYS[0]); i++)
    if (strcmp(label, EXTRA_KEYS[i]) == 0) return false;
  return true;
}

void drawKey(int r, int i, uint16_t bg, uint16_t fg) {
  const PhysKey& k = PHYS_ROW[r][i];
  int x = physSlotX(r, i);
  int w = k.span * (KW + KGAP) - KGAP;
  int y = KY0 + r * (KH + KGAP);
  tft.drawRect(x, y, w, KH, bg);
  int len = strlen(k.label);
  int tx = x + (w - len * 7) / 2;
  if (tx < x + 1) tx = x + 1;
  drawString(tx, y + 7, k.label, fg, bg);

  // 外框最後才畫。三個字的鍵帽（ESC / CAP / PGU…）文字寬 21px、鍵寬 22px，
  // 置中後會壓到最右那一行像素，先畫外框的話會被文字啃掉一角。
  uint16_t edge = isAppleIIPlusKey(k.label) ? C_EDGE_A2 : C_EDGE_EXTRA;
  tft.drawRect(x, y, w, 1, edge);
  tft.drawRect(x, y + KH - 1, w, 1, edge);
  tft.drawRect(x, y, 1, KH, edge);
  tft.drawRect(x + w - 1, y, 1, KH, edge);
}

void drawKeyboard() {
  for (int r = 0; r < 5; r++)
    for (int i = 0; i < PHYS_CNT[r]; i++)
      drawKey(r, i, learned[physFlat(r, i)] >= 0 ? C_DONE : C_KEY, C_KEY_TXT);
}

// ============================================================================
// 轉換鏈（自 PicoApple2.ino 原封搬移；joy_mode 固定為 false、無選單）
// 回傳實際會餵給 Apple II 閂鎖的 byte，0 表示不送鍵。
// ============================================================================
bool g_caps_lock = true;

uint8_t translate(int r, int c, bool shift, bool ctrl) {
  uint8_t k = shift ? keymap_shift[r][c] : keymap_base[r][c];
  if (k >= 209 && k <= 214) {
    if (k <= 212) { if (k == 209) k = 0x0B; else if (k == 210) k = 0x0A; else if (k == 211) k = 0x08; else if (k == 212) k = 0x15; }
    else k = 0;
  } else if (k == 203) k = 0x0D;
  else if (k == 207) k = 0x1B;
  else if (k == 204) k = 0x08;
  else if (ctrl) { if (k >= 'a' && k <= 'z') k = (k - 32) & 0x1F; else if (k >= '@' && k <= '_') k = k & 0x1F; else k = 0; }
  else { if (g_caps_lock && !shift && k >= 'a' && k <= 'z') k -= 32; else if (g_caps_lock && shift && k >= 'A' && k <= 'Z') k += 32; }
  return k;
}

const char* rawName(uint8_t v) {
  switch (v) {
    case 0:   return "(none)";
    case 202: return "202 SHIFT";
    case 203: return "203 ENTER";
    case 204: return "204 BKSP";
    case 205: return "205 CTRL";
    case 206: return "206 ???";
    case 207: return "207 ESC";
    case 208: return "208 ???";
    case 209: return "209 UP";
    case 210: return "210 DOWN";
    case 211: return "211 LEFT";
    case 212: return "212 RIGHT";
    case 213: return "213 BTN0";
    case 214: return "214 BTN1";
    default:  return nullptr;
  }
}

String describe(uint8_t v) {
  const char* n = rawName(v);
  if (n) return String(n);
  char buf[16];
  // ` ~ { } | 這幾個字 ROM 沒有字形，由 EXTRA_GLYPHS 補上，所以照原樣印就行。
  if (v >= 32 && v < 127) snprintf(buf, sizeof(buf), "'%c' 0x%02X", (char)v, v);
  else snprintf(buf, sizeof(buf), "0x%02X", v);
  return String(buf);
}

// ============================================================================
// Fn 組合鍵
// ============================================================================
// Fn+1~9 = F1~F9，Fn+0 = F10（F11 / F12 暫不定義）。Fn+C 切 CapsLock。
// 檢查器不「執行」這些動作，只把它解讀出來顯示 —— 底層掃描結果照舊呈現。
// 括號內是主韌體 PicoApple2.ino 目前真的會做的事。
bool g_fn_held = false;

String fnComboName(int rc) {
  int p = physFromTable(rc);
  if (p < 0) return String();
  int pr, pi; physFromFlat(p, &pr, &pi);
  const char* lbl = PHYS_ROW[pr][pi].label;
  if (strlen(lbl) != 1) return String();
  char ch = lbl[0];

  if (ch == 'C') return "FN+C = CAPS LOCK TOGGLE";
  if (ch >= '1' && ch <= '9') {
    int n = ch - '0';
    const char* act[] = { "WARM RESET", "COLD RESET", "DISK MENU", "JOYSTICK MODE", "SPEED CYCLE" };
    if (n <= 5) return "FN+" + String(ch) + " = F" + String(n) + " (" + act[n - 1] + ")";
    return "FN+" + String(ch) + " = F" + String(n) + " (UNUSED)";
  }
  if (ch == '0') return "FN+0 = F10 (UNUSED)";
  return String();
}

// ============================================================================
// 資訊面板
// ============================================================================
#define INFO_Y 130

int g_mode = 0;     // 0 = FREE, 1 = GUIDED
int g_target = 0;   // 導引模式目前目標（flat index）
int g_last_rc = -1; // 最後一次掃到的 [r][c]，-1 = 無

void drawInfo() {
  tft.drawRect(0, INFO_Y, 320, 240 - INFO_Y, C_BG);
  drawString(4, INFO_Y, g_mode == 0 ? "MODE: FREE" : "MODE: GUIDED", C_LABEL, C_BG);
  drawString(116, INFO_Y, "F/G/S/D/M/R", 0x8410, C_BG);

  int y = INFO_Y + 14;
  if (g_mode == 1) {
    int r, i; physFromFlat(g_target, &r, &i);
    drawString(4, y, "PRESS: " + String(PHYS_ROW[r][i].name), C_TARGET, C_BG);
    int done = 0; for (int x = 0; x < PHYS_TOTAL; x++) if (learned[x] >= 0) done++;
    drawString(4, y + 12, "LEARNED " + String(done) + "/" + String(PHYS_TOTAL) + "  (S=SKIP)", 0x07FF, C_BG);
    y += 28;
  }

  if (g_last_rc >= 0) {
    int r = g_last_rc >> 3, c = g_last_rc & 7;
    char buf[32];
    snprintf(buf, sizeof(buf), "MATRIX [%d][%d]", r, c);
    drawString(4, y, buf, C_HIT, C_BG);

    // 這顆 [r][c] 若已學到，指出它對應哪顆實體鍵
    int mapped = -1;
    for (int x = 0; x < PHYS_TOTAL; x++) if (learned[x] == g_last_rc) { mapped = x; break; }
    if (mapped >= 0) {
      int pr, pi; physFromFlat(mapped, &pr, &pi);
      drawString(116, y, "= " + String(PHYS_ROW[pr][pi].name), 0xFFE0, C_BG);
    } else {
      int guess = physFromTable(g_last_rc);
      if (guess >= 0) { int pr, pi; physFromFlat(guess, &pr, &pi); drawString(116, y, "? " + String(PHYS_ROW[pr][pi].name), C_GUESS, C_BG); }
      else drawString(116, y, "? UNMAPPED IN TABLE", C_GUESS, C_BG);
    }

    drawString(4, y + 14, "TABLE BASE " + describe(keymap_base[r][c]), 0xFFFF, C_BG);
    drawString(4, y + 26, "      SHFT " + describe(keymap_shift[r][c]), 0xFFFF, C_BG);
    drawString(4, y + 40, "SENDS PLAIN " + describe(translate(r, c, false, false)), 0x07E0, C_BG);
    drawString(4, y + 52, "      SHIFT " + describe(translate(r, c, true, false)), 0x07E0, C_BG);
    drawString(4, y + 64, "      CTRL  " + describe(translate(r, c, false, true)), 0x07E0, C_BG);

    // Fn 按住時，把組合鍵的意義解讀出來（僅顯示，不執行）
    if (g_fn_held) {
      String fc = fnComboName(g_last_rc);
      if (fc.length()) drawString(4, y + 78, fc, 0xFC00, C_BG);
    }
  }
}

// ============================================================================
// 序列埠：把學到的對照表印出來
// ============================================================================
void dumpMap() {
  Serial.println();
  Serial.println("// ---- Learned physical -> matrix map ----");
  Serial.println("// value = row*8+col, -1 = not learned");
  Serial.println("const int8_t PHYS_TO_MATRIX[64] = {");
  for (int r = 0; r < 5; r++) {
    Serial.print("  ");
    for (int i = 0; i < PHYS_CNT[r]; i++) { Serial.print(learned[physFlat(r, i)]); Serial.print(", "); }
    Serial.print("// row "); Serial.println(r);
  }
  Serial.println("};");

  Serial.println();
  Serial.println("// ---- Matrix -> physical, against the current table ----");
  for (int rc = 0; rc < 64; rc++) {
    int r = rc >> 3, c = rc & 7;
    int mapped = -1;
    for (int x = 0; x < PHYS_TOTAL; x++) if (learned[x] == rc) { mapped = x; break; }
    Serial.printf("[%d][%d] table=%-12s phys=", r, c, describe(keymap_base[r][c]).c_str());
    if (mapped >= 0) { int pr, pi; physFromFlat(mapped, &pr, &pi); Serial.println(PHYS_ROW[pr][pi].name); }
    else Serial.println("<unlearned>");
  }
  Serial.println();
}

// ============================================================================
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) tight_loop_contents();

  gpio_init(DATA_OUT_PIN); gpio_set_dir(DATA_OUT_PIN, GPIO_OUT);
  gpio_init(LATCH_PIN);    gpio_set_dir(LATCH_PIN, GPIO_OUT);
  gpio_init(CLOCK_PIN);    gpio_set_dir(CLOCK_PIN, GPIO_OUT);
  gpio_init(DATA_IN_PIN);  gpio_set_dir(DATA_IN_PIN, GPIO_IN);

  for (int i = 0; i < PHYS_TOTAL; i++) learned[i] = -1;
  resolvePhysMap();

  // ---- ILI9341 面板初始化 ----
  // TFT_DMA::begin() 只設定 GPIO / SPI / DMA，不含面板本身的初始化序列，
  // 那一段在主專案是寫在 setup1() 裡的。整段（含背光腳與硬體 reset 脈衝）
  // 自 PicoApple2.ino setup1() 原封搬移 —— 少了它畫面就是全黑。
  pinMode(PIN_DISPLAY_BL, OUTPUT); digitalWrite(PIN_DISPLAY_BL, LOW);

  initFont();
  tft.begin();
  gpio_put(PIN_DISPLAY_RST, 0); delay(100); gpio_put(PIN_DISPLAY_RST, 1); delay(100);
  tft.writeCommand(0x01); delay(150);
  tft.writeCommand(0xCB); tft.writeData(0x39); tft.writeData(0x2C); tft.writeData(0x00); tft.writeData(0x34); tft.writeData(0x02);
  tft.writeCommand(0xCF); tft.writeData(0x00); tft.writeData(0xC1); tft.writeData(0x30);
  tft.writeCommand(0xE8); tft.writeData(0x85); tft.writeData(0x00); tft.writeData(0x78);
  tft.writeCommand(0xEA); tft.writeData(0x00); tft.writeData(0x00);
  tft.writeCommand(0xED); tft.writeData(0x64); tft.writeData(0x03); tft.writeData(0x12); tft.writeData(0x81);
  tft.writeCommand(0xF7); tft.writeData(0x20);
  tft.writeCommand(0xC0); tft.writeData(0x23);
  tft.writeCommand(0xC1); tft.writeData(0x10);
  tft.writeCommand(0xC5); tft.writeData(0x3E); tft.writeData(0x28);
  tft.writeCommand(0xC7); tft.writeData(0x86);
  tft.setRotation(1);
  tft.writeCommand(0x3A); tft.writeData(0x55);
  tft.writeCommand(0xB1); tft.writeData(0x00); tft.writeData(0x18);
  tft.writeCommand(0xB6); tft.writeData(0x08); tft.writeData(0x82); tft.writeData(0x27);
  tft.writeCommand(0x11); delay(150);  // sleep out
  tft.writeCommand(0x29); delay(150);  // display on

  spi_set_baudrate(spi0, 62500000);
  tft.fillScreen(C_BG);
  digitalWrite(PIN_DISPLAY_BL, HIGH);  // 初始化完才點背光，避免開機瞬間的雪花
  drawKeyboard();
  drawInfo();

  Serial.println("PicoApple2 Keyboard Tester");
  Serial.println("F=free  G=guided  S=skip  D=dump  M=analysis  R=reset");
  dumpAnalysis();
}

void loop() {
  // ---- 序列埠命令 ----
  while (Serial.available()) {
    int ch = toupper(Serial.read());
    if (ch == 'F') { g_mode = 0; tft.fillScreen(C_BG); drawKeyboard(); drawInfo(); }
    else if (ch == 'G') { g_mode = 1; g_target = 0; tft.fillScreen(C_BG); drawKeyboard(); drawInfo(); }
    else if (ch == 'S' && g_mode == 1) { g_target = (g_target + 1) % PHYS_TOTAL; drawInfo(); }
    else if (ch == 'D') dumpMap();
    else if (ch == 'M') dumpAnalysis();
    else if (ch == 'R') { for (int i = 0; i < PHYS_TOTAL; i++) learned[i] = -1; g_target = 0; tft.fillScreen(C_BG); drawKeyboard(); drawInfo(); }
  }

  scanMatrix();
  unsigned long now_t = millis();

  // Fn 是否按住（位置取自解析後的真值表，不寫死座標）
  { uint8_t frc = phys_rc[physFlat(4, 1)];
    g_fn_held = (frc != 0xFF) && keyState[frc >> 3][frc & 7]; }

  // ---- 邊緣偵測 + 30ms 去彈跳（與 PicoApple2.ino 同規格）----
  bool changed = false;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      bool p = keyState[r][c];
      if (p != lastKeyState[r][c] && (now_t - lastKeyTime[r][c] > 30)) {
        lastKeyTime[r][c] = now_t; lastKeyState[r][c] = p;
        if (p) {
          g_last_rc = r * 8 + c;
          changed = true;
          Serial.printf("DOWN [%d][%d]  base=%s  shift=%s\n", r, c,
                        describe(keymap_base[r][c]).c_str(), describe(keymap_shift[r][c]).c_str());
          if (g_fn_held) { String fc = fnComboName(g_last_rc); if (fc.length()) { Serial.print("  "); Serial.println(fc); } }
          if (g_mode == 1) {
            learned[g_target] = g_last_rc;
            int pr, pi; physFromFlat(g_target, &pr, &pi);
            Serial.printf("  LEARNED: %s -> [%d][%d]\n", PHYS_ROW[pr][pi].name, r, c);
            g_target = (g_target + 1) % PHYS_TOTAL;
          }
        }
      }
    }
  }

  // ---- 高亮：紅=導引目標，綠=正按住，藍=已學到 ----
  static int last_target = -1;
  static int last_lit = -1;
  int lit = -1;
  uint16_t lit_col = C_HIT;
  if (g_last_rc >= 0 && keyState[g_last_rc >> 3][g_last_rc & 7]) {
    for (int x = 0; x < PHYS_TOTAL; x++) if (learned[x] == g_last_rc) { lit = x; break; }
    // 還沒實測過的話，退而用表推論（橘色 = 這是表的說法，不是量到的事實）
    if (lit < 0) { lit = physFromTable(g_last_rc); lit_col = C_GUESS; }
  }

  if (g_mode == 1 && g_target != last_target) {
    if (last_target >= 0) { int r, i; physFromFlat(last_target, &r, &i); drawKey(r, i, learned[last_target] >= 0 ? C_DONE : C_KEY, C_KEY_TXT); }
    int r, i; physFromFlat(g_target, &r, &i);
    drawKey(r, i, C_TARGET, C_KEY_TXT);
    last_target = g_target;
  }
  if (lit != last_lit) {
    if (last_lit >= 0) { int r, i; physFromFlat(last_lit, &r, &i); drawKey(r, i, learned[last_lit] >= 0 ? C_DONE : C_KEY, C_KEY_TXT); }
    if (lit >= 0) { int r, i; physFromFlat(lit, &r, &i); drawKey(r, i, lit_col, 0x0000); }
    last_lit = lit;
  }

  if (changed) drawInfo();
  delay(16); // ~60Hz，與主專案的 VBLANK 掃描率一致
}
