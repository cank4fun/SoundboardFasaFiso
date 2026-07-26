SoundBoardFasaFiso
==================

Windows 10/11 için kurulumsuz, portable soundboard.

SÜRÜM / VERSION
---------------
2.1.0

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
1. SoundBoardFasaFiso.exe dosyasını çalıştır; kontrol paneli açılır.
2. WAV, MP3 veya FLAC dosyalarını ya da bunları içeren bir klasörü panelin üzerine bırak. Dışarıdaki dosyalar sounds\Imported altına güvenli biçimde kopyalanır; Gözat düğmesi birden fazla dosya seçebilir.
3. İnternetten medya eklemek için ses alanına http:// veya https:// adresi yapıştırıp URL düğmesine bas. Paket içindeki doğrulanmış medya araçları medyayı arka planda indirip WAV'a dönüştürür.
4. Ses editöründe monitör çıkışından önizle, seçim ve hassas kırpma yap, düzenleme ve efektleri uygula, ardından WAV olarak kaydet. I kırpma başlangıcını, O kırpma bitişini oynatma imlecine ayarlar.
5. Hotkey'ler sekmesinde her ses için bir atama oluştur. İçe aktarma dosyayı editöre doldurur ancak otomatik global hotkey atamaz.
6. Ses cihazlarını ve kontrol hotkey'lerini panelden düzenle.
7. Kaydet ve uygula'ya bas; yeni config doğrulanıp güvenli şekilde etkinleştirilir.

Hiç etkin ses ataması olmasa veya eski atamalardaki dosyalar kayıp olsa bile panel açık kalır; config.txt dosyasını elle düzeltmeden yeni atama ekleyebilirsin.

KLASÖR YAPISI
-------------
SoundBoardFasaFiso.exe
portable.flag
config.txt
sounds\
tools\
README.txt
LICENSE
THIRD_PARTY_NOTICES.txt
WEBRTC_THIRD_PARTY_NOTICES.txt (AEC build)

DESTEKLENEN FORMATLAR
--------------------
WAV, MP3 ve FLAC

ÖRNEK ATAMALAR
--------------
F1=example.wav
F2=example.mp3|volume=0.80|mode=restart
F3=example.flac|volume=1.00|mode=overlap
F4=example.wav|volume=1.00|mode=toggle
F5=example.wav|volume=1.00|mode=ignore

SES MODLARI
-----------
restart : Her basışta sesi baştan çalar.
overlap : Aynı sesi üst üste bindirir; en fazla 8 voice kullanır.
toggle  : Pasifse çalar, aktif veya duraklatılmışsa durdurur.
loop    : Döngüye alır, tekrar basışta durdurur.
ignore  : Pasifse çalar, aktif veya duraklatılmışsa basışı yok sayar.

CİHAZ AYARLARI
--------------
output=default
monitor=none

"default" Windows varsayılan cihazını kullanır.
"none" monitör çıkışını kapatır.
VB-CABLE kullanıyorsan output alanına genellikle "CABLE Input" yazılır.
Cihaz adlarında tam isim yerine ayırt edici bir bölüm yazılabilir.

MİKROFON MİKSERİ
-----------------
microphone_enabled=false
microphone=default
microphone_volume=1.00
microphone_to_output=true
microphone_to_monitor=false

Mikrofon etkinleştirildiğinde fiziksel mikrofon ana çıkışa, monitöre veya
ikisine birden gerçek zamanlı eklenir. Monitöre yönlendirmede geri besleme
olmaması için kulaklık kullanılması önerilir. Discord/Steam gibi uygulamalara
mikrofon olarak vermek için ana çıkışta sanal bir audio endpoint seçilebilir.

MİKROFON İŞLEME
----------------
microphone_processing_enabled=false
microphone_processing_preset=natural
microphone_echo_cancellation_enabled=false
microphone_noise_suppression_enabled=false
microphone_agc_enabled=false

İşleme varsayılan olarak kapalıdır. WebRTC AEC3 içeren resmi v2.1 build'inde
echo cancellation, yalnızca fiziksel monitöre gönderilen soundboard sesini
referans alır. Mikrofon monitor sesi ve sanal ana çıkış referansa eklenmez.
WebRTC olmadan derlenen sürüm ayarı korur ancak AEC kontrolünü pasif gösterir.

GECİKME AYARLARI
----------------
audio_sample_rate=48000
audio_buffer_ms=5

0 değeri Windows/miniaudio varsayılanını kullanır. Cızırtı veya kesilme
olursa audio_buffer_ms değerini yükselt.

UYGULAMA VE LOGLAR
------------------
start_with_windows=false
show_console_on_start=false
check_updates_on_start=true

Windows ile başlatma kullanıcı hesabına özel olarak ayarlanır ve yönetici
izni istemez. Uygulama konsolsuz açılır; hata ayıklama konsolu panel veya
tray üzerinden gerektiğinde gösterilebilir. check_updates_on_start=true en güncel kararlı GitHub
Release sürümünü arka planda denetler. Panelden elle de denetim yapılabilir.
Program dosya indirmez veya kendini otomatik değiştirmez; yalnızca resmi
Release sayfasını açmayı teklif eder. Her oturum logs\latest.log dosyasına
yazılır; önceki oturum logs\previous.log olarak tutulur.

VARSAYILAN KONTROLLER
---------------------
F11             : Tüm sesleri durdur
CTRL+SHIFT+F9   : Ana çıkışı mute/unmute
CTRL+SHIFT+F10  : Monitör çıkışını mute/unmute
CTRL+SHIFT+F11  : Config'i yeniden yükle
CTRL+SHIFT+F12  : Programı kapat

KONTROL PANELİ
---------------
Panel tek pencere içinde Ana ekran, Ayarlar, Mikrofon filtreleri, Hotkey'ler
ve Ses editörü sekmelerini kullanır. Açık/koyu tema, canlı sinyal göstergeleri ve DPI ölçeklendirme
desteklenir. Panelden ana/monitör/mikrofon cihazı, ses seviyeleri,
mikrofon yönlendirmesi, dil, örnekleme hızı, buffer, Windows başlangıcı,
kontrol hotkey'leri ve ses atamaları düzenlenebilir. Ses ataması eklerken
WAV/MP3/FLAC seçilebilir; birden fazla dosya veya klasör pencereye
sürüklenebilir. Dışarıdaki dosyalar sounds\Imported klasörüne kopyalanır,
aynı isimler _2, _3 şeklinde güvenle ayrılır. Kaydet ve uygula tüm ayarları önce doğrular; hotkey
çakışması veya ses sistemi hatasında önceki çalışan ayarlar geri yüklenir.
Pencerenin X düğmesi programı kapatmaz; paneli tray'e gizler.

PANEL KISAYOLLARI
-----------------
CTRL+1 ... CTRL+5       : Sekmeler arasında geç
CTRL+S                  : Kaydet ve uygula
ESC                     : Hotkey yakalamayı iptal et

SES EDİTÖRÜ KISAYOLLARI
-----------------------
SPACE                   : Önizlemeyi oynat/duraklat
I                       : Kırpma başlangıcını oynatma imlecine ayarla
O                       : Kırpma bitişini oynatma imlecine ayarla
CTRL+X / CTRL+C / CTRL+V: Kes / kopyala / yapıştır
CTRL+Z / CTRL+Y         : Geri al / yinele
CTRL+S                  : WAV dosyasını kaydet

TRAY MENÜSÜ
-----------
Tray ikonuna sağ tıklayarak kontrol panelini açabilir, config'i yenileyebilir,
sesleri durdurabilir, çıkışları susturabilir, konsolu gösterebilir veya
programı kapatabilirsin. İkona çift tıklamak kontrol panelini açar.

WINDOWS GÜVENLİK NOTU
---------------------
GitHub portable sürümü şu anda kod imzası taşımaz. SmartScreen bilinmeyen
yayıncı uyarısı gösterebilir. Windows 11 Smart App Control zorlamalı moddaysa
yeni ve imzasız EXE'yi tek uygulamalık izin seçeneği sunmadan engelleyebilir;
bu durum yerelde derlenen imzasız Release EXE'ler için de geçerlidir.

ZIP dosyasını yalnızca resmi GitHub Release sayfasından indir ve yanında
yayınlanan .sha256 dosyasıyla karşılaştır. PowerShell örneği:

Get-FileHash .\SoundBoardFasaFiso-v*-windows-x64-portable.zip -Algorithm SHA256

Eşleşen SHA-256, arşivin Release dosyasıyla aynı olduğunu doğrular; kod
imzasının yerini tutmaz. Programı çalıştırmak için Windows güvenliğini
kalıcı olarak zayıflatma.

NOTLAR
------
- Resmi ZIP içindeki portable.flag nedeniyle config, log, import ve tools verileri EXE klasöründe kalır.
- Portable klasör yazılabilir olmalıdır; Program Files gibi korumalı konumlarda uygulama açık hata verir ve başka yere sessizce veri yazmaz.
- portable.flag kaldırılırsa veri klasörü %LOCALAPPDATA%\SoundBoardFasaFiso olur; mevcut kullanıcı dosyaları üzerine yazılmaz.
- Uygulama yönetici yetkisi istemez. Yönetici olarak elle açmak Explorer sürükle-bırak özelliğini engelleyebilir.
- Ses yolları sounds klasörüne göre yazılır; alt klasör kullanılabilir.
- Cihaz koparsa program açık kalır ve yeniden bağlanmayı dener.
- Hatalı reload uygulanmaz; son çalışan ayarlar korunur.
- Aynı anda uygulamanın yalnızca bir kopyası çalışır.

- Her atama için fade_in_ms ve fade_out_ms değerleri 0-10000 milisaniye arasında ayarlanabilir.
