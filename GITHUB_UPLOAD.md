# GitHub'a yükleme

`dist/fifa07-github-source/` klasörü yüklemeye hazır kaynak ağacıdır.
`dist/fifa07-github-source.zip` aynı ağacın sıkıştırılmış kopyasıdır; ZIP'i
GitHub'a tek dosya olarak yüklemek yerine açıp içindeki dosyaları depo olarak
gönder. Böylece GitHub kaynak kodunu gösterebilir.

Paket; port/src, port/tests, üretilmiş port/generated/default C++/başlıkları,
manifest, CMake dosyaları, araçlar, bağımlılık kilidi, lisanslar ve belgeleri
içerir. Oyun verisi, EXE/DLL, SDK/LLVM ikili paketleri, kayıtlar, önbellek ve
ham tanılama kayıtları dahil değildir. SDK kaynakları sabit commit üzerinden
kurulum betiğiyle alınır. Özgün port kodu MIT; diğer kaynakların lisansları
THIRD_PARTY_NOTICES.md dosyasında açıklanmıştır.

GitHub'da boş bir depo oluştur; otomatik README veya LICENSE ekleme. Ardından
kaynak klasöründe çalıştır:

```powershell
git init -b main
git add .
git commit -m "Initial FIFA 07 ReXGlue port sources"
git remote add origin https://github.com/KULLANICI/DEPO.git
git push -u origin main
```

`KULLANICI/DEPO` kısmını kendi deponla değiştir. Git kimliği ayarlanmamışsa Git
ad ve e-posta isteyecektir; kendi bilgilerini veya GitHub noreply adresini kullan.
Bu hazırlık GitHub'da depo oluşturmaz veya hesabına herhangi bir şey göndermez.

Paketin yeniden üretilmesi:

```powershell
python tools/package-source.py
```

Tam maç, kariyer kaydı/yükleme ve çevrimiçi özelliklerin test durumunu README'de
olduğu gibi koru; mevcut doğrulama kayıtları bunların tamamlandığını göstermiyor.
