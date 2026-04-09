# ESP32-S3 Media Hub v9.1

Media Hub adalah proyek berbasis ESP32-S3 yang menggabungkan fitur kecerdasan buatan (AI), hiburan, dan utilitas dalam satu perangkat portabel. Proyek ini menggunakan layar TFT 320x170 dan dilengkapi dengan berbagai fitur online dan offline.

## 🚀 Fitur Utama

### 🤖 Kecerdasan Buatan (AI)
- **AI Chat**: Integrasi dengan Google Gemini dan Groq API untuk asisten pintar portabel.
- **Dukungan Keyboard**: Input teks langsung menggunakan keyboard on-screen.

### 🎮 Game Mini (9 Games)
- **Tic-Tac-Toe**: Lawan AI yang cerdas.
- **Tank Battle**: Perang tank klasik dengan sistem pathfinding BFS.
- **Flappy Bird**: Game arcade klasik.
- **Minesweeper**: Game logika dengan berbagai ukuran grid.
- **Pacman**: Labirin klasik dengan hantu AI.
- **Doodle Jump**: Game melompat tanpa henti.
- **Memory Match**: Melatih ingatan dengan kartu.
- **Simon Says**: Uji ingatan urutan warna.
- **Sudoku**: Game teka-teki angka.

### 📺 Media & Informasi Online
- **Video Player**: Memutar file `.mjpeg` dari kartu SD dengan dukungan DMA.
- **RSS News Reader**: Membaca berita terkini dari berbagai sumber RSS.
- **Hacker News**: Menampilkan berita teratas dan komentar dari Algolia HN API.
- **NASA APOD**: Menampilkan "Astronomy Picture of the Day" langsung dari NASA.
- **ISS Tracker**: Pelacakan posisi International Space Station secara real-time.
- **Informasi Lain**: Quote Stoic, Fakta Angka (NumbersAPI), Chuck Norris Facts, Dad Jokes, dan Ide Aktivitas (Random Activity API).

### 🛠️ Sistem & Utilitas
- **File Manager**: Menjelajahi dan mengelola file di kartu SD.
- **WiFi Manager**: Scan dan simpan kredensial WiFi ke NVS (Preferences) secara permanen.
- **Power Menu**: Opsi Restart dan Deep Sleep.
- **Auto Brightness**: Penyesuaian kecerahan layar melalui sensor LDR.
- **Camera Stream**: Penerima stream MJPEG (IP Cam).

## 🛠️ Spesifikasi Perangkat Keras

- **Controller**: ESP32-S3 N16R8 (16MB Flash / 8MB OPI PSRAM).
- **Display**: ST7789 LCD 320x170 (Landscape).
- **LED**: 1x WS2812B NeoPixel (Status & Feedback).
- **Storage**: Slot Micro SD (HSPI Mode).

## 📌 Pin Mapping

| Komponen | Pin |
| :--- | :--- |
| **TFT CS** | 10 |
| **TFT SCLK** | 12 |
| **TFT MOSI** | 11 |
| **TFT DC** | 9 |
| **TFT RST** | 13 |
| **TFT BL** | 14 |
| **SD CS** | 3 |
| **SD SCK** | 18 |
| **SD MISO** | 8 |
| **SD MOSI** | 17 |
| **BTN SEL** | 0 |
| **BTN UP** | 41 |
| **BTN DW** | 40 |
| **BTN R** | 38 |
| **BTN L** | 39 |
| **NEOPIXEL** | 48 |
| **LDR** | 1 |

## 📚 Library yang Digunakan

- **LovyanGFX**: Driver grafis performa tinggi untuk ESP32.
- **ArduinoJson**: Parsing data API berbasis JSON.
- **Adafruit NeoPixel**: Kontrol LED RGB.
- **JPEGDEC**: Dekompresi gambar JPEG/MJPEG dengan cepat.
- **Preferences**: Penyimpanan data non-volatile (NVS).

---
*Dibuat dengan ❤️ untuk komunitas ESP32.*
