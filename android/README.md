# Aplicația Android „Exam Clock Controller"

Sursele aplicației (Kotlin + Jetpack Compose), pachet `com.example.calinmihai`:

- `MainActivity.kt`: interfața (tab-uri Countdown, Ceas, Program, Luminoz., Control, Status)
- `BluetoothHelper.kt`: conexiunea la HC-05 prin Bluetooth clasic (SPP)

Sunt doar fișierele sursă, nu un proiect Android Studio complet. Le pui într-un proiect Compose cu pachetul `com.example.calinmihai` și permisiunile Bluetooth declarate în `AndroidManifest.xml`.

## Protocol către placă

Aplicația trimite linii text terminate cu `\n`: `T=<secunde>`, `PAUSE`, `RESUME`, `STOP`, `C=HH:MM:SS`, `START=HH:MM`, `END=HH:MM`, `BRIGHT=0-255`, `MSG=...`. Firmware-ul din `../src/main.c` le înțelege pe toate (vezi tabelul de comenzi din `../docs/GHID.md`).

## Probleme cunoscute (neremediate în aceste fișiere)

Detalii și cod de înlocuire sunt în `../docs/GHID.md`, secțiunea „Aplicația Android":

1. Butonul `Disconnect` trimite `STOP` înainte de deconectare, deci ar opri cronometrul în timpul unui examen.
2. `connectLegacy()` se apelează pe firul UI și blochează interfața cât durează conectarea.

Fixurile nu au fost compilate.
