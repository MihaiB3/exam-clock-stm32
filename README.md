# Ceas cu cronometru de examen

Ceas digital cu LED-uri care poate fi și cronometru pentru examen, controlat de pe telefon prin Bluetooth.

- **Ceas normal** (ore:minute:secunde) pe un afișaj cu 172 de LED-uri WS2812B.
- **Cronometru de examen**: numărătoare inversă care își schimbă culoarea cu timpul rămas (verde, galben, portocaliu, roșu).
- **Control de pe telefon** prin Bluetooth (modul HC-05), cu o aplicație Android.
- Microcontroler **STM32F103**, programat cu PlatformIO.

## Ce e în repo

- `src/main.c` – programul pentru microcontroler
- `android/` – aplicația Android
- `test/` – teste (`./test/run_tests.sh`)
- `docs/GHID.md` – ghid detaliat: comenzi, depanare

## Cum îl pornești

1. Deschide folderul în VS Code cu extensia PlatformIO și apasă Upload (cu ST-LINK).
2. Înainte, verifică în `main.c` valoarea `HSE_MHZ`: 6 pentru placa proprie, 8 pentru Blue Pill.
3. Împerechează HC-05 cu telefonul și conectează-te din aplicație.

Stare: în lucru, încă netestat pe placa reală.
