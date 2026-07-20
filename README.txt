SoundBoardFasaFiso
==================

Windows 10/11 için kurulumsuz, portable soundboard.

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
1. Ses dosyalarını sounds klasörüne koy.
2. config.txt içindeki cihazları ve tuşları ayarla.
3. SoundBoardFasaFiso.exe dosyasını çalıştır; kontrol paneli açılır.
4. Ses cihazlarını, kontrol hotkey'lerini ve ses atamalarını panelden düzenle.
5. Kaydet ve uygula'ya bas; yeni config doğrulanıp güvenli şekilde etkinleştirilir.

KLASÖR YAPISI
-------------
SoundBoardFasaFiso.exe
config.txt
sounds\
README.txt

DESTEKLENEN FORMATLAR
--------------------
WAV, MP3 ve FLAC

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

CİHAZ AYARLARI
--------------
output=CABLE Input
monitor=default

"default" Windows varsayılan cihazını kullanır.
"none" monitör çıkışını kapatır.
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

GECİKME AYARLARI
----------------
audio_sample_rate=48000
audio_buffer_ms=5

0 değeri Windows/miniaudio varsayılanını kullanır. Cızırtı veya kesilme
olursa audio_buffer_ms değerini yükselt.

UYGULAMA VE LOGLAR
------------------
start_with_windows=false
show_console_on_start=true

Windows ile başlatma kullanıcı hesabına özel olarak ayarlanır ve yönetici
izni istemez. Konsol başlangıçta gizlense bile panel veya tray üzerinden
tekrar açılabilir. Her oturum logs\latest.log dosyasına yazılır; önceki
oturum logs\previous.log olarak tutulur.

VARSAYILAN KONTROLLER
---------------------
F11             : Tüm sesleri durdur
CTRL+SHIFT+F9   : Ana çıkışı mute/unmute
CTRL+SHIFT+F10  : Monitör çıkışını mute/unmute
CTRL+SHIFT+F11  : Config'i yeniden yükle
CTRL+SHIFT+F12  : Programı kapat

KONTROL PANELİ
---------------
Panel modern kart düzeni, açık/koyu tema desteği ve canlı sinyal göstergeleri kullanır. Panelden
ana/monitör/mikrofon cihazı, ses seviyeleri, mikrofon yönlendirmesi,
dil, örnekleme hızı, buffer, Windows başlangıcı, konsol başlangıcı,
kontrol hotkey'leri ve ses atamaları düzenlenebilir. Ses ataması eklerken
WAV/MP3/FLAC seçilebilir; dışarıdaki dosya isteğe bağlı olarak sounds
klasörüne kopyalanır. Kaydet ve uygula tüm ayarları önce doğrular; hotkey
çakışması veya ses sistemi hatasında önceki çalışan ayarlar geri yüklenir.
Pencerenin X düğmesi programı kapatmaz; paneli tray'e gizler.

TRAY MENÜSÜ
-----------
Tray ikonuna sağ tıklayarak kontrol panelini açabilir, config'i yenileyebilir,
sesleri durdurabilir, çıkışları susturabilir, konsolu gösterebilir veya
programı kapatabilirsin. İkona çift tıklamak kontrol panelini açar.

NOTLAR
------
- Program config.txt ve sounds klasörünü EXE'nin yanından okur.
- Ses yolları sounds klasörüne göre yazılır; alt klasör kullanılabilir.
- Cihaz koparsa program açık kalır ve yeniden bağlanmayı dener.
- Hatalı reload uygulanmaz; son çalışan ayarlar korunur.
- Aynı anda uygulamanın yalnızca bir kopyası çalışır.
