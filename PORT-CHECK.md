# FIFA 07 port kontrolü — 3 Kasım 2026

Mevcut sürüm: Release, ReXGlue 0.10.0.24-dev.gbd833a2.
EXE SHA-256: `F14AF5695EBE9292CD8B5E9279C315CE1A36AE17546DA714E55A0CACAB7D3899`.

Bu kontrol dosya bütünlüğü, yeniden derleme, mevcut regresyon testleri,
son normal oturumun kayıtları ve kaynak incelemesini kapsar. Bütün oyun
modlarının uçtan uca doğrulandığı anlamına gelmez.

## Doğrulananlar

| Kontrol | Sonuç | Kanıt |
|---|---|---|
| Çıkarılan oyun dosyaları | 57/57 dosyanın boyutu ve SHA-256 değeri ilk çıkarım kaydıyla aynı; toplam 4.603.909.983 bayt | logs/port-audit-assets.json |
| Release yeniden derleme | Başarılı, EXE kimliği değişmedi | logs/audit-release-build.log |
| Bellek politikası testleri | 995.328 durum geçti | logs/audit-memory-policy.log |
| Vektör komut testleri | 472 durum, 0 hata | logs/audit-vmx.log |
| Kol/bacak düzeltmesinin kaynakta korunması | 4 dosyada 64 güvenli kaynak kopyası mevcut | logs/port-audit-source-binaries.json |
| Resmî runtime ve GPU DLL dosyaları | SDK kopyalarıyla SHA-256 değerleri aynı | logs/port-audit-source-binaries.json |
| Menü/saha oyuncu geometrisi | Önceki kullanıcı kontrollerinde düzgün | MODEL-FIX.md |
| Menü tıklama/geçiş sesleri | Normal sürümde kullanıcı tarafından doğrulandı | AUDIO-ISSUE.md |
| Açılış ve maç öncesi sahneler | ROV varsayılanıyla kullanıcı tarafından doğrulandı; final kontrolde belirgin ilk donma bildirilmedi | CINEMATIC-ISSUE.md |
| Tanılama | Sinematik ve animasyon kayıt derlemeleri varsayılan olarak kapalı | port/CMakeLists.txt, port/out/build/win-release/CMakeCache.txt |

Son normal oturum kaydı yaklaşık 11 dakika 43 saniyelik bir çalışma gösteriyor.
Kapanışta pencere kapatma isteği ve oyunu sonlandırma kaydı var; bu kayıtta
fatal hata, assertion veya bellek tükenmesi kaydı bulunmadı. Bu gözlem bir tam
maçın tamamlandığını kanıtlamaz. Kullanıcı henüz tam maç bitirmediğini belirtti.

## Eksik destek

Oyunun bildirdiği 241 sistem işlevi içe aktarımından 21'i sabit SDK kaynağında
REX_EXPORT_STUB olarak tanımlanmış. Bunlar ağ/Xbox Live işlevleri, sesli sohbet
paket gönderimi ve arkadaşlar, davet, oyuncu kartı, başarılar, mağaza gibi konsol
arayüzlerini kapsıyor. Bu özellikler tamamlanmış destek olarak sunulmamalı.
İçe aktarımın bulunması, oyunun o işlevi denenen akışta çağırdığını kanıtlamaz.
Tam liste: logs/port-audit-imports.json.

## Öncelikli doğrulama açıkları

1. **Tam maç:** ilk yarı, devre arası, ikinci yarı, maç sonu ve menüye dönüş.
2. **Kayıt ve yeniden yükleme:** mevcut kullanıcı klasöründe 18.413 baytlık
   Settings 1 ve 328 baytlık başlık dosyası var. Ayar yazımı gerçekleşmiş;
   oyunu yeniden açınca ayarların geri gelmesi ve kariyer/turnuva kaydı ayrıca
   doğrulanmalı. Dosyanın varlığı tek başına kayıt sisteminin tamamını doğrulamaz.
3. **Diğer içerik ve modlar:** farklı takımlar/stadyumlar, kariyer ve turnuva,
   maç sonu tekrarları, spiker ve saha sesleri sistematik olarak denenmedi.
4. **Uzun oturum ve performans:** art arda maçlar, bellek/VRAM artışı,
   kare süresi ölçümü, Alt-Tab ve çözünürlük değişimleri doğrulanmadı.
5. **Giriş ve dağıtım:** ikinci kontrolcü/yerel çok oyunculu akış ve temiz bir
   bilgisayarda çalıştırma kontrolü yapılmadı. Mevcut kullanım geliştirici proje
   klasörü ve tools/run-local.cmd üzerinden; ayrı bir kurulum paketi hazırlanmamış.

## Kayıtlardaki şüpheli ama doğrulanmamış belirtiler

Son oturumda bazı gevşek dosya aramaları başarısız: örneğin leagueLogos/l2028.big,
(null).abk, banner_11.rx2 ve team_11_textures.rx2. Bunlar belirli bir görsel/ses
kusuruyla eşleştirilmedi. Oyunun BIG arşivleri ve geri dönüş yolları bulunduğu
için bu satırlar tek başına gerçek veri eksiği sayılmadı. Tam dosya bütünlüğü
kontrolü ilk çıkarımla eşleşiyor; orijinal oyunun her içerik yolunun çalıştığı
sonucu çıkarılmıyor.

En yararlı sonraki kontrol: bir tam maç bitirip menüye dönmek, ardından bir
ayar/kariyer kaydını oyunu kapatıp açarak yeniden yüklemek.
