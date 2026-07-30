SoundBoardFasaFiso
==================

Windows 10/11 için kurulumsuz, portable soundboard, mikrofon işlemcisi,
medya aktarıcısı ve WAV editörü.

SÜRÜM / VERSION
---------------
2.2.1

GENEL BAKIŞ
-----------
SoundBoardFasaFiso; global hotkey tabanlı ses oynatma, bağımsız ana ve
monitor çıkışları, fiziksel mikrofon miksleme, RNNoise gürültü engelleme,
WebRTC AEC3 yankı giderme, düşük gecikmeli Voice Effects / Voice Changer,
local/URL medya aktarımı ve gömülü WAV editörünü tek bir native Windows
uygulamasında birleştirir.

Kurulum, veritabanı, arka plan servisi, .NET veya harici GUI framework'ü
gerekmez. Config, sesler, loglar ve içe aktarılan dosyalar EXE'nin yanındaki
portable klasörde tutulur.

DİL / LANGUAGE
--------------
language=tr : Türkçe
language=en : English

TEMA / THEME
------------
theme=dark  : Koyu tema
theme=light : Açık tema

Tema panelin sağ üstündeki switch ile anında önizlenir. Kalıcı olması için
Kaydet ve uygula düğmesine basılır.

HIZLI KULLANIM
--------------
1. ZIP'i resmi GitHub Release sayfasından indir.
2. ZIP'in SHA-256 değerini yanında yayınlanan .sha256 dosyasıyla karşılaştır.
3. Arşivin tamamını yazma izni olan bir klasöre çıkar.
4. SoundBoardFasaFiso.exe dosyasını çalıştır.
5. Ana, monitor ve isteğe bağlı mikrofon cihazlarını seç.
6. Hotkey'ler sekmesinde ses atamalarını oluştur.
7. Kaydet ve uygula'ya bas.

Dosyayı yalnızca sounds klasörüne kopyalamak otomatik hotkey atamaz.

Hiç etkin ses ataması olmasa veya eski atamalardaki dosyalar kayıp olsa bile
panel açık kalır; config.txt dosyasını elle düzeltmeden yeni atama eklenebilir.

KLASÖR YAPISI
-------------
SoundBoardFasaFiso.exe
config.txt
README.txt
LICENSE
THIRD_PARTY_NOTICES.txt
media-tools\        Doğrulanmış yt-dlp, Deno, FFmpeg, ffprobe ve lisansları
sounds\             Ses arşivi ve örnek sesler
logs\               İlk çalıştırmada otomatik oluşturulur

Program config, log, import ve düzenlenen ses dosyalarını portable klasör
içinde tutar.

DESTEKLENEN OYNATMA FORMATLARI
------------------------------
WAV, MP3 ve FLAC

LOCAL VE URL MEDYA AKTARIMI
---------------------------
Kontrol panelinden:

- Bilgisayardaki bir medya dosyası içe aktarılabilir.
- Desteklenen bir medya URL'si içe aktarılabilir.
- Dönüştürme arka planda çalışır; ana pencere kullanılabilir kalır.
- Sonuç sounds klasörüne eklenebilir.

Resmi portable paket şu araçları kendi içinde taşır:

- yt-dlp
- Deno
- FFmpeg
- ffprobe

Araç sürümleri sabitlenir ve SHA-256 ile doğrulanır. Sistem genelinde
FFmpeg, yt-dlp, Deno veya Python kurulması gerekmez.

ÖRNEK ATAMALAR
--------------
F1=example.wav
F2=example.mp3|volume=0.80|mode=restart
F3=example.flac|volume=1.00|mode=overlap

SES MODLARI
-----------
restart : Her basışta sesi baştan çalar.
overlap : Aynı sesi üst üste bindirir; en fazla 8 voice kullanır.
toggle  : Bir basışta çalar, tekrar basışta durdurur.
loop    : Döngüye alır, tekrar basışta durdurur.

Varsayılan değerler:

volume=1.00
mode=restart

CİHAZ AYARLARI
--------------
output=default
output_volume=1.00
monitor=none
monitor_volume=0.30

"default" Windows varsayılan cihazını kullanır.
"none" monitor çıkışını kapatır.
Cihaz adlarında tam isim yerine ayırt edici bir bölüm yazılabilir.

VB-CABLE kullanılıyorsa ana çıkışta genellikle "CABLE Input" seçilir.
Monitor çıkışı kulaklığa verilerek ses gecikmesiz biçimde yerelden dinlenir.
VB-CABLE uygulamayla birlikte gelmez ve zorunlu değildir.

MİKROFON VE FİLTRELER
---------------------
microphone_enabled=false
microphone=default
microphone_volume=1.00
microphone_to_output=true
microphone_to_monitor=false

Mikrofon etkinleştirildiğinde fiziksel mikrofon ana çıkışa, monitöre veya
ikisine birden gerçek zamanlı eklenebilir.

Desteklenen mikrofon işleme sistemi:

- Mikrofon cihazı seçimi ve gain
- Ana çıkışa yönlendirme
- Monitor çıkışına yönlendirme
- RNNoise gürültü engelleme
- WebRTC AEC3 akustik yankı giderme
- Canlı mikrofon ve çıkış seviye göstergeleri
- Hatalı yeni ayarda önceki çalışan sisteme geri dönme

VOICE EFFECTS / VOICE CHANGER
-----------------------------
Voice Effects, crosstalk cancellation, AEC3, high-pass ve RNNoise temizliğinden
sonra; AGC, compressor ve limiter'dan önce çalışır. Output gain limiter'dan
önce uygulanır.

Özellikler:

- Pitch: -12 ile +12 semitone
- Formant: -6 ile +6 semitone; pitch'ten bağımsızdır
- Deep / Heavy, High / Nasal Rap, Dark Vocal, Radio, Robot ve Tiny / High Voice
- Vocal weight: pitch/formant değiştirmeden sesli harflerde göğüs gövdesi ve yoğunluk
- Character EQ, drive, dry/wet ve output gain
- Kullanıcı preset'i kaydetme, güncelleme ve silme
- Önceki/sonraki preset ve bypass hotkey'leri
- 48 kHz işlemde sabit 16 ms Voice Effects DSP gecikmesi
- Deadline, kuyruk ve düşen frame için inline runtime telemetry

Voice Effects yeni bir servis, DLL kurulumu veya harici runtime gerektirmez.
Ayarlar config.txt içinde portable olarak saklanır.

Mikrofon monitor çıkışına veriliyorsa geri beslemeyi önlemek için kulaklık
kullanılması önerilir.

GECİKME AYARLARI
----------------
audio_sample_rate=48000
audio_buffer_ms=5

0 değeri Windows/miniaudio varsayılanını kullanır. Cızırtı, kesilme veya
dropout olursa audio_buffer_ms değeri yükseltilmelidir.

GÖMÜLÜ WAV EDİTÖRÜ
------------------
Ses editörü ayrı popup yerine ana uygulama penceresinin içinde çalışır.

Özellikler:

- Async WAV yükleme ve kaydetme
- Uzun yükleme/kaydetme işlemini iptal etme
- Dalga formu görüntüleme
- Oynatma, duraklatma, seek, zoom ve ekrana sığdırma
- Monitor cihazından preview
- Hassas seçim ve trim
- Cut, copy ve paste
- Undo ve redo
- Gain ayarı
- Peak normalize
- Fade-in ve fade-out
- Mono'ya dönüştürme
- Baştaki ve sondaki sessizliği kırpma
- Save, Save As ve üzerine yazma
- Hatalı girişlerde popup yerine inline durum mesajı
- DPI ölçeklendirmesine uyumlu responsive toolbar

EDİTÖR KISAYOLLARI
------------------
I : Oynatma imlecini seçim/kırpma başlangıcı yapar.
O : Oynatma imlecini seçim/kırpma sonu yapar.

UYGULAMA VE LOGLAR
------------------
start_with_windows=false
show_console_on_start=false
check_updates_on_start=true

Windows ile başlatma kullanıcı hesabına özel ayarlanır ve yönetici izni
istemez. Portable klasör taşınıp uygulama yeniden çalıştırıldığında kayıtlı
başlangıç yolu güncellenir.

Uygulama normalde konsolsuz açılır. Tanılama konsolu panel veya tray
üzerinden gerektiğinde gösterilebilir.

check_updates_on_start=true en güncel kararlı GitHub Release sürümünü arka
planda denetler. Güncelleyici dosya indirmez, EXE'yi değiştirmez veya otomatik
kod çalıştırmaz; yalnızca resmi Release sayfasını açmayı teklif eder.

Her başarılı oturum logs\latest.log dosyasına yazılır. Önceki oturum
logs\previous.log olarak saklanır.

VARSAYILAN KONTROLLER
---------------------
F11             : Tüm sesleri durdur
CTRL+SHIFT+F9   : Ana çıkışı mute/unmute
CTRL+SHIFT+F10  : Monitor çıkışını mute/unmute
CTRL+SHIFT+F11  : Config'i yeniden yükle
CTRL+SHIFT+F12  : Programı kapat
CTRL+ALT+F21    : Önceki Voice Effects preset'i
CTRL+ALT+F22    : Sonraki Voice Effects preset'i
CTRL+ALT+F23    : Voice Effects bypass aç/kapat

KONTROL PANELİ
--------------
Panel tek pencere içinde Ana ekran, Ayarlar, Hotkey'ler, Mikrofon filtreleri,
Voice Effects ve Ses editörü sekmelerini kullanır. Ayrı efekt popup'ı açılmaz.

Panel özellikleri:

- Açık/koyu kalıcı tema
- DPI ölçeklendirme
- Canlı mikrofon ve çıkış göstergeleri
- Ana, monitor ve mikrofon cihaz seçimi
- Bağımsız ses ve mute ayarları
- Mikrofon yönlendirme ve işleme ayarları
- Hotkey yakalama
- Ses ataması ekleme/düzenleme
- Local ve URL medya aktarımı
- Log klasörünü açma
- Güncelleme kontrolü
- Windows ile başlatma
- Inline hata ve durum mesajları

Kaydet ve uygula tüm ayarları önce doğrular. Hotkey çakışması, cihaz hatası
veya runtime kurulumu başarısız olursa önceki çalışan ayarlar korunur.

Pencerenin X düğmesi programı kapatmaz; paneli tray'e gizler.

PANEL KISAYOLLARI
-----------------
CTRL+1 ... CTRL+6          : Sekmeler arasında geç
CTRL+S                    : Kaydet ve uygula
ESC                       : Hotkey yakalamayı iptal et

TRAY MENÜSÜ
-----------
Tray ikonuna sağ tıklayarak:

- Kontrol panelini aç
- Config'i yenile
- Tüm sesleri durdur
- Ana çıkışı sustur/aç
- Monitor çıkışını sustur/aç
- Tanılama konsolunu göster
- Programı kapat

İkona çift tıklamak kontrol panelini yeniden açar.

CİHAZ KURTARMA
--------------
Ayarlanmış bir ses cihazı kaybolursa uygulama kapanmaz. Ses sistemi belirli
aralıklarla yeniden kurulur ve cihaz geri geldiğinde RAM'deki sesler tekrar
kullanıma alınır.

WINDOWS GÜVENLİK NOTU
---------------------
Resmi GitHub release dosyaları şu anda kod imzası taşımaz. Microsoft Defender
SmartScreen bilinmeyen yayıncı uyarısı gösterebilir. Windows 11 Smart App
Control yeni ve imzasız EXE'yi engelleyebilir.

ZIP'i yalnızca resmi GitHub Release sayfasından indir ve yanında yayınlanan
.sha256 dosyasıyla karşılaştır:

Get-FileHash .\SoundBoardFasaFiso-v2.2.1-windows-x64-portable.zip -Algorithm SHA256

Eşleşen SHA-256, arşivin yayınlanan Release dosyasıyla aynı olduğunu
doğrular; kod imzasının yerini tutmaz. Programı çalıştırmak için Windows
güvenliğini kalıcı olarak zayıflatma.

PORTABLE RELEASE DOĞRULAMASI
----------------------------
Resmi release sistemi şunları denetler:

- x64 Windows GUI PE formatı
- Portable klasör içerik allowlist'i
- Güvenli varsayılan config
- Media-tool SHA-256 değerleri
- Gerekli üçüncü taraf lisansları
- Gizli dosya ve klasör bulunmaması
- Symbolic link/junction kalıntısı bulunmaması
- PDB, OBJ, LOG, TMP ve diğer build artıkları bulunmaması
- ZIP için SHA-256 checksum üretimi
- Taze derlenmiş tam test suite

NOTLAR
------
- Ses yolları sounds klasörüne göre yazılır; alt klasör kullanılabilir.
- Absolute path ve ".." traversal reddedilir.
- Hatalı reload uygulanmaz; son çalışan ayarlar korunur.
- Aynı anda uygulamanın yalnızca bir kopyası çalışır.
- Resmi portable pakette medya araçları ve gerekli lisanslar bulunur.
- Uygulama internetten kendini otomatik güncellemez.
