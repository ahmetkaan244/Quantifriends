#include <Wire.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_random.h>
#include <esp_system.h>
#include <time.h>

// =====================================================================
//  1) DONANIM AYARLARI
// =====================================================================
#define PIN_DOKUNMATIK  4    // TTP223 dijital cikis (aktif-HIGH)
#define PIN_BUZZER      3    // Pasif buzzer
#define PIN_I2C_SDA     8
#define PIN_I2C_SCL     9

#define EKRAN_GENISLIK  128
#define EKRAN_YUKSEKLIK 64
#define OLED_ADRES      0x3C

Adafruit_SSD1306 ekran(EKRAN_GENISLIK, EKRAN_YUKSEKLIK, &Wire, -1);
bool oledHazir = false;   // OLED yoksa ekran cagrilarini atla

// =====================================================================
//  2) Wi-Fi AP + WEB SUNUCU
// =====================================================================
const char* AP_ADI     = "Quantifriends";
const char* AP_SIFRE   = "12345678";
WebServer sunucu(80);

// =====================================================================
//  2b) TİPLER VE YARDIMCILAR (fonksiyonlardan once tanimli olmali)
// =====================================================================
enum SistemModu {
  MOD_PET = 0,
  MOD_OYUNLAR,
  MOD_MOTIVASYON,
  MOD_SOZLER,      // motivasyon sozu ekrani
  MOD_MENU,        // cihaz uzerinden ana menu
  MOD_DIL          // dil cikarma animasyonu
};

enum OyunTuru {
  OYUN_YOK = 0,
  OYUN_DINO,
  OYUN_FLAPPY,
  OYUN_YILAN,
  OYUN_PONG,
  OYUN_BREAKOUT,
  OYUN_STACKER,
  OYUN_SAYISI
};

const char* OYUN_ADLARI[OYUN_SAYISI] = {
  "Yok", "Dino Kosu", "Flappy Kus", "Yilan", "Pong", "Breakout", "Stacker"
};

enum TepkiTuru {
  TEP_MUTLU = 0, TEP_SASIRMA, TEP_GULUMSEME, TEP_ZIPLAMA, TEP_GOZ_KIRPMA,
  TEP_KIZGIN, TEP_UTANGAC, TEP_SIKILMIS, TEP_HEYECAN, TEP_KORKU,
  TEP_YORGUN, TEP_HAVALI, TEP_MUZIK, TEP_SUPHECI, TEP_ACIKMIS,
  TEP_KUTLAMA, TEP_BAS_DONMESI, TEP_SARJ, TEP_OPUCUK, TEP_KARISIK,
  TEP_GLITCH, TEP_ASKIN, TEP_UYUMA, TEP_ESNEME, TEP_GULME,
  TEP_DUSUNME, TEP_HAPSIRMA, TEP_ORKESTRA, TEP_KAFA_SALLAMA, TEP_EL_SALLAMA,
  TEP_GOZ_KIRP_SERI, TEP_TITREME, TEP_POFLA, TEP_DANST, TEP_KALP_ATISI,
  TEP_YILDIZ, TEP_BULUT, TEP_GUNES, TEP_ATES, TEP_BUZ,
  TEP_ROKET, TEP_YILDIRIM, TEP_BALON, TEP_YAPRAK, TEP_KAR
};

enum OtonomAktivite {
  AKT_YOK = 0,
  AKT_KITAP_OKUMA,
  AKT_AGIRLIK,
  AKT_MUZIK,
  AKT_ESNEME,
  AKT_UYUMA,
  AKT_GEZINME,
  AKT_YEMEK,
  AKT_DANS,
  AKT_SAYISI
};

enum DilAsamasi {
  DIL_CIK = 0,     // dil yavaşça dışarı çıkar
  DIL_ICERI,       // dil ağza geri girer
  DIL_GEZDIR,      // dil dışarıda sağ-sola gezer
  DIL_BEKLE        // ağız kapalı kısa bekleme
};

// =====================================================================
//  3) ZAMAN SABITLERI (hepsi ms)
// =====================================================================
const unsigned long ZAMAN_KARE_MS              = 16;    // ~60 FPS
const unsigned long ZAMAN_DOKUNMA_DEBOUNCE     = 30;
const unsigned long ZAMAN_IDLE_BASLANGIC       = 10000; // 10 sn dokunulmazsa otonom
const unsigned long ZAMAN_UZUN_BASMA           = 600;   // uzun basma esigi
const unsigned long ZAMAN_DOKUNUS_ARASI        = 400;   // ardisik dokunuslar arasi max sure
const unsigned long ZAMAN_MENU_OTOMATIK_CIKIS  = 60000; // menude 1 dk hareketsizlik
const uint8_t       MENU_ICIN_DOKUNUS          = 3;     // ana menu icin dokunus sayisi

// --- Aktif mod ve oyun ---
SistemModu sistemModu    = MOD_PET;
OyunTuru   aktifOyun     = OYUN_YOK;
int        oyunMenuSecim = 0;      // oyun listesindeki secili oge

// =====================================================================
//  4b) SAAT + METİN (fonksiyonlardan once tanimli olmali)
// =====================================================================
long     saatOfset = 0;            // unix zamani - (millis()/1000) farki
bool     saatAyarli = false;       // telefon bir kez saat gonderdi mi
unsigned long saatSonSenkron = 0;  // son senkron zamani (millis)
int      saatDilim = 3;            // Turkiye UTC+3

// Metni ekrana yazar (boyut: 1 = 5x7, 2 = 10x14)
void metinYaz(int x, int y, const char* metin, uint8_t boyut = 1) {
  ekran.setTextSize(boyut);
  ekran.setTextColor(SSD1306_WHITE);
  ekran.setCursor(x, y);
  ekran.print(metin);
}

unsigned long saatSimdiUnix() {
  long t = (long)(millis() / 1000) + saatOfset;
  if (t < 0) t = 0;
  return (unsigned long)t;
}

void saatAyarla(long unixSaniye, int dilimSaat) {
  if (dilimSaat < -12) dilimSaat = -12;
  if (dilimSaat > 14)  dilimSaat = 14;
  saatDilim = dilimSaat;
  saatOfset = unixSaniye - (long)(millis() / 1000);
  saatAyarli = true;
  saatSonSenkron = millis();
  Serial.print(F("[DB] Saat ayarlandi: "));
  Serial.println(unixSaniye);
}

// Saati HH:MM (veya HH:MM:SS) olarak yazar
void saatMetni(char* tampon, size_t boy, bool saniyeli) {
  if (!saatAyarli) { snprintf(tampon, boy, "--:--"); return; }
  unsigned long t = saatSimdiUnix() + (long)saatDilim * 3600UL;
  int saat   = (t / 3600) % 24;
  int dakika = (t / 60) % 60;
  int saniye = t % 60;
  if (saniyeli) snprintf(tampon, boy, "%02d:%02d:%02d", saat, dakika, saniye);
  else          snprintf(tampon, boy, "%02d:%02d", saat, dakika);
}

// Ekranin ust sagina saati basar
void saatKoseCiz() {
  char t[12];
  saatMetni(t, sizeof(t), false);
  metinYaz(128 - (int)strlen(t) * 6, 0, t);
}

// =====================================================================
//  5) BUTON (TTP223) DURUMU - tek buton, cok islev
// =====================================================================
bool          butonOncekiDurum   = false;
unsigned long butonBasilmaZamani = 0;
unsigned long butonBirakilmaZamani = 0;
bool          butonBasiliyor     = false;
bool          uzunBasmaIslendi   = false;
uint8_t       dokunusSayaci      = 0;      // ardisik kisa dokunus sayaci
unsigned long sonDokunusZamani   = 0;      // son kisa dokunusun zamani

// --- Ana menu durumu ---
int           menuSecim = 0;               // 0:Pet 1:Oyunlar 2:Sozler 3:Dil 4:Cikis
unsigned long menuGirisZamani = 0;
const int     MENU_OGE_SAYISI = 5;
const char*   MENU_OGELERI[MENU_OGE_SAYISI] = {
  "Pet Modu", "Oyunlar", "Sozler", "Dil Cikar", "Cikis"
};

// Uzun basma tekrar tetikleme (cuddle modunda mirildanma)
unsigned long sonMirildanma = 0;

// =====================================================================
//  6) IDLE / OTONOM PET DURUMU
// =====================================================================
unsigned long sonDokunmaZamani = 0;
bool          otonomAktif      = false;

// Dil animasyonu asamalari

OtonomAktivite otonomAktivite = AKT_YOK;
unsigned long  otonomBaslangic = 0;
unsigned long  otonomSure      = 0;
int            otonomAdim      = 0;

// =====================================================================
//  7) TEPKİ SİSTEMİ (45+ rastgele kisa tepki)
// =====================================================================

const int TEPKI_SAYISI = 45;

// Her tepki icin: animasyon suresi ve eslik eden ses turu
struct TepkiTanimi {
  TepkiTuru tur;
  uint16_t  sure;      // ms
  uint8_t   sesTuru;   // Ses motoru turu
};

// NOT: Buradaki dizi "dummy" veridir; ileride bitmap tabanli
// tepkiler eklemek icin genisletilebilir.
const TepkiTanimi TEPKILER[TEPKI_SAYISI] PROGMEM = {
  {TEP_MUTLU,         2160,  1}, {TEP_SASIRMA,       1620,  2},
  {TEP_GULUMSEME,     1980,  1}, {TEP_ZIPLAMA,       1800,  3},
  {TEP_GOZ_KIRPMA,    1440,  4}, {TEP_KIZGIN,        1620,  5},
  {TEP_UTANGAC,       2520,  1}, {TEP_SIKILMIS,      2700,  6},
  {TEP_HEYECAN,       2340,  3}, {TEP_KORKU,         1980,  7},
  {TEP_YORGUN,        2880,  6}, {TEP_HAVALI,        2160,  4},
  {TEP_MUZIK,         2700,  8}, {TEP_SUPHECI,       2340,  2},
  {TEP_ACIKMIS,       2160,  1}, {TEP_KUTLAMA,       3240,  9},
  {TEP_BAS_DONMESI,   2520, 10}, {TEP_SARJ,          1980, 11},
  {TEP_OPUCUK,        1800,  4}, {TEP_KARISIK,       2160,  2},
  {TEP_GLITCH,        1620, 12}, {TEP_ASKIN,         2520, 13},
  {TEP_UYUMA,         3600,  6}, {TEP_ESNEME,        2340,  6},
  {TEP_GULME,         1800,  1}, {TEP_DUSUNME,       2700,  2},
  {TEP_HAPSIRMA,      1620,  7}, {TEP_ORKESTRA,      2880, 14},
  {TEP_KAFA_SALLAMA,  2160,  8}, {TEP_EL_SALLAMA,    2340,  1},
  {TEP_GOZ_KIRP_SERI, 1980,  4}, {TEP_TITREME,       1620,  7},
  {TEP_POFLA,         1800,  6}, {TEP_DANST,         2700,  8},
  {TEP_KALP_ATISI,    2340, 13}, {TEP_YILDIZ,        2520,  3},
  {TEP_BULUT,         2880,  6}, {TEP_GUNES,         2520,  9},
  {TEP_ATES,          2160,  5}, {TEP_BUZ,           2160, 10},
  {TEP_ROKET,         1980,  3}, {TEP_YILDIRIM,      1620, 11},
  {TEP_BALON,         2340,  1}, {TEP_YAPRAK,        2700,  6},
  {TEP_KAR,           3060, 10}
};

bool          tepkiAktif      = false;
TepkiTuru     aktifTepki      = TEP_MUTLU;
unsigned long tepkiBaslangic  = 0;
unsigned long tepkiSure       = 0;

// =====================================================================
//  8) SEVGİ (CUDDLE) MODU
// =====================================================================
bool          sevgiModu      = false;
unsigned long sevgiBaslangic = 0;
int           sevgiKalpSayisi = 0;
struct Kalp { int x, y, hiz, boyut; };
Kalp kalpler[12];

// =====================================================================
//  9) SES MOTORU (non-blocking, tone() tabanli)
// =====================================================================
struct Nota { uint16_t frekans; uint16_t sure; };

const int MAX_SES_ADIM = 48;
Nota          sesDizisi[MAX_SES_ADIM];
int           sesAdimSayisi = 0;
int           sesAdim       = 0;
unsigned long sesSonrakiZaman = 0;
bool          sesCaliyor    = false;

// --- Ses efektleri (R2-D2 civiltilari + retro arcade) ---
void sesTemizle() {
  sesAdimSayisi = 0;
  sesAdim = 0;
  noTone(PIN_BUZZER);
  sesCaliyor = false;
}

void sesEkle(uint16_t f, uint16_t s) {
  if (sesAdimSayisi < MAX_SES_ADIM) {
    sesDizisi[sesAdimSayisi].frekans = f;
    sesDizisi[sesAdimSayisi].sure    = s;
    sesAdimSayisi++;
  }
}

void sesCalistir() {
  sesAdim = 0;
  if (sesAdimSayisi == 0) return;
  sesCaliyor = true;
  tone(PIN_BUZZER, sesDizisi[0].frekans);
  sesSonrakiZaman = millis() + sesDizisi[0].sure;
}

// R2-D2 tarzi robotik civilti (rastgele frekans sicramalari)
void sesRobotikCivilti() {
  sesTemizle();
  static const uint16_t notalar[] = {1200, 1800, 900, 2400, 1500, 2100, 1100, 2600, 1600};
  for (int i = 0; i < 8; i++) sesEkle(notalar[random(9)], 28 + random(40));
  sesCalistir();
}

// Tatli "chirp" (kisa dokunus sesi)
void sesChirp() {
  sesTemizle();
  sesEkle(1400, 35);
  sesEkle(1900, 45);
  sesCalistir();
}

// Sevimli mirildanma (cuddle)
void sesMirildan() {
  sesTemizle();
  static const uint16_t notalar[] = {520, 640, 580, 700, 610, 760};
  for (int i = 0; i < 6; i++) sesEkle(notalar[i], 70);
  sesCalistir();
}

// Mutlu selamlama
void sesSelamlama() {
  sesTemizle();
  sesEkle(523, 90); sesEkle(659, 90); sesEkle(784, 90); sesEkle(1046, 140);
  sesCalistir();
}

// Retro: ziplama
void sesZiplama() {
  sesTemizle();
  sesEkle(400, 40); sesEkle(600, 40); sesEkle(900, 50);
  sesCalistir();
}

// Retro: puan / toplama
void sesPuan() {
  sesTemizle();
  sesEkle(988, 50); sesEkle(1319, 80);
  sesCalistir();
}

// Retro: oyun bitti
void sesOyunBitti() {
  sesTemizle();
  sesEkle(784, 130); sesEkle(659, 130); sesEkle(523, 130); sesEkle(392, 260);
  sesCalistir();
}

// Retro: carpma / hasar
void sesCarpma() {
  sesTemizle();
  sesEkle(180, 60); sesEkle(120, 90);
  sesCalistir();
}

// Retro: menu secimi
void sesMenuSecim() {
  sesTemizle();
  sesEkle(880, 35); sesEkle(1175, 55);
  sesCalistir();
}

// Tepki sesleri icin tur secici
void sesTepkiTuru(uint8_t tur) {
  switch (tur) {
    case 1:  sesChirp(); break;
    case 2:  sesRobotikCivilti(); break;
    case 3:  sesZiplama(); break;
    case 4:  { sesTemizle(); sesEkle(1500, 30); sesEkle(1200, 30); sesCalistir(); } break;
    case 5:  { sesTemizle(); sesEkle(300, 70); sesEkle(220, 90); sesCalistir(); } break;
    case 6:  { sesTemizle(); sesEkle(500, 90); sesEkle(420, 110); sesCalistir(); } break;
    case 7:  sesCarpma(); break;
    case 8:  { sesTemizle(); sesEkle(660, 60); sesEkle(880, 60); sesEkle(660, 60); sesCalistir(); } break;
    case 9:  sesSelamlama(); break;
    case 10: { sesTemizle(); sesEkle(1200, 40); sesEkle(900, 40); sesEkle(1200, 40); sesCalistir(); } break;
    case 11: { sesTemizle(); sesEkle(700, 30); sesEkle(1400, 30); sesEkle(2100, 40); sesCalistir(); } break;
    case 12: sesRobotikCivilti(); break;
    case 13: sesMirildan(); break;
    case 14: { sesTemizle(); sesEkle(523, 60); sesEkle(587, 60); sesEkle(659, 60); sesEkle(784, 90); sesCalistir(); } break;
    default: sesChirp(); break;
  }
}

// Ses motorunu her karede cagir (non-blocking)
void sesGuncelle() {
  if (!sesCaliyor) return;
  if (millis() < sesSonrakiZaman) return;

  sesAdim++;
  if (sesAdim >= sesAdimSayisi) {
    noTone(PIN_BUZZER);
    sesCaliyor = false;
    return;
  }
  if (sesDizisi[sesAdim].frekans == 0) noTone(PIN_BUZZER);
  else tone(PIN_BUZZER, sesDizisi[sesAdim].frekans);
  sesSonrakiZaman = millis() + sesDizisi[sesAdim].sure;
}

// =====================================================================
//  10) ÇİZİM YARDIMCILARI (OLED 128x64)
// =====================================================================
const int GOZ_SOL_X = 42, GOZ_SAG_X = 86, GOZ_Y = 26, GOZ_R = 10;
const float DERECE = 3.14159265f / 180.0f;

void yay(int cx, int cy, int r, int a0, int a1) {
  for (int a = a0; a <= a1; a += 3) {
    ekran.drawPixel(cx + (int)(cosf(a * DERECE) * r),
                    cy + (int)(sinf(a * DERECE) * r), SSD1306_WHITE);
  }
}

void kalinYay(int cx, int cy, int r, int a0, int a1) {
  yay(cx, cy, r, a0, a1);
  yay(cx, cy, r - 1, a0, a1);
}

// Göz kırpığı (3 ince çizgi) — üst kenara oturur
void kirpikCiz(int cx, int cy, int r) {
  // üst yay üzerinden dışa taşan 3 kısa çizgi
  ekran.drawLine(cx - 6, cy - r + 1, cx - 8, cy - r - 3, SSD1306_WHITE);
  ekran.drawLine(cx,     cy - r,     cx,     cy - r - 4, SSD1306_WHITE);
  ekran.drawLine(cx + 6, cy - r + 1, cx + 8, cy - r - 3, SSD1306_WHITE);
}

// Yarı kapak göz (kırpıklı) — utangaç / uykulu ifadeler için
void gozKirpikli(int cx, int cy, bool bebek = true) {
  ekran.fillCircle(cx, cy, GOZ_R, SSD1306_WHITE);
  if (bebek) ekran.fillCircle(cx - 3, cy - 3, 3, SSD1306_BLACK);
  kirpikCiz(cx, cy, GOZ_R);
}

void gozAcik(int cx, int cy, bool bebek = true) {
  ekran.fillCircle(cx, cy, GOZ_R, SSD1306_WHITE);
  if (bebek) ekran.fillCircle(cx - 3, cy - 3, 3, SSD1306_BLACK);
  kirpikCiz(cx, cy, GOZ_R);
}

void gozKapali(int cx, int cy) {
  kalinYay(cx, cy, 8, 200, 340);
  // kapalı gözde alt kırpıklar
  ekran.drawLine(cx - 6, cy + 6, cx - 8, cy + 9, SSD1306_WHITE);
  ekran.drawLine(cx,     cy + 8, cx,     cy + 11, SSD1306_WHITE);
  ekran.drawLine(cx + 6, cy + 6, cx + 8, cy + 9, SSD1306_WHITE);
}

void kalpCiz(int cx, int cy, int s) {
  ekran.fillCircle(cx - s / 2, cy - s / 4, s / 2, SSD1306_WHITE);
  ekran.fillCircle(cx + s / 2, cy - s / 4, s / 2, SSD1306_WHITE);
  for (int i = 0; i <= s; i++) {
    int w = s - i;
    ekran.drawFastHLine(cx - w, cy + (i * 3) / 4, w * 2 + 1, SSD1306_WHITE);
  }
}

void yildizCiz(int cx, int cy, int r) {
  int pxs[5], pys[5];
  for (int i = 0; i < 5; i++) {
    float a = (-90.0f + i * 72.0f) * DERECE;
    pxs[i] = cx + (int)(cosf(a) * r);
    pys[i] = cy + (int)(sinf(a) * r);
  }
  for (int i = 0; i < 5; i++) {
    int j = (i + 1) % 5;
    ekran.fillTriangle(cx, cy, pxs[i], pys[i], pxs[j], pys[j], SSD1306_WHITE);
  }
}

void simsekCiz(int cx, int cy, int h) {
  ekran.drawLine(cx + 3, cy - h, cx - 3, cy, SSD1306_WHITE);
  ekran.drawLine(cx - 3, cy, cx + 1, cy, SSD1306_WHITE);
  ekran.drawLine(cx + 1, cy, cx - 3, cy + h, SSD1306_WHITE);
  ekran.drawLine(cx + 4, cy - h, cx - 2, cy, SSD1306_WHITE);
  ekran.drawLine(cx, cy, cx + 2, cy, SSD1306_WHITE);
  ekran.drawLine(cx + 2, cy, cx - 2, cy + h, SSD1306_WHITE);
}

void gozyasiCiz(int x, int y, int uzunluk) {
  ekran.fillCircle(x, y + uzunluk, 2, SSD1306_WHITE);
  ekran.drawLine(x, y, x, y + uzunluk, SSD1306_WHITE);
  ekran.drawLine(x - 1, y + 2, x, y, SSD1306_WHITE);
  ekran.drawLine(x + 1, y + 2, x, y, SSD1306_WHITE);
}

void spiralCiz(int cx, int cy, int maxR, float a0) {
  int pxx = cx, pyy = cy;
  for (int t = 1; t < 90; t++) {
    float a = a0 + t * 0.30f;
    float r = (float)maxR * t / 90.0f;
    int x = cx + (int)(cosf(a) * r);
    int y = cy + (int)(sinf(a) * r);
    ekran.drawLine(pxx, pyy, x, y, SSD1306_WHITE);
    pxx = x; pyy = y;
  }
}

// =====================================================================
//  11) MOTİVASYON SÖZLERİ
// =====================================================================
const int SOZ_SAYISI = 10;

// Not: Bu dizi 100+ soze cikarilabilir; her soz maks ~120 karakter olmali.
const char* MOTIVASYON_SOZLERI[SOZ_SAYISI] = {
  "Pes etme, sadece mola ver.",
  "Buyuk isler kucuk adimlarla baslar.",
  "Bugun yaptigin en kucuk sey bile yarinin temeli.",
  "Zor olan baslamak; gerisi aliskanlik.",
  "Hata yapmak, denediginin kanitidir.",
  "Kendine inan; gerisi zaman meselesi.",
  "Her gun yuzde bir iyiles, bir yilda dev ol.",
  "Odaklan; gelecek su anki adimlarinda.",
  "Sabir, en guclu sessiz gucdur.",
  "Dunsen bile bir adim daha at."
};

int aktifSoz = 0;
int sozSatirBaslangic = 0;          // kaydirma icin
unsigned long sozKaydirmaZamani = 0;
const unsigned long SOZ_KAYDIRMA_MS = 2600;  // her 2.6 sn'de bir sayfa

// Bir sozu 16 karakterlik satirlara boler
int sozuSatirlara(const char* soz, char satirlar[4][22]) {
  int satir = 0, kolon = 0;
  int uzunluk = strlen(soz);
  int sonBosluk = -1;

  for (int i = 0; i <= uzunluk; i++) {
    char c = (i < uzunluk) ? soz[i] : '\0';
    if (c == ' ') sonBosluk = i;

    if (kolon >= 21 || c == '\0') {
      if (c == '\0' || sonBosluk <= 0) {
        satirlar[satir][kolon] = '\0';
        satir++; kolon = 0;
        if (satir >= 4) break;
      } else {
        // son bosluktan bol
        int bolme = sonBosluk;
        int k = 0;
        for (int j = (i - kolon); j < bolme && k < 21; j++) satirlar[satir][k++] = soz[j];
        satirlar[satir][k] = '\0';
        satir++; kolon = 0;
        if (satir >= 4) break;
        for (int j = bolme + 1; j < i && kolon < 21; j++) satirlar[satir][kolon++] = soz[j];
      }
    } else {
      satirlar[satir][kolon++] = c;
    }
  }
  satirlar[satir][kolon] = '\0';
  return satir + 1;
}

void sozEkraniCiz() {
  metinYaz(0, 0, "GUNUN SOZU");
  ekran.drawFastHLine(0, 9, 128, SSD1306_WHITE);

  char satirlar[4][22];
  int n = sozuSatirlara(MOTIVASYON_SOZLERI[aktifSoz], satirlar);
  for (int i = 0; i < n && i < 4; i++) {
    metinYaz(2, 14 + i * 10, satirlar[i]);
  }

  // alt bilgi
  // alt bilgi: soz sayaci + saat
  char saatT[12];
  saatMetni(saatT, sizeof(saatT), false);
  char alt[32];
  snprintf(alt, sizeof(alt), "%d/%d  %s", aktifSoz + 1, SOZ_SAYISI, saatT);
  metinYaz(0, 56, alt);
}

// =====================================================================
//  12) OTONOM PET ÇİZİMLERİ (aktivite animasyonlari)
// =====================================================================
void cizIdleYuz(int f);
void cizMuzik(int f);
void cizYemek(int f);
void cizUyuma(int f);
void cizEsneme(int f);

void cizKitapOkuma(int f) {
  // kitap
  ekran.drawRect(28, 40, 32, 20, SSD1306_WHITE);
  ekran.drawRect(68, 40, 32, 20, SSD1306_WHITE);
  ekran.drawLine(64, 40, 64, 60, SSD1306_WHITE);
  // gozler saga sola
  int dx = ((f / 20) % 2) ? 2 : -2;
  ekran.fillCircle(GOZ_SOL_X + dx, GOZ_Y, 6, SSD1306_WHITE);
  ekran.fillCircle(GOZ_SAG_X + dx, GOZ_Y, 6, SSD1306_WHITE);
  ekran.fillCircle(GOZ_SOL_X + dx - 2, GOZ_Y - 2, 2, SSD1306_BLACK);
  ekran.fillCircle(GOZ_SAG_X + dx - 2, GOZ_Y - 2, 2, SSD1306_BLACK);
  // satirlar
  if ((f / 10) % 2) { ekran.drawFastHLine(31, 45, 25, SSD1306_WHITE); ekran.drawFastHLine(71, 45, 25, SSD1306_WHITE); }
}

void cizAgirlik(int f) {
  int up = ((f / 15) % 2) ? 6 : 0;
  // dambil
  ekran.drawFastHLine(24, 50 - up, 80, SSD1306_WHITE);
  ekran.fillRect(18, 42 - up, 8, 16, SSD1306_WHITE);
  ekran.fillRect(102, 42 - up, 8, 16, SSD1306_WHITE);
  // gozler
  gozAcik(GOZ_SOL_X, GOZ_Y - 2); gozAcik(GOZ_SAG_X, GOZ_Y - 2);
  ekran.drawFastHLine(58, 34, 12, SSD1306_WHITE);   // kararlı agiz
}

void cizMuzik(int f) {
  int bob = ((f / 12) % 2) ? 1 : -1;
  yay(64, 24, 32, 190, 350);
  ekran.fillRect(24, 22, 7, 14, SSD1306_WHITE);
  ekran.fillRect(97, 22, 7, 14, SSD1306_WHITE);
  ekran.fillCircle(GOZ_SOL_X, GOZ_Y + bob, 7, SSD1306_WHITE);
  ekran.fillCircle(GOZ_SAG_X, GOZ_Y + bob, 7, SSD1306_WHITE);
  kalinYay(64, 42 + bob, 9, 20, 160);
  // nota
  int n = f % 40;
  int ny = 6 + (n / 5);
  ekran.fillCircle(112, ny, 2, SSD1306_WHITE);
  ekran.drawLine(114, ny, 114, ny - 6, SSD1306_WHITE);
}

void cizEsneme(int f) {
  int mo = 3 + ((f / 8) % 3);
  gozKapali(GOZ_SOL_X, GOZ_Y);
  gozKapali(GOZ_SAG_X, GOZ_Y);
  ekran.fillCircle(64, 44, mo + 3, SSD1306_WHITE);
  ekran.fillCircle(64, 44, mo, SSD1306_BLACK);
  // esneme cizgileri
  ekran.drawLine(16, 20, 22, 16, SSD1306_WHITE);
  ekran.drawLine(110, 20, 104, 16, SSD1306_WHITE);
}

void cizUyuma(int f) {
  gozKapali(GOZ_SOL_X, GOZ_Y);
  gozKapali(GOZ_SAG_X, GOZ_Y);
  ekran.fillCircle(64, 46, 4, SSD1306_WHITE);
  ekran.fillCircle(64, 45, 2, SSD1306_BLACK);
  // Zzz
  for (int i = 0; i < 3; i++) {
    int p = (f + i * 30) % 90;
    int sz = 2 + p / 30;
    int zx = 90 + p / 3;
    int zy = 28 - p / 4;
    if (zy > 0 && zx < 124) {
      ekran.drawLine(zx, zy, zx + sz, zy, SSD1306_WHITE);
      ekran.drawLine(zx + sz, zy, zx, zy + sz, SSD1306_WHITE);
      ekran.drawLine(zx, zy + sz, zx + sz, zy + sz, SSD1306_WHITE);
    }
  }
}

void cizGezinme(int f) {
  int dx = (int)(sinf(f * 0.05f) * 8);
  gozAcik(GOZ_SOL_X + dx, GOZ_Y); gozAcik(GOZ_SAG_X + dx, GOZ_Y);
  kalinYay(64 + dx, 40, 12, 20, 160);
  // ayak izleri
  for (int i = 0; i < 4; i++) {
    int x = 20 + i * 24;
    ekran.drawPixel(x, 58, SSD1306_WHITE);
    ekran.drawPixel(x + 1, 59, SSD1306_WHITE);
  }
}

void cizYemek(int f) {
  gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
  ekran.fillCircle(64, 44, 10, SSD1306_WHITE);
  ekran.fillCircle(64, 42, 7, SSD1306_BLACK);
  int tx = 64 + ((f / 6) % 3 - 1) * 3;
  ekran.fillRect(tx - 4, 46, 8, 8, SSD1306_WHITE);
  ekran.fillCircle(tx, 53, 4, SSD1306_WHITE);
}

void cizDans(int f) {
  int hop = ((f / 8) % 2) ? 2 : 0;
  gozAcik(GOZ_SOL_X, GOZ_Y - hop); gozAcik(GOZ_SAG_X, GOZ_Y - hop);
  kalinYay(64, 40 - hop, 12, 20, 160);
  ekran.drawPixel(16, 12 + (f % 20), SSD1306_WHITE);
  ekran.drawPixel(112, 30 - (f % 20), SSD1306_WHITE);
}

void cizOtonomAktivite() {
  int f = otonomAdim;
  switch (otonomAktivite) {
    case AKT_KITAP_OKUMA: cizKitapOkuma(f); break;
    case AKT_AGIRLIK:     cizAgirlik(f); break;
    case AKT_MUZIK:       cizMuzik(f); break;
    case AKT_ESNEME:      cizEsneme(f); break;
    case AKT_UYUMA:       cizUyuma(f); break;
    case AKT_GEZINME:     cizGezinme(f); break;
    case AKT_YEMEK:       cizYemek(f); break;
    case AKT_DANS:        cizDans(f); break;
    default:              cizIdleYuz(f); break;
  }
}

// =====================================================================
//  13) TEMEL YÜZ (idle) + TEPKİ YÜZLERİ
// =====================================================================
void cizIdleYuz(int f) {
  bool kirp = ((f / 90) % 6) == 0;
  if (kirp) {
    gozKapali(GOZ_SOL_X, GOZ_Y);
    gozKapali(GOZ_SAG_X, GOZ_Y);
  } else {
    gozAcik(GOZ_SOL_X, GOZ_Y);
    gozAcik(GOZ_SAG_X, GOZ_Y);
    ekran.drawPixel(GOZ_SOL_X + 4, GOZ_Y + 4, SSD1306_WHITE);
    ekran.drawPixel(GOZ_SAG_X + 4, GOZ_Y + 4, SSD1306_WHITE);
  }
  kalinYay(64, 40, 14, 20, 160);
  ekran.fillCircle(28, 44, 4, SSD1306_WHITE);
  ekran.fillCircle(100, 44, 4, SSD1306_WHITE);
}

// --- 45 TEPKİNİN TAMAMI: her birinin kendine ait animasyonu var ---
void cizTepki(int f) {
  switch (aktifTepki) {

    // 1) MUTLU - gulumseme + yanak
    case TEP_MUTLU: {
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      kalinYay(64, 40, 14, 20, 160);
      ekran.fillCircle(28, 44, 4, SSD1306_WHITE);
      ekran.fillCircle(100, 44, 4, SSD1306_WHITE);
      break;
    }

    // 2) ŞAŞIRMA - kucuk bebek, O agiz
    case TEP_SASIRMA: {
      int pop = ((f / 6) % 2) ? 1 : 0;
      ekran.drawCircle(GOZ_SOL_X, GOZ_Y - pop, 11, SSD1306_WHITE);
      ekran.drawCircle(GOZ_SAG_X, GOZ_Y - pop, 11, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SOL_X, GOZ_Y - pop, 2, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SAG_X, GOZ_Y - pop, 2, SSD1306_WHITE);
      ekran.fillCircle(64, 46, 9, SSD1306_WHITE);
      ekran.fillCircle(64, 46, 6, SSD1306_BLACK);
      break;
    }

    // 3) GÜLÜMSEME - genis kapali goz gulusu
    case TEP_GULUMSEME: {
      kalinYay(GOZ_SOL_X, GOZ_Y, 8, 200, 340);
      kalinYay(GOZ_SAG_X, GOZ_Y, 8, 200, 340);
      kalinYay(64, 38, 16, 10, 170);
      ekran.fillCircle(26, 44, 4, SSD1306_WHITE);
      ekran.fillCircle(102, 44, 4, SSD1306_WHITE);
      break;
    }

    // 4) ZIPLAMA - yukari asagi ziplar, gölge kucultir
    case TEP_ZIPLAMA: {
      int hop = ((f / 5) % 2) ? 3 : 0;
      gozAcik(GOZ_SOL_X, GOZ_Y - hop); gozAcik(GOZ_SAG_X, GOZ_Y - hop);
      kalinYay(64, 40 - hop, 12, 20, 160);
      ekran.drawFastHLine(46, 62, 36 - hop * 4, SSD1306_WHITE);
      break;
    }

    // 5) GÖZ KIRPMA - tek goz kapanip acilir
    case TEP_GOZ_KIRPMA: {
      gozAcik(GOZ_SOL_X, GOZ_Y);
      if ((f / 8) % 2) {
        gozKapali(GOZ_SAG_X, GOZ_Y);
        ekran.drawLine(GOZ_SAG_X - 7, GOZ_Y + 4, GOZ_SAG_X + 7, GOZ_Y - 3, SSD1306_WHITE);
      } else gozAcik(GOZ_SAG_X, GOZ_Y);
      kalinYay(64, 38, 14, 20, 160);
      break;
    }

    // 6) KIZGIN - catik kas + titreme
    case TEP_KIZGIN: {
      int ox = ((f / 4) % 2) ? 1 : -1;
      gozAcik(GOZ_SOL_X + ox, GOZ_Y); gozAcik(GOZ_SAG_X + ox, GOZ_Y);
      ekran.drawLine(30 + ox, 10, 52 + ox, 17, SSD1306_WHITE);
      ekran.drawLine(98 + ox, 10, 76 + ox, 17, SSD1306_WHITE);
      kalinYay(64, 48, 14, 200, 340);
      break;
    }

    // 7) UTANGAÇ - kizarma + kacan bakis
    case TEP_UTANGAC: {
      int look = ((f / 10) % 2) ? -2 : 2;
      ekran.drawCircle(GOZ_SOL_X + look, GOZ_Y, GOZ_R, SSD1306_WHITE);
      ekran.drawCircle(GOZ_SAG_X + look, GOZ_Y, GOZ_R, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SOL_X + look + 3, GOZ_Y + 2, 3, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SAG_X + look + 3, GOZ_Y + 2, 3, SSD1306_WHITE);
      for (int i = 0; i < 3; i++) {
        ekran.drawLine(16 + i * 5, 44, 20 + i * 5, 38, SSD1306_WHITE);
        ekran.drawLine(94 + i * 5, 44, 98 + i * 5, 38, SSD1306_WHITE);
      }
      ekran.drawLine(58, 50, 70, 50, SSD1306_WHITE);
      break;
    }

    // 8) SIKILMIŞ - goz devirme + sisik yanak
    case TEP_SIKILMIS: {
      for (int e = 0; e < 2; e++) {
        int cx = e ? GOZ_SAG_X : GOZ_SOL_X;
        ekran.drawCircle(cx, GOZ_Y, GOZ_R, SSD1306_WHITE);
        ekran.fillCircle(cx, GOZ_Y - 5 + (f / 20) % 3, 3, SSD1306_WHITE);
      }
      ekran.fillCircle(20, 44, 7, SSD1306_WHITE);
      ekran.fillCircle(108, 44, 7, SSD1306_WHITE);
      ekran.drawLine(58, 46, 64, 44, SSD1306_WHITE);
      ekran.drawLine(64, 44, 70, 46, SSD1306_WHITE);
      break;
    }

    // 9) HEYECAN - yildiz gozler + zipzip
    case TEP_HEYECAN: {
      int hop = ((f / 6) % 2) ? 2 : 0;
      yildizCiz(GOZ_SOL_X, GOZ_Y - hop, 9);
      yildizCiz(GOZ_SAG_X, GOZ_Y - hop, 9);
      ekran.fillCircle(64, 44 - hop, 7, SSD1306_WHITE);
      ekran.fillCircle(64, 42 - hop, 4, SSD1306_BLACK);
      ekran.drawPixel(20, 14, SSD1306_WHITE);
      ekran.drawPixel(108, 14, SSD1306_WHITE);
      break;
    }

    // 10) KORKU - titreyen minik bebek
    case TEP_KORKU: {
      int t = ((f / 3) % 2) ? 2 : -2;
      ekran.drawCircle(GOZ_SOL_X + t, GOZ_Y, 11, SSD1306_WHITE);
      ekran.drawCircle(GOZ_SAG_X + t, GOZ_Y, 11, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SOL_X + t, GOZ_Y + 2, 1, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SAG_X + t, GOZ_Y + 2, 1, SSD1306_WHITE);
      for (int x = 50; x < 78; x += 4)
        ekran.drawLine(x, 46 + ((x / 4) % 2) * 3, x + 4, 46 + (((x / 4) + 1) % 2) * 3, SSD1306_WHITE);
      break;
    }

    // 11) YORGUN - yari kapak + goz alti torbasi
    case TEP_YORGUN: {
      for (int e = 0; e < 2; e++) {
        int cx = e ? GOZ_SAG_X : GOZ_SOL_X;
        ekran.drawCircle(cx, GOZ_Y, GOZ_R, SSD1306_WHITE);
        ekran.fillCircle(cx, GOZ_Y + 3, 3, SSD1306_WHITE);
        ekran.fillRect(cx - 11, GOZ_Y - 11, 22, 13, SSD1306_BLACK);
        ekran.drawFastHLine(cx - 10, GOZ_Y - 9, 21, SSD1306_WHITE);
      }
      yay(GOZ_SOL_X, GOZ_Y + 13, 8, 20, 160);
      yay(GOZ_SAG_X, GOZ_Y + 13, 8, 20, 160);
      ekran.drawFastHLine(56, 50, 17, SSD1306_WHITE);
      break;
    }

    // 12) HAVALI - yukaridan inen gunes gozlugu
    case TEP_HAVALI: {
      int gy = 20 + ((f < 10) ? f * 2 : 20);
      if (gy < 40) { gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y); }
      ekran.fillRect(26, gy, 34, 13, SSD1306_WHITE);
      ekran.fillRect(68, gy, 34, 13, SSD1306_WHITE);
      ekran.fillRect(60, gy + 3, 8, 3, SSD1306_WHITE);
      ekran.drawLine(32, gy + 10, 42, gy + 2, SSD1306_BLACK);
      ekran.drawLine(74, gy + 10, 84, gy + 2, SSD1306_BLACK);
      yay(64, 46, 14, 20, 140);
      break;
    }

    // 13) MÜZİK - kulaklik + kafa sallama
    case TEP_MUZIK: {
      int bob = ((f / 10) % 2) ? 1 : -1;
      yay(64, 24, 32, 190, 350);
      ekran.fillRect(24, 22, 7, 14, SSD1306_WHITE);
      ekran.fillRect(97, 22, 7, 14, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SOL_X, GOZ_Y + bob, 7, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SAG_X, GOZ_Y + bob, 7, SSD1306_WHITE);
      kalinYay(64, 42 + bob, 9, 20, 160);
      int ny = 8 + (f % 6);
      ekran.fillCircle(112, ny, 2, SSD1306_WHITE);
      ekran.drawLine(114, ny, 114, ny - 6, SSD1306_WHITE);
      break;
    }

    // 14) ŞÜPHECİ - tek kas havada
    case TEP_SUPHECI: {
      ekran.drawCircle(GOZ_SOL_X, GOZ_Y, GOZ_R, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SOL_X + 3, GOZ_Y, 3, SSD1306_WHITE);
      ekran.drawLine(GOZ_SAG_X - 9, GOZ_Y, GOZ_SAG_X + 9, GOZ_Y, SSD1306_WHITE);
      ekran.drawLine(32, 12, 50, 14, SSD1306_WHITE);
      ekran.drawLine(78, 4, 96, 10, SSD1306_WHITE);
      ekran.drawLine(54, 46, 62, 44, SSD1306_WHITE);
      ekran.drawLine(62, 44, 74, 46, SSD1306_WHITE);
      break;
    }

    // 15) ACIKMIŞ - yalayan dil
    case TEP_ACIKMIS: {
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      ekran.fillCircle(64, 44, 9, SSD1306_WHITE);
      ekran.fillCircle(64, 42, 6, SSD1306_BLACK);
      int tx = 64 + ((f / 6) % 3 - 1) * 3;
      ekran.fillRect(tx - 4, 46, 8, 8, SSD1306_WHITE);
      ekran.fillCircle(tx, 53, 4, SSD1306_WHITE);
      ekran.fillCircle(24, 42, 3, SSD1306_WHITE);
      ekran.fillCircle(104, 42, 3, SSD1306_WHITE);
      break;
    }

    // 16) KUTLAMA - parti sapkasi + konfeti
    case TEP_KUTLAMA: {
      gozAcik(GOZ_SOL_X, GOZ_Y + 2); gozAcik(GOZ_SAG_X, GOZ_Y + 2);
      ekran.fillTriangle(50, 14, 78, 14, 64, 0, SSD1306_WHITE);
      ekran.drawFastHLine(50, 14, 29, SSD1306_WHITE);
      ekran.fillCircle(64, 42, 8, SSD1306_WHITE);
      ekran.fillCircle(64, 40, 5, SSD1306_BLACK);
      for (int i = 0; i < 8; i++) {
        int cx = 8 + i * 15;
        int cy = (f * 3 + i * 7) % 40 + 4;
        ekran.drawPixel(cx, cy, SSD1306_WHITE);
        if (i % 2) ekran.drawPixel(cx + 1, cy + 1, SSD1306_WHITE);
      }
      break;
    }

    // 17) BAŞ DÖNMESİ - donen spiraller
    case TEP_BAS_DONMESI: {
      float a0 = f * 0.35f;
      spiralCiz(GOZ_SOL_X, GOZ_Y, 10, a0);
      spiralCiz(GOZ_SAG_X, GOZ_Y, 10, a0);
      for (int x = 48; x < 82; x += 4)
        ekran.drawLine(x, 46 + (int)(sinf((x + f * 4) * 0.35f) * 2),
                       x + 4, 46 + (int)(sinf((x + 4 + f * 4) * 0.35f) * 2), SSD1306_WHITE);
      break;
    }

    // 18) ŞARJ - simsek gozler + dolan bar
    case TEP_SARJ: {
      simsekCiz(GOZ_SOL_X, GOZ_Y, 9);
      simsekCiz(GOZ_SAG_X, GOZ_Y, 9);
      kalinYay(64, 40, 12, 20, 160);
      int doluluk = (f / 3) % 20;
      int d = doluluk < 10 ? doluluk : 19 - doluluk;
      ekran.drawRect(34, 54, 60, 8, SSD1306_WHITE);
      int w = (d * 56) / 9;
      if (w > 0) ekran.fillRect(36, 56, w, 4, SSD1306_WHITE);
      break;
    }

    // 19) ÖPÜCÜK - kalp ucar
    case TEP_OPUCUK: {
      bool op = (f / 8) % 2;
      if (op) gozKapali(GOZ_SOL_X, GOZ_Y); else gozAcik(GOZ_SOL_X, GOZ_Y);
      gozAcik(GOZ_SAG_X, GOZ_Y);
      ekran.drawCircle(64, 46, 4, SSD1306_WHITE);
      ekran.fillCircle(64, 46, 2, SSD1306_WHITE);
      if (op) kalpCiz(84 + (f % 40), 44 - (f % 40) / 2, 4);
      break;
    }

    // 20) KARIŞIK - asimetrik gozler + "?"
    case TEP_KARISIK: {
      ekran.fillCircle(GOZ_SOL_X, GOZ_Y, 9, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SOL_X + 2, GOZ_Y + 1, 3, SSD1306_BLACK);
      ekran.fillCircle(GOZ_SAG_X, GOZ_Y + 2, 6, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SAG_X - 2, GOZ_Y + 3, 2, SSD1306_BLACK);
      ekran.drawLine(56, 48, 62, 44, SSD1306_WHITE);
      ekran.drawLine(62, 44, 70, 48, SSD1306_WHITE);
      if ((f / 8) % 2) metinYaz(104, 2, "?", 2);
      break;
    }

    // 21) GLITCH - karlanma
    case TEP_GLITCH: {
      ekran.fillRect(GOZ_SOL_X - 9, GOZ_Y - 6, 18, 5, SSD1306_WHITE);
      ekran.fillRect(GOZ_SOL_X - 9 + ((f % 3) - 1) * 3, GOZ_Y + 1, 12, 5, SSD1306_WHITE);
      ekran.fillRect(GOZ_SAG_X - 9, GOZ_Y - 6, 18, 5, SSD1306_WHITE);
      ekran.fillRect(GOZ_SAG_X - 9 + (f % 2) * 4, GOZ_Y + 1, 12, 5, SSD1306_WHITE);
      ekran.drawLine(50, 46, 60, 42, SSD1306_WHITE);
      ekran.drawLine(62, 48, 72, 44, SSD1306_WHITE);
      for (int i = 0; i < 50; i++)
        ekran.drawPixel((i * 37 + f * 53) % 128, (i * 61 + f * 17) % 64, SSD1306_WHITE);
      break;
    }

    // 22) AŞKIN - kalp gozler + ucusan kalpler
    case TEP_ASKIN: {
      int beat = ((f / 8) % 2) ? 1 : 0;
      kalpCiz(GOZ_SOL_X, GOZ_Y, 9 + beat);
      kalpCiz(GOZ_SAG_X, GOZ_Y, 9 + beat);
      kalpCiz(64, 46, 6);
      int p = f % 10;
      if (p < 5) { ekran.drawPixel(16, 20 - p * 2, SSD1306_WHITE); ekran.drawPixel(112, 20 - ((p + 3) % 5) * 2, SSD1306_WHITE); }
      break;
    }

    // 23) UYUMA - Zzz
    case TEP_UYUMA: cizUyuma(f); break;

    // 24) ESNEME - genis agiz
    case TEP_ESNEME: cizEsneme(f); break;

    // 25) GÜLME - gozler kapali, agiz kahkaha
    case TEP_GULME: {
      kalinYay(GOZ_SOL_X, GOZ_Y - 1, 9, 200, 340);
      kalinYay(GOZ_SAG_X, GOZ_Y - 1, 9, 200, 340);
      ekran.fillCircle(64, 42, 11, SSD1306_WHITE);
      ekran.fillCircle(64, 40, 8, SSD1306_BLACK);
      ekran.drawPixel(56, 48, SSD1306_WHITE);
      ekran.drawPixel(64, 50, SSD1306_WHITE);
      ekran.drawPixel(72, 48, SSD1306_WHITE);
      break;
    }

    // 26) DÜŞÜNME - gozler yukari, baloncuk
    case TEP_DUSUNME: {
      ekran.drawCircle(GOZ_SOL_X, GOZ_Y, GOZ_R, SSD1306_WHITE);
      ekran.drawCircle(GOZ_SAG_X, GOZ_Y, GOZ_R, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SOL_X, GOZ_Y - 5, 3, SSD1306_WHITE);
      ekran.fillCircle(GOZ_SAG_X, GOZ_Y - 5, 3, SSD1306_WHITE);
      ekran.drawFastHLine(58, 48, 12, SSD1306_WHITE);
      ekran.drawCircle(104, 16, 9, SSD1306_WHITE);
      ekran.drawCircle(96, 28, 3, SSD1306_WHITE);
      ekran.drawCircle(90, 34, 1, SSD1306_WHITE);
      if ((f / 6) % 2) { ekran.drawPixel(101, 14, SSD1306_WHITE); ekran.drawPixel(107, 18, SSD1306_WHITE); }
      break;
    }

    // 27) HAPSIRMA - burnu cizgili hapsirma
    case TEP_HAPSIRMA: {
      gozKapali(GOZ_SOL_X, GOZ_Y);
      gozKapali(GOZ_SAG_X, GOZ_Y);
      ekran.fillCircle(64, 46, 8, SSD1306_WHITE);
      ekran.fillCircle(64, 46, 5, SSD1306_BLACK);
      for (int i = 0; i < 3; i++) {
        ekran.drawLine(96 + i * 3, 18 + i * 2, 110 + i * 3, 24 + i * 2, SSD1306_WHITE);
      }
      break;
    }

    // 28) ORKESTRA - sallanan nota cubuklari
    case TEP_ORKESTRA: {
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      kalinYay(64, 40, 12, 20, 160);
      for (int i = 0; i < 4; i++) {
        int h = 6 + ((f / 4 + i * 2) % 12);
        ekran.drawFastVLine(18 + i * 8, 40 - h, h, SSD1306_WHITE);
        ekran.drawFastVLine(94 + i * 8, 40 - h, h, SSD1306_WHITE);
      }
      break;
    }

    // 29) KAFA SALLAMA - yana sallanir
    case TEP_KAFA_SALLAMA: {
      int dx = ((f / 6) % 2) ? 3 : -3;
      gozAcik(GOZ_SOL_X + dx, GOZ_Y); gozAcik(GOZ_SAG_X + dx, GOZ_Y);
      kalinYay(64 + dx, 40, 12, 20, 160);
      ekran.drawLine(24 - dx, 20, 30 - dx, 14, SSD1306_WHITE);
      break;
    }

    // 30) EL SALLAMA - el sallar
    case TEP_EL_SALLAMA: {
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      kalinYay(64, 40, 13, 20, 160);
      int d = ((f / 5) % 2) ? 4 : -4;
      ekran.drawLine(104, 30, 112 + d, 16, SSD1306_WHITE);
      ekran.drawLine(108, 32, 116 + d, 18, SSD1306_WHITE);
      break;
    }

    // 31) GÖZ KIRPMA SERİSİ - art arda kirpar
    case TEP_GOZ_KIRP_SERI: {
      bool k = (f / 4) % 2;
      if (k) { gozKapali(GOZ_SOL_X, GOZ_Y); gozKapali(GOZ_SAG_X, GOZ_Y); }
      else { gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y); }
      kalinYay(64, 38, 14, 20, 160);
      break;
    }

    // 32) TİTREME - tum yuz titrer
    case TEP_TITREME: {
      int t = ((f / 2) % 2) ? 1 : -1;
      gozAcik(GOZ_SOL_X + t, GOZ_Y + t); gozAcik(GOZ_SAG_X + t, GOZ_Y + t);
      ekran.drawLine(56 + t, 46 + t, 72 + t, 46 + t, SSD1306_WHITE);
      ekran.drawLine(20, 30 + t * 2, 24, 30 + t * 2, SSD1306_WHITE);
      ekran.drawLine(104, 30 - t * 2, 108, 30 - t * 2, SSD1306_WHITE);
      break;
    }

    // 33) POFLA - sisik yanak + oflama
    case TEP_POFLA: {
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      int puff = 6 + ((f / 8) % 3);
      ekran.fillCircle(20, 44, puff, SSD1306_WHITE);
      ekran.fillCircle(108, 44, puff, SSD1306_WHITE);
      ekran.drawLine(58, 46, 64, 44, SSD1306_WHITE);
      ekran.drawLine(64, 44, 70, 46, SSD1306_WHITE);
      ekran.drawPixel(116, 34, SSD1306_WHITE);
      ekran.drawPixel(120, 30, SSD1306_WHITE);
      break;
    }

    // 34) DANST - zipla + nota
    case TEP_DANST: {
      int hop = ((f / 4) % 2) ? 3 : 0;
      gozAcik(GOZ_SOL_X, GOZ_Y - hop); gozAcik(GOZ_SAG_X, GOZ_Y - hop);
      kalinYay(64, 40 - hop, 12, 20, 160);
      ekran.drawPixel(14, 14 + (f % 16), SSD1306_WHITE);
      ekran.drawPixel(112, 30 - (f % 16), SSD1306_WHITE);
      ekran.drawFastHLine(40, 62, 48, SSD1306_WHITE);
      break;
    }

    // 35) KALP ATIŞI - kalp buyur kuculur
    case TEP_KALP_ATISI: {
      int beat = ((f / 5) % 2) ? 2 : 0;
      kalpCiz(GOZ_SOL_X, GOZ_Y, 8 + beat);
      kalpCiz(GOZ_SAG_X, GOZ_Y, 8 + beat);
      kalinYay(64, 40, 12, 20, 160);
      break;
    }

    // 36) YILDIZ - parlayan yildizlar
    case TEP_YILDIZ: {
      yildizCiz(GOZ_SOL_X, GOZ_Y, 8);
      yildizCiz(GOZ_SAG_X, GOZ_Y, 8);
      kalinYay(64, 40, 12, 20, 160);
      if ((f / 5) % 2) { ekran.drawPixel(64, 6, SSD1306_WHITE); ekran.drawPixel(20, 20, SSD1306_WHITE); }
      break;
    }

    // 37) BULUT - huzunlu bulut
    case TEP_BULUT: {
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      yay(64, 50, 14, 200, 340);
      ekran.drawCircle(36, 14, 7, SSD1306_WHITE);
      ekran.drawCircle(52, 10, 9, SSD1306_WHITE);
      ekran.drawCircle(72, 12, 8, SSD1306_WHITE);
      ekran.drawCircle(88, 16, 6, SSD1306_WHITE);
      ekran.drawFastHLine(30, 22, 64, SSD1306_WHITE);
      if ((f / 6) % 2) { ekran.drawPixel(46, 26, SSD1306_WHITE); ekran.drawPixel(66, 26, SSD1306_WHITE); }
      break;
    }

    // 38) GÜNEŞ - isinlar
    case TEP_GUNES: {
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      kalinYay(64, 40, 12, 20, 160);
      for (int i = 0; i < 8; i++) {
        float a = (i * 45 + f * 3) * DERECE;
        int x1 = 64 + (int)(cosf(a) * 26), y1 = 30 + (int)(sinf(a) * 26);
        int x2 = 64 + (int)(cosf(a) * 32), y2 = 30 + (int)(sinf(a) * 32);
        ekran.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
      }
      break;
    }

    // 39) ATEŞ - alev gozler
    case TEP_ATES: {
      ekran.fillTriangle(GOZ_SOL_X - 9, GOZ_Y + 4, GOZ_SOL_X + 9, GOZ_Y + 4, GOZ_SOL_X, GOZ_Y - 10, SSD1306_WHITE);
      ekran.fillTriangle(GOZ_SAG_X - 9, GOZ_Y + 4, GOZ_SAG_X + 9, GOZ_Y + 4, GOZ_SAG_X, GOZ_Y - 10, SSD1306_WHITE);
      int w = 4 + ((f / 4) % 4);
      ekran.fillCircle(GOZ_SOL_X, GOZ_Y + 2, w, SSD1306_BLACK);
      ekran.fillCircle(GOZ_SAG_X, GOZ_Y + 2, w, SSD1306_BLACK);
      kalinYay(64, 50, 12, 200, 340);
      break;
    }

    // 40) BUZ - buz kristalleri
    case TEP_BUZ: {
      for (int e = 0; e < 2; e++) {
        int cx = e ? GOZ_SAG_X : GOZ_SOL_X;
        for (int i = 0; i < 3; i++) {
          float a = (i * 60 + f * 2) * DERECE;
          ekran.drawLine(cx - (int)(cosf(a) * 9), GOZ_Y - (int)(sinf(a) * 9),
                         cx + (int)(cosf(a) * 9), GOZ_Y + (int)(sinf(a) * 9), SSD1306_WHITE);
        }
      }
      ekran.drawFastHLine(56, 48, 17, SSD1306_WHITE);
      break;
    }

    // 41) ROKET - yukari firlar
    case TEP_ROKET: {
      int hop = ((f / 4) % 2) ? 3 : 0;
      gozAcik(GOZ_SOL_X, GOZ_Y - hop); gozAcik(GOZ_SAG_X, GOZ_Y - hop);
      kalinYay(64, 40 - hop, 12, 20, 160);
      ekran.fillTriangle(60, 58, 68, 58, 64, 64, SSD1306_WHITE);
      ekran.drawFastVLine(64, 54, 6, SSD1306_WHITE);
      break;
    }

    // 42) YILDIRIM - simsek carpar
    case TEP_YILDIRIM: {
      if ((f / 3) % 2) ekran.fillRect(0, 0, 128, 64, SSD1306_WHITE);
      ekran.fillRect(58, 4, 12, 40, SSD1306_BLACK);
      simsekCiz(64, 28, 18);
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      break;
    }

    // 43) BALON - ucan balon
    case TEP_BALON: {
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      kalinYay(64, 40, 12, 20, 160);
      int by = 46 - (f % 40);
      ekran.drawCircle(104, by, 7, SSD1306_WHITE);
      ekran.drawLine(104, by + 7, 104, by + 12, SSD1306_WHITE);
      break;
    }

    // 44) YAPRAK - dusen yapraklar
    case TEP_YAPRAK: {
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      kalinYay(64, 40, 12, 20, 160);
      for (int i = 0; i < 6; i++) {
        int x = (i * 23 + (f / 2)) % 128;
        int y = (i * 13 + f) % 64;
        ekran.drawLine(x, y, x + 3, y + 2, SSD1306_WHITE);
      }
      break;
    }

    // 45) KAR - kar taneleri
    case TEP_KAR: {
      gozAcik(GOZ_SOL_X, GOZ_Y); gozAcik(GOZ_SAG_X, GOZ_Y);
      kalinYay(64, 40, 12, 20, 160);
      for (int i = 0; i < 14; i++) {
        int x = (i * 31 + (f / 3)) % 128;
        int y = (i * 19 + f) % 64;
        ekran.drawPixel(x, y, SSD1306_WHITE);
      }
      break;
    }

    default: cizIdleYuz(f); break;
  }
}

//  14) SEVGİ (CUDDLE) EKRANI
// =====================================================================
void sevgiBaslat() {
  sevgiModu = true;
  sevgiBaslangic = millis();
  sevgiKalpSayisi = 12;
  for (int i = 0; i < sevgiKalpSayisi; i++) {
    kalpler[i].x = random(10, 118);
    kalpler[i].y = random(20, 70);
    kalpler[i].hiz = random(1, 3);
    kalpler[i].boyut = random(3, 7);
  }
  sesMirildan();
}

void cizSevgi(int f) {
  // mutlu kisik gozler
  kalinYay(GOZ_SOL_X, GOZ_Y, 9, 200, 340);
  kalinYay(GOZ_SAG_X, GOZ_Y, 9, 200, 340);
  kalinYay(64, 38, 14, 20, 160);
  ekran.fillCircle(28, 44, 4, SSD1306_WHITE);
  ekran.fillCircle(100, 44, 4, SSD1306_WHITE);

  // ucuskan kalpler
  for (int i = 0; i < sevgiKalpSayisi; i++) {
    kalpCiz(kalpler[i].x, kalpler[i].y, kalpler[i].boyut);
    kalpler[i].y -= kalpler[i].hiz;
    if (kalpler[i].y < 4) {
      kalpler[i].y = 70;
      kalpler[i].x = random(10, 118);
    }
  }
}

// =====================================================================
//  15) OYUNLAR (hepsi tek buton)
// =====================================================================
struct OyunDurumu {
  bool  basladi;
  bool  bitti;
  int   puan;
  int   yuksekPuan;
  unsigned long sonGuncelleme;
  unsigned long kareSuresi;   // ms
};

OyunDurumu oyun = { false, false, 0, 0, 0, 40 };

// --- Dino Run ---
float dinoY, dinoHiz;
int   dinoZemin;
int   dinoEngelX, dinoEngelY, dinoEngelBoy;
int   dinoHizSabit;

// --- Flappy ---
float kusY, kusHiz;
int   boruX, boruBosluk, boruGecildi;

// --- Yilan ---
#define YILAN_MAX 64
struct Nokta { int8_t x, y; };
Nokta yilan[YILAN_MAX];
int   yilanUzunluk;
int   yilanYon;         // 0 saga, 1 asagi, 2 sola, 3 yukari
Nokta yem;
bool  yilanBekliyor;

// --- Pong ---
float pongTopX, pongTopY, pongTopHizX, pongTopHizY;
int   pongRaketY, pongRaketYon;
int   pongYapayZekaY;

// --- Breakout ---
#define TUGLA_SATIR 3
#define TUGLA_KOLON 8
bool  tuglalar[TUGLA_SATIR][TUGLA_KOLON];
float topX, topY, topHizX, topHizY;
int   raketX, raketYon;

// --- Stacker ---
#define STACK_MAX 8
int   stackBlokX[STACK_MAX];
int   stackGenislik[STACK_MAX];
int   stackSeviye;
int   stackHareketX;
int   stackHareketYon;

void oyunSifirla() {
  oyun.basladi = false;
  oyun.bitti = false;
  oyun.puan = 0;
  oyun.sonGuncelleme = millis();

  switch (aktifOyun) {
    case OYUN_DINO:
      dinoY = 40; dinoHiz = 0; dinoZemin = 46;
      dinoEngelX = 128; dinoEngelBoy = 10; dinoEngelY = 40;
      dinoHizSabit = 3;
      break;

    case OYUN_FLAPPY:
      kusY = 32; kusHiz = 0;
      boruX = 128; boruBosluk = 24; boruGecildi = 0;
      break;

    case OYUN_YILAN:
      yilanUzunluk = 4;
      yilanYon = 0;
      for (int i = 0; i < yilanUzunluk; i++) {
        yilan[i].x = 10 - i * 3;
        yilan[i].y = 5;
      }
      yem.x = random(2, 20);
      yem.y = random(1, 7);
      yilanBekliyor = false;
      break;

    case OYUN_PONG:
      pongTopX = 64; pongTopY = 32; pongTopHizX = 2.2f; pongTopHizY = 1.4f;
      pongRaketY = 26; pongRaketYon = 1;
      pongYapayZekaY = 26;
      break;

    case OYUN_BREAKOUT:
      for (int r = 0; r < TUGLA_SATIR; r++)
        for (int c = 0; c < TUGLA_KOLON; c++) tuglalar[r][c] = true;
      topX = 64; topY = 50; topHizX = 1.6f; topHizY = -2.0f;
      raketX = 52; raketYon = 1;
      break;

    case OYUN_STACKER:
      stackSeviye = 0;
      for (int i = 0; i < STACK_MAX; i++) {
        stackBlokX[i] = 44;
        stackGenislik[i] = 40;
      }
      stackHareketX = 0;
      stackHareketYon = 1;
      stackGenislik[0] = 40;
      break;
  }
}

void oyunBaslat(OyunTuru tur) {
  aktifOyun = tur;
  oyunSifirla();
  oyun.basladi = true;
  oyun.bitti = false;
  sistemModu = MOD_OYUNLAR;
  sesSelamlama();
}

// --- Oyun kare guncellemesi (her oyun ~25-40 FPS) ---
void oyunGuncelle(unsigned long simdi) {
  if (aktifOyun == OYUN_YOK) return;
  if (!oyun.basladi || oyun.bitti) return;
  if (simdi - oyun.sonGuncelleme < oyun.kareSuresi) return;
  oyun.sonGuncelleme = simdi;

  switch (aktifOyun) {

    // ---------------- DINO RUN ----------------
    case OYUN_DINO: {
      dinoHiz += 0.9f;
      dinoY += dinoHiz;
      if (dinoY >= dinoZemin) { dinoY = dinoZemin; dinoHiz = 0; }

      dinoEngelX -= dinoHizSabit;
      if (dinoEngelX < -8) {
        dinoEngelX = 128;
        dinoEngelBoy = random(8, 16);
        oyun.puan++;
        if (oyun.puan % 3 == 0 && dinoHizSabit < 7) dinoHizSabit++;
      }
      // carpisma
      if (dinoEngelX < 22 && dinoEngelX + 8 > 12 && dinoY > dinoZemin - 10) {
        oyun.bitti = true;
        sesOyunBitti();
      }
      break;
    }

    // ---------------- FLAPPY ----------------
    case OYUN_FLAPPY: {
      kusHiz += 0.75f;
      kusY += kusHiz;
      if (kusY < 0) { kusY = 0; kusHiz = 0; }
      if (kusY > 56) { oyun.bitti = true; sesOyunBitti(); break; }

      boruX -= 2;
      if (boruX < -14) {
        boruX = 128;
        boruBosluk = random(16, 44);
        oyun.puan++;
      }
      // boru carpismasi
      if (boruX < 26 && boruX + 12 > 14) {
        if (kusY < boruBosluk || kusY + 6 > boruBosluk + 18) {
          oyun.bitti = true;
          sesOyunBitti();
        }
      }
      break;
    }

    // ---------------- YILAN ----------------
    case OYUN_YILAN: {
      Nokta bas = yilan[0];
      Nokta yeni = bas;
      switch (yilanYon) {
        case 0: yeni.x += 1; break;
        case 1: yeni.y += 1; break;
        case 2: yeni.x -= 1; break;
        case 3: yeni.y -= 1; break;
      }
      // ekran sinirlari (128x64 -> 2x8 piksel hucre, y=0..7 ust bilgi disi)
      if (yeni.x < 0 || yeni.x >= 32 || yeni.y < 0 || yeni.y >= 7) {
        oyun.bitti = true;
        sesOyunBitti();
        break;
      }
      // kendine carpma
      bool carpti = false;
      for (int i = 0; i < yilanUzunluk; i++) {
        if (yilan[i].x == yeni.x && yilan[i].y == yeni.y) carpti = true;
      }
      if (carpti) {
        oyun.bitti = true;
        sesOyunBitti();
        break;
      }

      // kaydir
      for (int i = yilanUzunluk - 1; i > 0; i--) yilan[i] = yilan[i - 1];
      yilan[0] = yeni;

      // yem
      if (yeni.x == yem.x && yeni.y == yem.y) {
        if (yilanUzunluk < YILAN_MAX) yilanUzunluk++;
        oyun.puan++;
        sesPuan();
        yem.x = random(0, 32);
        yem.y = random(0, 7);
      }
      break;
    }

    // ---------------- PONG ----------------
    case OYUN_PONG: {
      pongTopX += pongTopHizX;
      pongTopY += pongTopHizY;

      if (pongTopY < 8) { pongTopY = 8; pongTopHizY = -pongTopHizY; }
      if (pongTopY > 60) { pongTopY = 60; pongTopHizY = -pongTopHizY; }

      // raket surekli hareket eder
      pongRaketY += pongRaketYon * 3;
      if (pongRaketY < 8) { pongRaketY = 8; pongRaketYon = 1; }
      if (pongRaketY > 52) { pongRaketY = 52; pongRaketYon = -1; }

      // yapay zeka raket (sol)
      if (pongYapayZekaY + 5 < pongTopY) pongYapayZekaY += 2;
      else if (pongYapayZekaY + 5 > pongTopY) pongYapayZekaY -= 2;
      if (pongYapayZekaY < 8) pongYapayZekaY = 8;
      if (pongYapayZekaY > 52) pongYapayZekaY = 52;

      // sag raket (oyuncu)
      if (pongTopX > 112 && pongTopX < 120 && pongTopY > pongRaketY - 3 && pongTopY < pongRaketY + 14) {
        pongTopX = 112;
        pongTopHizX = -fabsf(pongTopHizX) - 0.05f;
        pongTopHizY += ((pongTopY - (pongRaketY + 5)) * 0.12f);
        oyun.puan++;
        sesPuan();
      }
      // sol raket
      if (pongTopX < 12 && pongTopX > 4 && pongTopY > pongYapayZekaY - 3 && pongTopY < pongYapayZekaY + 14) {
        pongTopX = 12;
        pongTopHizX = fabsf(pongTopHizX) + 0.05f;
      }
      // kacirilan top
      if (pongTopX < 0 || pongTopX > 128) {
        oyun.bitti = true;
        sesOyunBitti();
      }
      break;
    }

    // ---------------- BREAKOUT ----------------
    case OYUN_BREAKOUT: {
      topX += topHizX;
      topY += topHizY;

      if (topX < 2) { topX = 2; topHizX = -topHizX; }
      if (topX > 125) { topX = 125; topHizX = -topHizX; }
      if (topY < 10) { topY = 10; topHizY = -topHizY; }

      // raket yon degistirir (otomatik salinim)
      raketX += raketYon * 2;
      if (raketX < 2) { raketX = 2; raketYon = 1; }
      if (raketX > 106) { raketX = 106; raketYon = -1; }

      // raketten sekme
      if (topY > 52 && topY < 58 && topX > raketX - 2 && topX < raketX + 22) {
        topY = 52;
        topHizY = -fabsf(topHizY);
        topHizX += ((topX - (raketX + 10)) * 0.08f);
      }

      // tugla carpismasi
      for (int r = 0; r < TUGLA_SATIR; r++) {
        for (int c = 0; c < TUGLA_KOLON; c++) {
          if (!tuglalar[r][c]) continue;
          int bx = c * 15 + 3;
          int by = r * 6 + 12;
          if (topX > bx - 2 && topX < bx + 15 && topY > by - 2 && topY < by + 7) {
            tuglalar[r][c] = false;
            topHizY = -topHizY;
            oyun.puan += 10;
            sesPuan();
          }
        }
      }

      // alt cizgi
      if (topY > 62) {
        oyun.bitti = true;
        sesOyunBitti();
      }

      // kazanma
      bool kaldi = false;
      for (int r = 0; r < TUGLA_SATIR; r++)
        for (int c = 0; c < TUGLA_KOLON; c++) if (tuglalar[r][c]) kaldi = true;
      if (!kaldi) { oyun.bitti = true; sesSelamlama(); }
      break;
    }

    // ---------------- STACKER ----------------
    case OYUN_STACKER: {
      if (stackSeviye == 0) break;
      stackHareketX += stackHareketYon * 3;
      if (stackHareketX < 0) { stackHareketX = 0; stackHareketYon = 1; }
      if (stackHareketX > 128 - stackGenislik[stackSeviye]) {
        stackHareketX = 128 - stackGenislik[stackSeviye];
        stackHareketYon = -1;
      }
      break;
    }
  }

  if (oyun.bitti && oyun.puan > oyun.yuksekPuan) oyun.yuksekPuan = oyun.puan;
}

// --- Oyun cizimi ---
void oyunCiz() {
  char tampon[24];

  if (aktifOyun == OYUN_YOK) {
    metinYaz(20, 24, "OYUN SECILMEDI");
    metinYaz(8, 40, "Web'den oyun sec");
    return;
  }

  if (oyun.bitti) {
    metinYaz(24, 12, "OYUN BITTI", 1);
    snprintf(tampon, sizeof(tampon), "Puan: %d", oyun.puan);
    metinYaz(24, 26, tampon);
    snprintf(tampon, sizeof(tampon), "Rekor: %d", oyun.yuksekPuan);
    metinYaz(24, 36, tampon);
    metinYaz(12, 52, "Dokun: yeniden");
    return;
  }

  switch (aktifOyun) {
    case OYUN_DINO: {
      // zemin
      ekran.drawFastHLine(0, 56, 128, SSD1306_WHITE);
      // dino
      ekran.fillRect(12, (int)dinoY, 10, 10, SSD1306_WHITE);
      ekran.fillRect(22, (int)dinoY + 2, 3, 3, SSD1306_WHITE);
      // engel (kaktus)
      ekran.fillRect(dinoEngelX, dinoEngelY, 8, dinoEngelBoy, SSD1306_WHITE);
      // puan
      snprintf(tampon, sizeof(tampon), "%d", oyun.puan);
      metinYaz(104, 0, tampon);
      break;
    }

    case OYUN_FLAPPY: {
      ekran.fillRect(boruX, 0, 12, boruBosluk, SSD1306_WHITE);
      ekran.fillRect(boruX, boruBosluk + 18, 12, 64, SSD1306_WHITE);
      ekran.fillRect(14, (int)kusY, 7, 6, SSD1306_WHITE);
      snprintf(tampon, sizeof(tampon), "%d", oyun.puan);
      metinYaz(104, 0, tampon);
      break;
    }

    case OYUN_YILAN: {
      // yem (hucre: 4x8 piksel, 32x7 izgara)
      ekran.fillRect(yem.x * 4, 8 + yem.y * 8, 4, 7, SSD1306_WHITE);
      // yilan
      for (int i = 0; i < yilanUzunluk; i++) {
        if (i == 0) ekran.fillRect(yilan[i].x * 4, 8 + yilan[i].y * 8, 4, 7, SSD1306_WHITE);
        else ekran.drawRect(yilan[i].x * 4, 8 + yilan[i].y * 8, 3, 6, SSD1306_WHITE);
      }
      snprintf(tampon, sizeof(tampon), "Skor %d", oyun.puan);
      metinYaz(0, 0, tampon);
      break;
    }

    case OYUN_PONG: {
      ekran.drawFastVLine(63, 8, 56, SSD1306_WHITE);
      ekran.fillRect(4, pongYapayZekaY, 4, 12, SSD1306_WHITE);
      ekran.fillRect(120, pongRaketY, 4, 12, SSD1306_WHITE);
      ekran.fillRect((int)pongTopX, (int)pongTopY, 3, 3, SSD1306_WHITE);
      snprintf(tampon, sizeof(tampon), "%d", oyun.puan);
      metinYaz(58, 0, tampon);
      break;
    }

    case OYUN_BREAKOUT: {
      for (int r = 0; r < TUGLA_SATIR; r++)
        for (int c = 0; c < TUGLA_KOLON; c++)
          if (tuglalar[r][c]) ekran.drawRect(c * 15 + 3, r * 6 + 12, 14, 5, SSD1306_WHITE);
      ekran.fillRect(raketX, 58, 20, 3, SSD1306_WHITE);
      ekran.fillCircle((int)topX, (int)topY, 2, SSD1306_WHITE);
      snprintf(tampon, sizeof(tampon), "%d", oyun.puan);
      metinYaz(104, 0, tampon);
      break;
    }

    case OYUN_STACKER: {
      for (int i = 1; i <= stackSeviye; i++) {
        int y = 58 - (i - 1) * 6;
        ekran.fillRect(stackBlokX[i - 1], y, stackGenislik[i - 1], 5, SSD1306_WHITE);
      }
      int y = 58 - stackSeviye * 6;
      ekran.fillRect(stackHareketX, y, stackGenislik[stackSeviye], 5, SSD1306_WHITE);
      snprintf(tampon, sizeof(tampon), "Kat %d", stackSeviye);
      metinYaz(0, 0, tampon);
      break;
    }
  }
}

// =====================================================================
//  15c) ANA MENÜ (3 kez dokunus ile acilir)
// =====================================================================

void menuCiz() {
  if (!oledHazir) return;
  ekran.clearDisplay();

  // --- UST SATIR: uzun basma ilerleme cubugu ---
  // Basili tutuldukca cubuk 0'dan 128 piksele dolar.
  // Cubuk tamamen dolunca (ZAMAN_UZUN_BASMA = 600 ms) secim onaylanir.
  int oran = 0;
  if (butonBasiliyor && !uzunBasmaIslendi) {
    unsigned long gecen = millis() - butonBasilmaZamani;
    if (gecen > ZAMAN_UZUN_BASMA) gecen = ZAMAN_UZUN_BASMA;
    oran = (int)((gecen * 128) / ZAMAN_UZUN_BASMA);
  }
  ekran.drawRect(0, 0, 128, 5, SSD1306_WHITE);     // cerceve
  if (oran > 0) ekran.fillRect(1, 1, oran > 126 ? 126 : oran, 3, SSD1306_WHITE);

  // --- BASLIK ---
  metinYaz(0, 7, "ANA MENU");
  // sag ustte saat (cubuk alaninin altinda)
  {
    char st[12];
    saatMetni(st, sizeof(st), false);
    metinYaz(128 - (int)strlen(st) * 6, 7, st);
  }
  ekran.drawFastHLine(0, 16, 128, SSD1306_WHITE);

  // --- 5 secenek (Pet / Oyunlar / Sozler / Dil Cikar / Cikis) ---
  for (int i = 0; i < MENU_OGE_SAYISI; i++) {
    int y = 18 + i * 8;
    if (i == menuSecim) {
      ekran.fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
      ekran.setTextColor(SSD1306_BLACK);
      ekran.setTextSize(1);
      ekran.setCursor(3, y);
      ekran.print("> ");
      ekran.print(MENU_OGELERI[i]);
      ekran.setTextColor(SSD1306_WHITE);
    } else {
      metinYaz(14, y, MENU_OGELERI[i]);
    }
  }

  // --- ALT IPUCU: kalan sure ---
  int kalan = (int)((ZAMAN_MENU_OTOMATIK_CIKIS -
                     (millis() - sonDokunusZamani)) / 1000);
  if (kalan < 0) kalan = 0;
  char alt[32];
  snprintf(alt, sizeof(alt), "dokun:sec bas:onay %ds", kalan);
  metinYaz(0, 56, alt);

  ekran.display();
}

void menuAc() {
  sistemModu = MOD_MENU;
  sevgiModu = false;
  tepkiAktif = false;
  menuSecim = 0;
  menuGirisZamani = millis();
  sonDokunusZamani = millis();
  dokunusSayaci = 0;
  uzunBasmaIslendi = true;    // menuye giris dokunusu onay sayilmasin
  sesSelamlama();
  menuCiz();                  // ekrani hemen guncelle
}

void menuSecimiUygula() {
  if (menuSecim == 0) {
    // "Pet Modu" secildi -> pet ekranina don
    sistemModu = MOD_PET;
    aktifOyun = OYUN_YOK;
    otonomAktif = false;
    tepkiAktif = false;
    sonDokunmaZamani = millis();
  } else if (menuSecim == 1) {
    // OYUN MODU: once oyun listesi acilir, oyun otomatik baslamaz
    sistemModu = MOD_OYUNLAR;
    aktifOyun = OYUN_YOK;
    oyunMenuSecim = 0;
  } else if (menuSecim == 2) {
    sistemModu = MOD_SOZLER;
    sozKaydirmaZamani = millis();
  } else if (menuSecim == 3) {
    dilBaslat();                       // DIL CIKARMA animasyonu
  } else {
    // menuSecim == 0 (Pet Modu) veya 3 (Cikis) -> pet ekranina don
    sistemModu = MOD_PET;
    aktifOyun = OYUN_YOK;
    otonomAktif = false;
    tepkiAktif = false;
    sonDokunmaZamani = millis();
  }
  sonDokunusZamani = millis();
  sesMenuSecim();
  if (sistemModu == MOD_MENU) menuCiz();   // menude kaldiysak ekrani tazele
}

// =====================================================================
//  15b) DİL ÇIKARMA ANİMASYONU (MOD_DIL)
// ---------------------------------------------------------------------
//  Döngü:  dil ağızdan ÇIKAR -> geri GİRER -> çıkar -> SAĞA/SOLA gezdirir
//          -> geri girer -> baştan.
//  Her aşama kendi süresiyle non-blocking ilerler. Dokunma: sonraki aşama,
//  uzun basma: Pet moduna dön.
// =====================================================================

DilAsamasi    dilAsama = DIL_CIK;
unsigned long dilBaslangic = 0;
int           dilYon = 1;            // gezdirmede yön (+1 sağ, -1 sol)
int           dilX = 0;              // dilin yatay ofseti (-8..+8)

// Aşama süreleri (ms) - animasyonu yavaşlatmak için buradan artır
const unsigned long DIL_SURE_CIK    = 1100;
const unsigned long DIL_SURE_ICERI  = 900;
const unsigned long DIL_SURE_GEZDIR = 2600;
const unsigned long DIL_SURE_BEKLE  = 600;

void dilAsamaBaslat(DilAsamasi a) {
  dilAsama = a;
  dilBaslangic = millis();
  if (a == DIL_CIK) dilX = 0;
}

void dilBaslat() {
  sistemModu = MOD_DIL;
  aktifOyun = OYUN_YOK;
  otonomAktif = false;
  tepkiAktif = false;
  sevgiModu = false;
  sonDokunmaZamani = millis();
  dilAsamaBaslat(DIL_CIK);
  sesChirp();
}

// Dil çizimi: uzunluk 0..1 arası (0 = ağız içinde, 1 = tamamen dışarıda)
void dilCiz(float uzunluk, int kaydir) {
  int agizY = 44;
  int boy = (int)(uzunluk * 16.0f);          // 0..16 piksel dil boyu
  if (boy < 1) return;

  int x = 64 + kaydir;
  // dil gövdesi (aşağı doğru uzayan yuvarlak uç)
  ekran.fillRect(x - 4, agizY, 8, boy, SSD1306_WHITE);
  if (boy > 4) ekran.fillCircle(x, agizY + boy, 4, SSD1306_WHITE);
  // dil ortası çizgisi (dilin ikiye ayrıldığı hissi)
  if (boy > 6) ekran.drawFastVLine(x, agizY + 2, boy - 2, SSD1306_BLACK);
}

void dilEkraniCiz() {
  if (!oledHazir) return;
  ekran.clearDisplay();
  unsigned long gecen = millis() - dilBaslangic;

  // --- Ağız (dil çıkarken açılır) ---
  int agizAciklik = 6;      // ağız yüksekliği (piksel)
  float uzunluk = 0.0f;
  int kaydir = 0;

  switch (dilAsama) {
    case DIL_CIK: {
      float t = (float)gecen / (float)DIL_SURE_CIK;
      if (t > 1.0f) t = 1.0f;
      uzunluk = t;
      agizAciklik = 6 + (int)(t * 6);
      break;
    }
    case DIL_ICERI: {
      float t = (float)gecen / (float)DIL_SURE_ICERI;
      if (t > 1.0f) t = 1.0f;
      uzunluk = 1.0f - t;
      agizAciklik = 12 - (int)(t * 6);
      break;
    }
    case DIL_GEZDIR: {
      uzunluk = 1.0f;
      agizAciklik = 12;
      // sağ-sola yumuşak gezinme
      float t = (float)gecen / 400.0f;
      kaydir = (int)(sinf(t) * 8.0f);
      dilX = kaydir;
      break;
    }
    case DIL_BEKLE: {
      uzunluk = 0.0f;
      agizAciklik = 4;
      break;
    }
  }

  // --- Gözler: neşeli kısık (dil çıkarırken) ---
  if (dilAsama == DIL_GEZDIR) {
    // dilin yönüne bakar
    gozAcik(GOZ_SOL_X + kaydir / 3, GOZ_Y);
    gozAcik(GOZ_SAG_X + kaydir / 3, GOZ_Y);
  } else {
    kalinYay(GOZ_SOL_X, GOZ_Y, 9, 200, 340);
    kalinYay(GOZ_SAG_X, GOZ_Y, 9, 200, 340);
  }

  // --- Ağız boşluğu ---
  ekran.fillRect(64 - 14, 44, 28, agizAciklik, SSD1306_WHITE);
  ekran.fillRect(64 - 12, 45, 24, agizAciklik - 2, SSD1306_BLACK);

  // --- Dil ---
  dilCiz(uzunluk, kaydir);

  // --- Yanaklar ---
  ekran.fillCircle(24, 42, 4, SSD1306_WHITE);
  ekran.fillCircle(104, 42, 4, SSD1306_WHITE);

  // --- Aşama yazısı (üstte) + saat (sağ üstte) ---
  saatKoseCiz();
  if (dilAsama == DIL_GEZDIR) metinYaz(0, 0, "DIL GEZIYOR");
  else if (dilAsama == DIL_CIK) metinYaz(0, 0, "DIL CIKTI");
  else if (dilAsama == DIL_ICERI) metinYaz(0, 0, "DIL GIRDI");

  ekran.display();
}

// Aşama ilerletme (her karede loop'tan çağrılır)
void dilGuncelle() {
  unsigned long gecen = millis() - dilBaslangic;

  switch (dilAsama) {
    case DIL_CIK:
      if (gecen >= DIL_SURE_CIK) dilAsamaBaslat(DIL_GEZDIR);
      break;
    case DIL_GEZDIR:
      if (gecen >= DIL_SURE_GEZDIR) dilAsamaBaslat(DIL_ICERI);
      break;
    case DIL_ICERI:
      if (gecen >= DIL_SURE_ICERI) dilAsamaBaslat(DIL_BEKLE);
      break;
    case DIL_BEKLE:
      if (gecen >= DIL_SURE_BEKLE) {
        dilAsamaBaslat(DIL_CIK);
        sesChirp();                 // her turda kisa civilti
      }
      break;
  }
}

// =====================================================================
//  15d) OYUN MENÜSÜ (oyun moduna girince acilir)
// ---------------------------------------------------------------------
//  Dokunus: sonraki oyun | Uzun basma: oyunu baslat
//  Son ogede (GERI) uzun basma: ana menüye doner
// =====================================================================
const int OYUN_MENU_SAYISI = 7;          // 6 oyun + Geri
const char* OYUN_MENU_ADLARI[OYUN_MENU_SAYISI] = {
  "Dino Kosu", "Flappy Kus", "Yilan", "Pong", "Breakout", "Stacker", "Geri"
};
const OyunTuru OYUN_MENU_TURLERI[OYUN_MENU_SAYISI] = {
  OYUN_DINO, OYUN_FLAPPY, OYUN_YILAN, OYUN_PONG, OYUN_BREAKOUT, OYUN_STACKER, OYUN_YOK
};

void oyunMenuCiz() {
  if (!oledHazir) return;
  ekran.clearDisplay();

  // ust ilerleme cubugu (uzun basma gostergesi)
  int oran = 0;
  if (butonBasiliyor && !uzunBasmaIslendi) {
    unsigned long gecen = millis() - butonBasilmaZamani;
    if (gecen > ZAMAN_UZUN_BASMA) gecen = ZAMAN_UZUN_BASMA;
    oran = (int)((gecen * 128) / ZAMAN_UZUN_BASMA);
  }
  ekran.drawRect(0, 0, 128, 5, SSD1306_WHITE);
  if (oran > 0) ekran.fillRect(1, 1, oran > 126 ? 126 : oran, 3, SSD1306_WHITE);

  metinYaz(0, 7, "OYUNLAR");
  char st[12];
  saatMetni(st, sizeof(st), false);
  metinYaz(128 - (int)strlen(st) * 6, 7, st);
  ekran.drawFastHLine(0, 16, 128, SSD1306_WHITE);

  // 7 oge dikey liste, secili olan dolu kutu
  for (int i = 0; i < OYUN_MENU_SAYISI; i++) {
    int y = 18 + i * 7;
    if (i == oyunMenuSecim) {
      ekran.fillRect(0, y - 1, 128, 8, SSD1306_WHITE);
      ekran.setTextColor(SSD1306_BLACK);
      ekran.setTextSize(1);
      ekran.setCursor(3, y);
      ekran.print("> ");
      ekran.print(OYUN_MENU_ADLARI[i]);
      ekran.setTextColor(SSD1306_WHITE);
    } else {
      metinYaz(14, y, OYUN_MENU_ADLARI[i]);
    }
  }
  ekran.display();
}

void oyunMenuSecimiUygula() {
  OyunTuru tur = OYUN_MENU_TURLERI[oyunMenuSecim];
  if (tur == OYUN_YOK) {
    // Geri -> ana menu
    menuAc();
    return;
  }
  oyunBaslat(tur);       // secili oyunu baslat
}

// =====================================================================
//  16) BUTON OLAYLARI
// =====================================================================
// Son gosterilen tepkiler (ayni animasyon ust uste gelmesin)
uint32_t tepkiGecmis = 0;          // bit maskesi (son 32 tepki)
int      sonTepkiIndex = -1;
int      ardisikAyni = 0;

void tepkiBaslat() {
  int secim = -1;

  // En az 12 farkli tepki gormeden ayni tepkiyi tekrar secme
  for (int deneme = 0; deneme < 20; deneme++) {
    int aday = random(0, TEPKI_SAYISI);
    if (aday == sonTepkiIndex) continue;          // hemen tekrar yok
    if ((tepkiGecmis & (1UL << (aday % 32))) && deneme < 15) continue;
    secim = aday;
    break;
  }
  if (secim < 0) secim = random(0, TEPKI_SAYISI);

  sonTepkiIndex = secim;
  tepkiGecmis |= (1UL << (secim % 32));
  // 16 farkli tepkiden sonra maskeyi yavas yavas temizle
  if (++ardisikAyni > 16) {
    ardisikAyni = 0;
    tepkiGecmis = 0;
  }

  TepkiTanimi t;
  memcpy_P(&t, &TEPKILER[secim], sizeof(TepkiTanimi));
  aktifTepki = t.tur;
  tepkiSure = t.sure;
  tepkiBaslangic = millis();
  tepkiAktif = true;
  sesTepkiTuru(t.sesTuru);
}

void sozIlerlet() {
  aktifSoz = (aktifSoz + 1) % SOZ_SAYISI;
  sozKaydirmaZamani = millis();
  sesMenuSecim();
}

void dokunusIsle() {
  sonDokunmaZamani = millis();

  // Otonom moddan cik
  if (otonomAktif) {
    otonomAktif = false;
    otonomAktivite = AKT_YOK;
    sesSelamlama();
    tepkiBaslat();
    return;
  }

  switch (sistemModu) {
    case MOD_OYUNLAR:
      if (aktifOyun == OYUN_YOK) return;   // oyun menusunde input yok
      if (oyun.bitti) { oyunSifirla(); oyun.basladi = true; sesMenuSecim(); return; }
      switch (aktifOyun) {
        case OYUN_DINO:
          if (dinoY >= dinoZemin) { dinoHiz = -8.0f; sesZiplama(); }
          break;
        case OYUN_FLAPPY:
          kusHiz = -4.2f; sesZiplama();
          break;
        case OYUN_YILAN:
          yilanYon = (yilanYon + 1) % 4; sesMenuSecim();
          break;
        case OYUN_PONG:
          pongRaketYon = -pongRaketYon; sesMenuSecim();
          break;
        case OYUN_BREAKOUT:
          raketYon = -raketYon; sesMenuSecim();
          break;
        case OYUN_STACKER: {
          if (stackSeviye == 0) {
            stackSeviye = 1;
            stackHareketX = 0;
            sesMenuSecim();
            break;
          }
          int aktif = stackSeviye;
          int ust = aktif - 1;
          int sol = stackHareketX > stackBlokX[ust] ? stackHareketX : stackBlokX[ust];
          int sagA = stackHareketX + stackGenislik[aktif];
          int sagB = stackBlokX[ust] + stackGenislik[ust];
          int sag = sagA < sagB ? sagA : sagB;
          int yeniGenislik = sag - sol;
          if (yeniGenislik <= 2) {
            oyun.bitti = true; sesOyunBitti();
          } else {
            stackBlokX[aktif] = sol;
            stackGenislik[aktif] = yeniGenislik;
            oyun.puan = aktif;
            sesPuan();
            if (stackSeviye < STACK_MAX - 1) {
              stackSeviye++;
              stackGenislik[stackSeviye] = yeniGenislik;
              stackHareketX = 0;
              stackHareketYon = 1;
            } else {
              oyun.bitti = true; sesSelamlama();
            }
          }
          break;
        }
      }
      break;

    case MOD_SOZLER:
      sozIlerlet();
      break;

    case MOD_PET:
    default:
      tepkiBaslat();
      break;
  }
}

void uzunBasmaIsle() {
  sonDokunmaZamani = millis();
  dokunusSayaci = 0;              // uzun basma dokunus sayacini sifirlar

  if (sistemModu == MOD_MENU) {
    // menude uzun basma = secimi onayla
    menuSecimiUygula();
    return;
  }
  if (sistemModu == MOD_OYUNLAR) {
    // oyundan cik -> oyun listesine don (tekrar basinca ana menuye)
    if (aktifOyun != OYUN_YOK) {
      aktifOyun = OYUN_YOK;
      oyunMenuSecim = 0;
      oyunMenuCiz();
    } else {
      menuAc();
    }
    sesMenuSecim();
    return;
  }
  if (sistemModu == MOD_SOZLER || sistemModu == MOD_DIL) {
    sistemModu = MOD_PET;
    sesMenuSecim();
    return;
  }
  sevgiBaslat();
}

void butonGuncelle() {
  bool durum = digitalRead(PIN_DOKUNMATIK);
  unsigned long simdi = millis();

  if (durum && !butonOncekiDurum) {           // basildi
    butonBasilmaZamani = simdi;
    butonBasiliyor = true;
    uzunBasmaIslendi = false;
  }

  // ================= 1) MENU EKRANI =================
  // Kisa dokunus : secenegi asagi kaydirir
  // Uzun basma   : cubuk dolunca (600 ms) secimi ONAYLAR
  if (sistemModu == MOD_MENU) {
    if (durum && butonBasiliyor) {
      if (!uzunBasmaIslendi && (simdi - butonBasilmaZamani) >= ZAMAN_UZUN_BASMA) {
        uzunBasmaIslendi = true;
        menuSecimiUygula();                    // ONAY
      }
    } else if (!durum && butonOncekiDurum) {
      butonBasiliyor = false;
      if (!uzunBasmaIslendi) {
        menuSecim = (menuSecim + 1) % MENU_OGE_SAYISI;   // asagi kaydir
        sonDokunusZamani = simdi;
        sesMenuSecim();
        menuCiz();                                       // aninda guncelle
      }
    }
    butonOncekiDurum = durum;
    return;
  }

  // ================= 2) OYUN MODU =================
  if (sistemModu == MOD_OYUNLAR) {
    dokunusSayaci = 0;

    // --- 2a) OYUN MENUSU (henuz oyun secilmedi) ---
    if (aktifOyun == OYUN_YOK) {
      if (durum && butonBasiliyor) {
        if (!uzunBasmaIslendi && (simdi - butonBasilmaZamani) >= ZAMAN_UZUN_BASMA) {
          uzunBasmaIslendi = true;
          oyunMenuSecimiUygula();            // secili oyunu baslat / geri
        }
      } else if (!durum && butonOncekiDurum) {
        butonBasiliyor = false;
        if (!uzunBasmaIslendi) {
          oyunMenuSecim = (oyunMenuSecim + 1) % OYUN_MENU_SAYISI;   // asagi kaydir
          sonDokunusZamani = simdi;
          sesMenuSecim();
          oyunMenuCiz();
        }
      }
      butonOncekiDurum = durum;
      return;
    }

    // --- 2b) OYUN OYNANIYOR ---
    // 3'lu dokunus sayaci CALISMAZ: her dokunus dogrudan oyuna gider.
    // Uzun basma = oyundan cik -> oyun listesine don.
    if (durum && butonBasiliyor) {
      if (!uzunBasmaIslendi && (simdi - butonBasilmaZamani) >= ZAMAN_UZUN_BASMA) {
        uzunBasmaIslendi = true;
        uzunBasmaIsle();                       // oyundan cik
      }
    } else if (!durum && butonOncekiDurum) {
      butonBasiliyor = false;
      if (!uzunBasmaIslendi) {
        sonDokunusZamani = simdi;
        dokunusIsle();                         // oyun inputu
      }
    }
    butonOncekiDurum = durum;
    return;
  }

  // ================= 2b) DİL ANİMASYONU =================
  // Kisa dokunus: sonraki asama (cik -> gezdir -> iceri -> bekle)
  // Uzun basma  : Pet moduna don
  if (sistemModu == MOD_DIL) {
    dokunusSayaci = 0;

    if (durum && butonBasiliyor) {
      if (!uzunBasmaIslendi && (simdi - butonBasilmaZamani) >= ZAMAN_UZUN_BASMA) {
        uzunBasmaIslendi = true;
        sistemModu = MOD_PET;
        sonDokunmaZamani = simdi;
        sesMenuSecim();
      }
    } else if (!durum && butonOncekiDurum) {
      butonBasiliyor = false;
      if (!uzunBasmaIslendi) {
        sonDokunusZamani = simdi;
        // sonraki asama
        if (dilAsama == DIL_CIK)         dilAsamaBaslat(DIL_GEZDIR);
        else if (dilAsama == DIL_GEZDIR) dilAsamaBaslat(DIL_ICERI);
        else if (dilAsama == DIL_ICERI)  dilAsamaBaslat(DIL_BEKLE);
        else                             dilAsamaBaslat(DIL_CIK);
        sesChirp();
      }
    }
    butonOncekiDurum = durum;
    return;
  }

  // ================= 3) SOZLER EKRANI =================
  // Kisa dokunus = sonraki soz, uzun basma = Pet'e don. 3'lu sayac yok.
  if (sistemModu == MOD_SOZLER) {
    dokunusSayaci = 0;

    if (durum && butonBasiliyor) {
      if (!uzunBasmaIslendi && (simdi - butonBasilmaZamani) >= ZAMAN_UZUN_BASMA) {
        uzunBasmaIslendi = true;
        uzunBasmaIsle();
      }
    } else if (!durum && butonOncekiDurum) {
      butonBasiliyor = false;
      if (!uzunBasmaIslendi) {
        sonDokunusZamani = simdi;
        dokunusIsle();                         // sonraki soz
      }
    }
    butonOncekiDurum = durum;
    return;
  }

  // ================= 4) PET MODU =================
  if (durum && butonBasiliyor) {
    if (!uzunBasmaIslendi && simdi - butonBasilmaZamani >= ZAMAN_UZUN_BASMA) {
      uzunBasmaIslendi = true;
      uzunBasmaIsle();                         // sevgi modu
    }
    // sevgi modunda mirildanma tekrari
    if (sevgiModu && uzunBasmaIslendi && simdi - sonMirildanma > 900) {
      sonMirildanma = simdi;
      sesMirildan();
      sevgiBaslangic = simdi;                  // sevgi suresini uzat
    }
  }

  if (!durum && butonOncekiDurum) {            // birakildi
    butonBasiliyor = false;
    if (!uzunBasmaIslendi) {
      // --- 3'lu dokunus sayaci (YALNIZCA PET MODUNDA) ---
      // 3 kez kisa dokunus -> ana menu acilir.
      if (dokunusSayaci > 0 && (simdi - sonDokunusZamani) > ZAMAN_DOKUNUS_ARASI) {
        dokunusSayaci = 0;                     // arada cok zaman gecti -> sifirla
      }
      sonDokunusZamani = simdi;
      dokunusSayaci++;

      if (dokunusSayaci >= MENU_ICIN_DOKUNUS) {
        dokunusSayaci = 0;
        menuAc();                              // 3 dokunus -> ANA MENU
      } else {
        dokunusIsle();                         // 1. ve 2. dokunus normal tepki
      }
    }
  }

  // Sayac zaman asimi (dokunulmadan gecen sure)
  if (!butonBasiliyor && dokunusSayaci > 0 &&
      (simdi - sonDokunusZamani) > ZAMAN_DOKUNUS_ARASI) {
    dokunusSayaci = 0;
  }

  butonOncekiDurum = durum;
}

// =====================================================================
//  17) OTONOM PET GÜNCELLEMESİ
// =====================================================================
void otonomGuncelle() {
  unsigned long simdi = millis();

  // 10 sn dokunulmazsa otonom baslar
  if (!otonomAktif && sistemModu == MOD_PET && !sevgiModu &&
      simdi - sonDokunmaZamani > ZAMAN_IDLE_BASLANGIC) {
    otonomAktif = true;
    otonomAktivite = (OtonomAktivite)random(1, AKT_SAYISI);
    otonomBaslangic = simdi;
    otonomSure = random(7000, 14000);   // her aktivite 7-14 sn
    otonomAdim = 0;
  }

  // Menude 1 dakika dokunulmazsa otomatik Pet moduna don
  if (sistemModu == MOD_MENU && !butonBasiliyor &&
      simdi - sonDokunusZamani > ZAMAN_MENU_OTOMATIK_CIKIS) {
    sistemModu = MOD_PET;
    dokunusSayaci = 0;
    sonDokunmaZamani = simdi;
  }

  if (!otonomAktif) return;

  otonomAdim++;

  if (simdi - otonomBaslangic > otonomSure) {
    otonomBaslangic = simdi;
    otonomSure = random(7000, 14000);   // her aktivite 7-14 sn
    otonomAdim = 0;
    // uyuma daha uzun surer
    if (otonomAktivite == AKT_UYUMA) otonomSure = random(14000, 24000);

    // Ayni aktiviteyi ust uste oynatma: son 3 aktiviteyi hatirla
    static OtonomAktivite sonAktiviteler[3] = { AKT_YOK, AKT_YOK, AKT_YOK };
    OtonomAktivite yeni = otonomAktivite;
    for (int deneme = 0; deneme < 20; deneme++) {
      OtonomAktivite aday = (OtonomAktivite)random(1, AKT_SAYISI);
      if (aday == otonomAktivite) continue;
      bool tekrar = false;
      for (int k = 0; k < 3; k++) if (sonAktiviteler[k] == aday) tekrar = true;
      if (tekrar && deneme < 15) continue;
      yeni = aday;
      break;
    }
    sonAktiviteler[2] = sonAktiviteler[1];
    sonAktiviteler[1] = sonAktiviteler[0];
    sonAktiviteler[0] = yeni;
    otonomAktivite = yeni;

    if (otonomAktivite == AKT_ESNEME || otonomAktivite == AKT_UYUMA) sesMirildan();
    else if (otonomAktivite == AKT_DANS || otonomAktivite == AKT_MUZIK) sesRobotikCivilti();
  }
}

// =====================================================================
//  18) ANA ÇİZİM
// =====================================================================
void ekranCiz() {
  if (!oledHazir) return;          // OLED bagli degilse cokme/reset olmasin
  ekran.clearDisplay();
  int f = (int)(millis() / 60);

  // --- Sevgi modu her seyin ustunde ---
  if (sevgiModu) {
    if (millis() - sevgiBaslangic > 2500 && !butonBasiliyor) sevgiModu = false;
    else { cizSevgi(f); ekran.display(); return; }
  }

  switch (sistemModu) {
    case MOD_MENU:
      menuCiz();
      return;               // menuCiz kendi display() cagrisini yapar

    case MOD_OYUNLAR:
      if (aktifOyun == OYUN_YOK) { oyunMenuCiz(); return; }
      oyunCiz();
      break;

    case MOD_SOZLER:
      sozEkraniCiz();
      break;

    case MOD_DIL:
      dilEkraniCiz();
      return;               // dilEkraniCiz kendi display() cagrisini yapar

    case MOD_MOTIVASYON:
      sozEkraniCiz();
      break;

    case MOD_PET:
    default:
      if (tepkiAktif) {
        if (millis() - tepkiBaslangic > tepkiSure) tepkiAktif = false;
        else { cizTepki(f); break; }
      }
      if (otonomAktif) cizOtonomAktivite();
      else cizIdleYuz(f);
      if (!saatAyarli) metinYaz(0, 0, "SAAT: WEB");   // telefondan senkron bekle
      else saatKoseCiz();            // sag ustte saat
      break;
  }

  ekran.display();
}

// =====================================================================
//  19) WEB ARAYÜZÜ
// =====================================================================
const char SAYFA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="tr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Quantifriends</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:"Segoe UI",Arial,sans-serif;background:linear-gradient(135deg,#0f172a,#312e81);color:#e2e8f0;min-height:100vh;padding:20px}
.wrap{max-width:460px;margin:0 auto}
h1{text-align:center;font-size:1.5rem;letter-spacing:3px;color:#7dd3fc;margin-bottom:4px}
.sub{text-align:center;font-size:.75rem;color:#64748b;margin-bottom:20px;letter-spacing:1px}
.card{background:rgba(255,255,255,.07);border:1px solid rgba(125,211,252,.2);border-radius:20px;padding:18px;margin-bottom:16px}
h2{font-size:.8rem;letter-spacing:2px;color:#94a3b8;margin-bottom:12px;text-transform:uppercase}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
button{background:rgba(125,211,252,.12);border:2px solid rgba(125,211,252,.35);color:#e0f2fe;padding:13px 8px;border-radius:14px;font-size:.8rem;font-weight:700;cursor:pointer;transition:.15s;width:100%}
button:hover{background:rgba(125,211,252,.3)}
button.aktif{background:#22d3ee;color:#082f49;border-color:#22d3ee}
.full{grid-column:1/-1}
.durum{text-align:center;font-size:.8rem;color:#7dd3fc;min-height:1.2em;margin-top:10px}
</style></head><body><div class="wrap">
<h1>Quantifriends</h1>
<div class="sub">ESP32-C3 SuperMini &bull; Wi-Fi: Quantifriends</div>

<div class="card">
  <h2>Mod Secimi</h2>
  <div class="grid">
    <button onclick="git('/mod?m=pet')">Pet Modu</button>
    <button onclick="git('/mod?m=oyun')">Oyunlar</button>
    <button class="full" onclick="git('/mod?m=soz')">Motivasyon Sozleri</button>
    <button class="full" onclick="git('/mod?m=menu')">Cihazda Ana Menu Ac</button>
  </div>
</div>

<div class="card">
  <h2>Oyunlar</h2>
  <div class="grid">
    <button onclick="git('/oyun?o=dino')">Dino Kosu</button>
    <button onclick="git('/oyun?o=flappy')">Flappy Kus</button>
    <button onclick="git('/oyun?o=yilan')">Yilan</button>
    <button onclick="git('/oyun?o=pong')">Pong</button>
    <button onclick="git('/oyun?o=breakout')">Breakout</button>
    <button onclick="git('/oyun?o=stacker')">Stacker</button>
  </div>
</div>

<div class="card">
  <h2>Sozler</h2>
  <div class="grid">
    <button onclick="git('/soz?islem=sonraki')">Sonraki Soz</button>
    <button onclick="git('/soz?islem=rastgele')">Rastgele Soz</button>
  </div>
</div>

<div class="durum" id="durum">Hazir</div>
<div class="durum" id="saatDurum">Saat senkronize ediliyor...</div>
</div>
<script>
// --- Saat otomatik senkronu ---
// Telefon sayfayi actiginda ve her 60 sn'de bir cihaza saati gonderir.
function saatGonder(){
  var t = Math.floor(Date.now()/1000);
  var d = -new Date().getTimezoneOffset()/60;   // UTC+3 -> 3
  fetch('/saat?t='+t+'&d='+d)
    .then(r=>r.text())
    .then(txt=>{ var el=document.getElementById('saatDurum'); if(el) el.textContent=txt; })
    .catch(()=>{});
}
saatGonder();
setInterval(saatGonder, 60000);

function git(u){
  fetch(u).then(r=>r.text()).then(t=>{
    document.getElementById('durum').textContent = t;
    document.querySelectorAll('button').forEach(b=>b.classList.remove('aktif'));
    if(event && event.target) event.target.classList.add('aktif');
  }).catch(e=>{document.getElementById('durum').textContent='Baglanti hatasi';});
}
</script></body></html>
)rawliteral";

void webAnaSayfa() {
  sunucu.send_P(200, "text/html", SAYFA_HTML);
}

void webModSec() {
  String m = sunucu.hasArg("m") ? sunucu.arg("m") : String("");
  if (m == "pet") {
    sistemModu = MOD_PET;
    aktifOyun = OYUN_YOK;
    otonomAktif = false;
    sonDokunmaZamani = millis();
    sunucu.send(200, "text/plain", "Pet modu aktif");
  } else if (m == "oyun") {
    sistemModu = MOD_OYUNLAR;
    sunucu.send(200, "text/plain", "Oyun modu aktif");
  } else if (m == "soz") {
    sistemModu = MOD_SOZLER;
    sozKaydirmaZamani = millis();
    sunucu.send(200, "text/plain", "Motivasyon sozleri aktif");
  } else if (m == "menu") {
    menuAc();
    sunucu.send(200, "text/plain", "Ana menu acildi");
    return;
  } else {
    sunucu.send(400, "text/plain", "Gecersiz mod");
  }
  sesMenuSecim();
}

void webOyunSec() {
  String o = sunucu.hasArg("o") ? sunucu.arg("o") : String("");
  OyunTuru tur = OYUN_YOK;
  if (o == "dino")          tur = OYUN_DINO;
  else if (o == "flappy")   tur = OYUN_FLAPPY;
  else if (o == "yilan")    tur = OYUN_YILAN;
  else if (o == "pong")     tur = OYUN_PONG;
  else if (o == "breakout") tur = OYUN_BREAKOUT;
  else if (o == "stacker")  tur = OYUN_STACKER;

  if (tur == OYUN_YOK) {
    sunucu.send(400, "text/plain", "Gecersiz oyun");
    return;
  }
  oyunBaslat(tur);
  char cevap[40];
  snprintf(cevap, sizeof(cevap), "%s basladi", OYUN_ADLARI[tur]);
  sunucu.send(200, "text/plain", cevap);
}

void webSoz() {
  String islem = sunucu.hasArg("islem") ? sunucu.arg("islem") : String("");
  if (islem == "rastgele") aktifSoz = random(0, SOZ_SAYISI);
  else aktifSoz = (aktifSoz + 1) % SOZ_SAYISI;
  sozKaydirmaZamani = millis();
  sistemModu = MOD_SOZLER;
  sesMenuSecim();
  sunucu.send(200, "text/plain", MOTIVASYON_SOZLERI[aktifSoz]);
}

// Telefondan saat senkronu: /saat?t=<unix_saniye>&d=<saat_dilimi>
void webSaat() {
  if (!sunucu.hasArg("t")) {
    // Parametresiz istek: sadece mevcut saati dondur
    char t[12];
    saatMetni(t, sizeof(t), true);
    sunucu.send(200, "text/plain", t);
    return;
  }
  long unixS = sunucu.arg("t").toInt();
  int  dilim = sunucu.hasArg("d") ? sunucu.arg("d").toInt() : 3;
  if (unixS < 1600000000L) {          // gecersiz zaman
    sunucu.send(400, "text/plain", "Gecersiz zaman");
    return;
  }
  saatAyarla(unixS, dilim);
  char t[12];
  saatMetni(t, sizeof(t), true);
  char cevap[32];
  snprintf(cevap, sizeof(cevap), "Saat ayarlandi: %s", t);
  sunucu.send(200, "text/plain", cevap);
}

void webBulunamadi() {
  sunucu.send(404, "text/plain", "Sayfa bulunamadi");
}

// ---- Wi-Fi / bellek saglik kontrolu (reset teshisi) ----
void wifiSaglikRaporu() {
  Serial.print(F("[DB] heap: ")); Serial.print(ESP.getFreeHeap());
  Serial.print(F("  minHeap: ")); Serial.print(ESP.getMinFreeHeap());
  Serial.print(F("  RSSI: ")); Serial.print(WiFi.RSSI());
  Serial.print(F("  istasyon: ")); Serial.println(WiFi.softAPgetStationNum());
}

// Reset sebebini okur (bir kez, setup sonunda)
void resetSebebiYazdir() {
  esp_reset_reason_t r = esp_reset_reason();
  Serial.print(F("[DB] Reset sebebi: "));
  switch (r) {
    case ESP_RST_POWERON:  Serial.println(F("POWERON (normal acilis)")); break;
    case ESP_RST_SW:       Serial.println(F("SOFTWARE (kod resetledi / panic)")); break;
    case ESP_RST_PANIC:    Serial.println(F("PANIC (crash!)")); break;
    case ESP_RST_INT_WDT:  Serial.println(F("INTERRUPT WATCHDOG")); break;
    case ESP_RST_TASK_WDT: Serial.println(F("TASK WATCHDOG")); break;
    case ESP_RST_WDT:      Serial.println(F("WATCHDOG")); break;
    case ESP_RST_BROWNOUT: Serial.println(F("BROWNOUT (guc yetmedi!)")); break;
    case ESP_RST_DEEPSLEEP:Serial.println(F("DEEPSLEEP uyanma")); break;
    case ESP_RST_EXT:      Serial.println(F("EXTERNAL pin")); break;
    default:               Serial.println((int)r); break;
  }
}

// =====================================================================
//  20) SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_DOKUNMATIK, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  // I2C 400 kHz -> hizli ekran (Zero-Lag UI)
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  oledHazir = ekran.begin(SSD1306_SWITCHCAPVCC, OLED_ADRES);
  if (!oledHazir) {
    Serial.println(F("OLED baslatilamadi! (SDA=8, SCL=9, adres 0x3C)"));
  } else {
    ekran.clearDisplay();
    ekran.display();
  }

  // Gercek donanim rastgeleligi (her acilista farkli sira)
  randomSeed(esp_random() ^ micros());

  // --- Wi-Fi oncesi geri sayim ---
  // Ag baglantisi kurulurken (DHCP/assoc) olusan anlik akim cekilmesi
  // zayif USB kablolarinda reset'e yol acar. 3 sn bekleyip ekranda gosteriyoruz.
  for (int s = 3; s > 0 && oledHazir; s--) {
    ekran.clearDisplay();
    metinYaz(26, 18, "Quantifriends", 2);
    char g[16];
    snprintf(g, sizeof(g), "Wi-Fi %d sn", s);
    metinYaz(24, 42, g);
    ekran.display();
    delay(1000);                    // yalnizca ACILISTA, loop icinde delay yok
  }

  // Wi-Fi AP
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);             // guc dalgalanmasini azaltir
  WiFi.softAP(AP_ADI, AP_SIFRE);
  Serial.print(F("AP: ")); Serial.println(AP_ADI);
  Serial.print(F("Sifre: ")); Serial.println(AP_SIFRE);
  Serial.print(F("IP: ")); Serial.println(WiFi.softAPIP());

  sunucu.on("/", webAnaSayfa);
  sunucu.on("/mod", webModSec);
  sunucu.on("/oyun", webOyunSec);
  sunucu.on("/soz", webSoz);
  sunucu.on("/saat", webSaat);
  sunucu.onNotFound(webBulunamadi);
  sunucu.begin();

  resetSebebiYazdir();
  wifiSaglikRaporu();

  sonDokunmaZamani = millis();
  sesSelamlama();

  // Acilis ekrani
  if (oledHazir) {
    ekran.clearDisplay();
    metinYaz(20, 12, "Quantifriends", 2);
    metinYaz(10, 34, "Wi-Fi: Quantifriends");
    metinYaz(10, 44, "IP: 192.168.4.1");
    metinYaz(4, 54, "3 dokunus: menu");
    ekran.display();
  }
}

// =====================================================================
//  21) LOOP  (tamamen non-blocking)
// =====================================================================
unsigned long sonKareZamani = 0;
unsigned long sonMenuCizimZamani = 0;

void loop() {
  sunucu.handleClient();
  butonGuncelle();
  sesGuncelle();
  otonomGuncelle();

  unsigned long simdi = millis();
  oyunGuncelle(simdi);
  if (sistemModu == MOD_DIL) dilGuncelle();

  // ~60 FPS UI
  if (simdi - sonKareZamani >= ZAMAN_KARE_MS) {
    sonKareZamani = simdi;
    ekranCiz();
  }

  // Her 5 sn'de bir Wi-Fi / bellek raporu (Seri Monitor 115200)
  static unsigned long sonRapor = 0;
  if (simdi - sonRapor > 5000) {
    sonRapor = simdi;
    wifiSaglikRaporu();
  }

  // Menu ekraninda buton basili tutuluyorsa cubugu akici ciz (30 ms'de bir)
  if (sistemModu == MOD_MENU && butonBasiliyor &&
      simdi - sonMenuCizimZamani >= 30) {
    sonMenuCizimZamani = simdi;
    menuCiz();
  }
}
