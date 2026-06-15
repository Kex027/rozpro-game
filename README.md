# Gold Fever - Sieciowa Gra RTS-lite

Gold Fever to wieloosobowa gra sieciowa typu RTS-lite / Time Management stworzona w języku C++ z użyciem biblioteki **Raylib**. Gra realizuje model autorytatywnego serwera (Server-Authoritative) z dedykowanym protokołem sieciowym TCP.

---

## 🛠️ Wymagania przed kompilacją

### Na systemie Windows:
1. Zainstalowane środowisko **Visual Studio 2022** z zaznaczoną paczką narzędzi **"Programowanie aplikacji klasycznych w języku C++"** (dostarcza kompilator MSVC oraz narzędzie CMake).
2. Pliki biblioteki Raylib pobiorą się automatycznie podczas konfiguracji projektu – nie musisz nic instalować ręcznie!

### Na systemie Linux / WSL (Ubuntu):
Zainstaluj kompilator i narzędzia budowania:
```bash
sudo apt update
sudo apt install build-essential cmake
```

---

## 🚀 Kompilacja i Uruchomienie

### Opcja A: Windows (Serwer + Klient z grafiką)

Otwórz terminal PowerShell w głównym folderze projektu i wykonaj następujące komendy:

1. **Konfiguracja projektu przez CMake**:
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -B winbuild -S .
   ```
   *(Uwaga: Jeśli masz zainstalowane środowisko VS w innej ścieżce lub posiadasz globalnie zainstalowany `cmake`, możesz użyć po prostu komendy `cmake -B winbuild -S .`)*

2. **Kompilacja**:
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build winbuild --config Release
   ```

3. **Uruchomienie serwera**:
   ```powershell
   .\winbuild\Release\GoldFeverServer.exe
   ```

4. **Uruchomienie klienta**:
   ```powershell
   .\winbuild\Release\GoldFeverClient.exe
   ```

---

### Opcja B: Linux / WSL (Tylko Serwer konsolowy)

Otwórz terminal w głównym folderze projektu:

1. **Konfiguracja projektu**:
   ```bash
   cmake -B build -S .
   ```

2. **Kompilacja**:
   ```bash
   cmake --build build --config Release
   ```

3. **Uruchomienie serwera**:
   ```bash
   ./build/GoldFeverServer
   ```

---

## 🎮 Jak grać?

1. **Dołączenie do gry**: Po uruchomieniu klienta wpisz swój pseudonim oraz adres IP serwera (jeśli grasz na tym samym komputerze co serwer, pozostaw `127.0.0.1`) i kliknij **ENTER LOBBY**.
2. **Lobby (Samouczek)**: Jesteś na egzotycznej wyspie. Możesz poruszać się swoim kapeluszem, aby przetestować sterowanie. Gdy będziesz gotowy, naciśnij **SPACJĘ** (lub kliknij przycisk), aby oznaczyć gotowość. Kiedy wszyscy gracze zaznaczą gotowość, runda się rozpocznie.
3. **Pętla rozgrywki**:
   * Zbieraj sztabki złota pojawiające się losowo na mapie.
   * Zanieś zebrane złoto do swojej bazy (koło o kolorze Twojego kapelusza obracające się wokół kopalni), aby je bezpiecznie zapisać.
   * Otwórz sklep klawiszem **B**, aby zakupić ulepszenia za złoto zdeponowane w bazie.
   * Atakuj innych graczy klawiszem **F** (wymaga zakupu miecza w sklepie), aby ich ogłuszyć i ukraść niesione przez nich złoto.
   * Okradaj bazy innych graczy stając na nich (uważaj na ulepszenie obronne bazy przeciwnika – spowalnia złodziei!).
4. **Wygrana**: Gra składa się z 3 rund. Rundy wygrywa osoba z największą ilością złota zdeponowanego w bazie. Całą grę wygrywa gracz, który wygra najwięcej rund. W przypadku remisu decyduje łączna suma złota zdobyta w trakcie całego meczu.

---

## ⌨️ Sterowanie w grze
* **W, S, A, D** lub **Strzałki** – Ruch kapeluszem
* **SPACJA** – Oznaczenie gotowości (tylko w Lobby)
* **B** – Otwarcie / Zamknięcie Sklepu
* **F** – Atak mieczem (po zakupie w sklepie)
* **TAB** – Przełączanie pól tekstowych (w menu logowania)
