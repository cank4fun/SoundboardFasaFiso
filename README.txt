SoundBoardFasaFiso
==================

Windows 10/11 için kurulumsuz, portable soundboard.

DİL / LANGUAGE
--------------
language=tr : Türkçe
language=en : English

HIZLI KULLANIM
--------------
1. Ses dosyalarını sounds klasörüne koy.
2. config.txt içindeki cihazları ve tuşları ayarla.
3. SoundBoardFasaFiso.exe dosyasını çalıştır.
4. Config değişikliklerinden sonra CTRL+SHIFT+F11'e bas.

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

GECİKME AYARLARI
----------------
audio_sample_rate=48000
audio_buffer_ms=5

0 değeri Windows/miniaudio varsayılanını kullanır. Cızırtı veya kesilme
olursa audio_buffer_ms değerini yükselt.

VARSAYILAN KONTROLLER
---------------------
F11             : Tüm sesleri durdur
CTRL+SHIFT+F9   : Ana çıkışı mute/unmute
CTRL+SHIFT+F10  : Monitör çıkışını mute/unmute
CTRL+SHIFT+F11  : Config'i yeniden yükle
CTRL+SHIFT+F12  : Programı kapat

TRAY MENÜSÜ
-----------
Tray ikonuna sağ tıklayarak config'i yenileyebilir, sesleri durdurabilir,
çıkışları susturabilir, konsolu gösterebilir veya programı kapatabilirsin.
İkona çift tıklamak konsolu gösterip gizler.

NOTLAR
------
- Program config.txt ve sounds klasörünü EXE'nin yanından okur.
- Ses yolları sounds klasörüne göre yazılır; alt klasör kullanılabilir.
- Cihaz koparsa program açık kalır ve yeniden bağlanmayı dener.
- Hatalı reload uygulanmaz; son çalışan ayarlar korunur.
- Aynı anda uygulamanın yalnızca bir kopyası çalışır.
