# 🤖 Quantifriends

ESP32-C3 SuperMini tabanlı, kendi Wi-Fi ağını (AP) yayan, dahili web arayüzüne sahip ve tek bir dokunmatik sensörle kontrol edilebilen **Sanal Masaüstü Evcil Hayvanı ve Retro Oyun Konsolu**.

Bu proje, engellemesiz (non-blocking) C++ mimarisi ve yüksek hızlı I2C (400kHz) iletişimi sayesinde OLED ekran üzerinde gecikmesiz (Zero-Lag) ve akıcı bir deneyim sunar.

---

## ✨ Öne Çıkan Özellikler

### 🎭 71 Farklı Animasyon (Otonom & İnteraktif)
Sıradan bir ekrandan fazlası. Sistem boşta kaldığında kendi kendine takılan, dokunulduğunda ise tepki veren devasa bir animasyon kütüphanesi[cite: 3]:
* **25 Temel Durum:** Mutlu, Hüzünlü, Öfkeli, Uykulu, Havalı, Şüpheci, Bozulmuş (Glitch), Şarj Oluyor ve daha fazlası[cite: 3].
* **45 Dokunuş Tepkisi:** Zıplama, utanma, kalp atışı, müzik dinleme, dans etme, hapşırma, poflama gibi anlık reaksiyonlar[cite: 3].
* **Özel "Dil Çıkarma" Animasyonu:** Aşamalı olarak dışarı çıkan, sağa sola gezinen ve geri giren detaylı animasyon dizisi[cite: 3].
* **Sevgi (Cuddle) Modu:** Dokunmatik sensöre uzun basıldığında ekranı kaplayan kalpler ve mırıldanma sesleri[cite: 5].

### 🕹️ 6 Klasik Retro Oyun (Tek Buton Mekaniği)
Sistemde sadece tek bir TTP223 sensörü olmasına rağmen tüm oyunlar zekice tasarlanmış "tek tuş" mekaniği ile oynanır[cite: 4]:
* **🦖 Dino Koşu:** Dokunarak zıpla, kaktüslere çarpma[cite: 4].
* **🐦 Flappy Kuş:** Dokunarak uç, boruların arasından geç[cite: 4].
* **🐍 Yılan:** Her dokunuşta yılanı saat yönünde 90° döndür[cite: 4].
* **🏓 Pong:** Dokunarak raketin hareket yönünü tersine çevir[cite: 4].
* **🧱 Breakout:** Dokunarak raketi yönlendir ve tuğlaları kır[cite: 4].
* **📦 Stacker:** Dokunarak bloğu bırak, en yüksek kuleyi diz[cite: 4].

### 📱 Wi-Fi Web Kontrol Paneli & Ekstralar
* Cihaz `Quantifriends` isimli şifreli bir Wi-Fi ağı yayar[cite: 5].
* Tarayıcıdan IP adresine (`192.168.4.1`) girerek **Cam Efektli (Glassmorphism)** şık kontrol paneline ulaşılır[cite: 5].
* Web paneli üzerinden modlar (Pet, Oyun, Motivasyon) arası geçiş yapılabilir[cite: 5].
* **Saat Senkronizasyonu:** Web paneline girildiğinde telefonun saati arka planda cihaza iletilir ve ekrana yansıtılır[cite: 5].
* **Motivasyon Modu:** Günün ilham verici sözleri OLED ekranda okunabilir[cite: 5].

---

## 🛠️ Donanım Gereksinimleri ve Bağlantılar

Devrede karmaşık kablolamalara yer yoktur, sadece 4 ana bileşen kullanılmıştır:

| Bileşen | ESP32-C3 Pini | Görevi / Not |
| :--- | :--- | :--- |
| **0.96" OLED Ekran (SSD1306)** | `SDA = GPIO 8` <br> `SCL = GPIO 9` | *ESP32-C3 donanımsal I2C pinleridir.* |
| **TTP223 Dokunmatik Sensör** | `GPIO 4` | *Sistemdeki tek fiziksel kontrol butonu (Dijital Giriş).* |
| **Pasif Buzzer** | `GPIO 3` | *PWM kullanılarak R2-D2 sesleri ve 8-bit retro oyun melodileri üretir.* |

---

## ⚙️ Kurulum ve Yükleme (Arduino IDE)

### 1. Gerekli Kütüphaneler
Arduino IDE "Library Manager" üzerinden aşağıdaki kütüphaneleri kurun:
* `Adafruit SSD1306`
* `Adafruit GFX Library`
* `Adafruit BusIO`

### 2. Kart Ayarları (Kritik)
Kodu yüklemeden önce Arduino IDE > Araçlar (Tools) menüsünü aşağıdaki gibi ayarlayın:
* **Board:** `ESP32C3 Dev Module` (ESP32 Core v2.0.17+ veya 3.x)
* **USB CDC On Boot:** `Enabled` *(Etkinleştirilmezse seri monitör çalışmaz)*
* **Flash Size:** `4MB`
* **Partition Scheme:** `Default`

---

## 🎮 Cihaz Üzerinden Kontrol Rehberi
* **Kısa Dokunma (Idle):** 45 farklı animasyondan birini rastgele tetikler[cite: 3, 5].
* **Kısa Dokunma (Oyun):** Oyun içi eylemi gerçekleştirir (Zıpla, dön vb.)[cite: 4, 5].
* **Uzun Basılı Tutma:** Sevgi (Cuddle) modunu açar veya menülerde onayı tetikler[cite: 5].
* **Hızlı 3 Dokunuş:** Ana menüyü açar. Menüde kısa dokunuşlar ile aşağı inilir, uzun basılarak seçim onaylanır[cite: 5].