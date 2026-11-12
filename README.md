# Ceas cu cronometru de examen (STM32F103 + WS2812B + HC-05)

Un singur fișier de cod (`src/main.c`, ~570 linii cu comentarii; ~200 sunt configurarea hardware, ~370 logica aplicației), fără HAL și fără CubeMX. Înțelege comenzile aplicației tale Android „Exam Clock Controller".

**Ce face**
- **Ceas normal** HH:MM:SS (albastru), cu separatorii care clipesc o dată pe secundă. Ora se sincronizează de pe telefon (`C=`) sau se setează cu `SET`.
- **Cronometru de examen** (numărătoare inversă), culoarea după timpul rămas:
  - 🟢 verde: peste 30 min
  - 🟡 galben: sub 30 min
  - 🟠 portocaliu: sub 10 min
  - 🔴 roșu: sub 1 min

  Pragurile sunt aceleași ca în previzualizarea din aplicație. Se pot schimba (sau treci pe „treimi din durată", ca în lucrare) din constantele de la începutul lui `main.c`.
- La 00:00:00 afișajul clipește roșu 10 secunde, apoi revine singur la ceas.
- **Program de examen** `START=HH:MM` + `END=HH:MM`: cronometrul pornește singur la ora START și ajunge la zero exact la ora END.
- Pauză / reluare, luminozitate 0–255, totul prin Bluetooth.
- Ceasul normal merge **și în timpul cronometrului**, deci după examen ora e corectă.

## 1. Ipoteze despre hardware (din lucrarea de licență de la care pornește proiectul)

| Funcție | Pin | Observație |
|---|---|---|
| Date LED-uri WS2812B | **PA6** (TIM3_CH1 + DMA1 canal 6) | 172 LED: 6 digiți × 28 + 2 × 2 separatori |
| HC-05 RXD ← MCU | **PA9** (USART1 TX) | TX/RX se încrucișează |
| HC-05 TXD → MCU | **PA10** (USART1 RX) | 9600 baud |
| Programare | SWD (ST-LINK V2) | |

> ⚠️ **Dacă ai făcut PCB-ul după schema originală, linia de date a LED-urilor e pe PA7.** PA7 este TIM3_CH2 și nu poate folosi DMA. În lucrare s-a tăiat traseul și s-a făcut un jumper pe **PA6** (cap. 3.3). Fără această modificare nu se aprinde nimic.

### ⚠️ Un singur lucru de verificat: cristalul (`HSE_MHZ`)

```c
#define HSE_MHZ  6     /* 6 = PCB-ul din lucrare (cristal 6 MHz), 8 = Blue Pill */
```

Dacă pui valoarea greșită, LED-urile nu merg corect, iar ceasul merge prea repede sau prea încet. Verifică ce scrie pe cristal.

Alte constante de la început: `LED_COUNT`, `DEFAULT_BRIGHT`, `END_BLINK_SEC`, `COLOR_BY_THIRDS`, `T_YELLOW_SEC`, `T_ORANGE_SEC`, `T_RED_SEC`.

## 2. Compilare și încărcare

**PlatformIO (varianta recomandată):**
1. Instalează VS Code + extensia *PlatformIO IDE*.
2. Pune fișierele așa: `platformio.ini` în rădăcina folderului, `main.c` în `src/`.
3. Conectează ST-LINK V2 (SWD) și apasă **Upload**.

`main.c` folosește doar antetele CMSIS (`stm32f1xx.h`). Merge și în Keil sau STM32CubeIDE, într-un proiect gol pentru STM32F103C8 cu fișierul de startup. Această variantă **nu am testat-o**.

## 3. Controlul de pe telefon

La pornire se aprind scurt toate segmentele (**88:88:88**, test pentru cei 172 LED), apoi apare ceasul `00:00:00`. Fără baterie RTC, ora trebuie sincronizată după fiecare pornire.

1. În setările Bluetooth ale telefonului, împerechează **HC-05** (PIN `1234`, uneori `0000`).
2. Deschide aplicația ta, apasă **Connect**, apoi tab-ul **Ceas** → **PORNEȘTE** ca să sincronizezi ora.
3. Tab-ul **Countdown**: alegi timpul și apeși **START**.

Alternativ, orice terminal Bluetooth merge (de exemplu *Serial Bluetooth Terminal*, cu *Newline* pe LF). Scrii comenzile de mai jos.

### Comenzi

Literele mari/mici nu contează, spațiile de la final se ignoră, iar fiecare comandă se termină cu Enter. Ceasul răspunde cu `OK ...` sau `ERR ...` (aplicația le afișează în tab-ul *Status*).

| Comandă | Efect | Trimisă de aplicație |
|---|---|---|
| `T=<secunde>` (ex. `T=5400`) | pornește cronometrul (1–359999 s) | START |
| `HH:MM:SS` (ex. `01:30:00`) | la fel, cu ora scrisă direct | |
| `0` sau `STOP` | oprește cronometrul, revine la ceas **și anulează programul START/END** | STOP |
| `PAUSE` / `RESUME` | pauză (afișajul clipește lent, timpul stă) / reluare | PAUSE, RESUME |
| `C=HH:MM:SS` | sincronizare ceas. Corectează doar dacă diferența e ≥ 2 s și nu răspunde, deci poate veni în fiecare secundă fără să deranjeze | tab Ceas |
| `SET HH:MM:SS` | setează ora exact (pentru terminal) | |
| `START=HH:MM` + `END=HH:MM` | programul de examen (vezi mai jos) | tab Program |
| `BRIGHT=0-255` (sau `BRIGHT 0-255`) | luminozitate | tab Luminoz. |
| `MSG=PAUSE`, `MSG=STOP` | la fel ca `PAUSE`, `STOP` | tab Control |
| `MSG=EXAMEN` | răspunde `OK MSG`, **fără efect pe afișaj** | tab Control |
| `STATUS` | trimite modul, ora, timpul rămas, programul | |
| `HELP` | lista comenzilor | |

O comandă nouă de pornire o înlocuiește pe cea în curs. Un cronometru de `0` secunde este refuzat.

### Cum funcționează programul START/END

- Ordinea nu contează, iar programul se activează când ai primit amândouă.
- Dacă ora curentă e înainte de START, ceasul așteaptă. La START pornește cronometrul cu durata `END − START` și ajunge la zero exact la END.
- Dacă ora curentă e deja între START și END, cronometrul pornește imediat pentru timpul rămas până la END.
- Merge și peste miezul nopții (ex. `START=23:30`, `END=00:15`).
- Programul se consumă după pornire. `STOP` îl anulează. Un cronometru pornit manual nu e întrerupt de program, iar programul pornește după ce acesta se termină, dacă mai e în interval.
- `START=09:00` și `END=09:00` sunt refuzate.

## 4. Alimentare și curent

LED-urile consumă mult. La luminozitate 50 (implicit): aproximativ 0,5–0,8 A pentru un afișaj obișnuit (cel mai rău caz, `88:88:88`, circa 1,2 A). La 120 (valoarea implicită a slider-ului din aplicație) poate fi 1,5–2,5 A. La 255 pot fi 3–6 A. Folosește o sursă de 5 V cu cel puțin 2 A și nu ridica luminozitatea peste ce suportă sursa.

## 5. Aplicația Android: ce am găsit

Codul e bine structurat, dar nu era compatibil cu firmware-ul inițial (acum e) și are două probleme:

**1. `Disconnect` trimite `STOP` înainte să se deconecteze.** În timpul unui examen, o deconectare (sau o apăsare greșită) ar opri cronometrul. Cronometrul rulează pe placă și nu are nevoie de telefon conectat. În `MainActivity.kt`, butonul `Disconnect`:

```kotlin
Button(
    onClick = {
        bt?.disconnect()
        bt = null
    },
    enabled = connected
) { Text("Disconnect") }
```

**2. `connectLegacy()` blochează interfața.** Metoda `connect()` a socket-ului poate dura câteva secunde și se apelează direct în `onClick`, pe firul UI, deci aplicația îngheață cât se conectează. Mută-l pe un fir de fundal. Adaugă importurile `kotlinx.coroutines.Dispatchers`, `kotlinx.coroutines.launch` și `kotlinx.coroutines.withContext`. Lângă `val context = LocalContext.current` adaugă `val scope = rememberCoroutineScope()`. Apoi, pe butonul Connect:

```kotlin
onClick = {
    scope.launch(Dispatchers.IO) {
        val helper = BluetoothHelper("HC-05")
        val ok = helper.connectLegacy()
        withContext(Dispatchers.Main) {
            if (ok) {
                bt = helper
                Toast.makeText(context, "Conectat", Toast.LENGTH_SHORT).show()
            } else {
                helper.disconnect()
                bt = null
                Toast.makeText(context, "Conectare eșuată", Toast.LENGTH_LONG).show()
            }
        }
    }
},
```

Nu am compilat codul Kotlin (nu am Android Studio aici), deci încearcă-l și spune-mi dacă dă erori.

**Observații minore:**
- Sincronizarea din tab-ul *Ceas* se oprește când schimbi tab-ul, pentru că `clockRunning` e ținut în interiorul tab-ului. Nu e o problemă, fiindcă o singură sincronizare ajunge (ceasul de pe placă merge singur după ea).
- Dacă nu e acordată permisiunea Bluetooth, conectarea eșuează fără să spună de ce. Mesajul „Conectare eșuată" acoperă și acest caz.
- `MSG=EXAMEN` nu are efect vizibil pe placă (vezi tabelul).

## 6. Depanare

| Simptom | Cauză probabilă |
|---|---|
| Nu se aprinde nimic | linia de date nu e pe PA6; `HSE_MHZ` greșit; lipsă alimentare 5 V |
| Culorile sunt amestecate (verde↔roșu) | LED-uri cu altă ordine decât G-R-B. Schimbă ordinea în `ws_show` (variabila `c`). |
| Ceasul merge prea repede / încet | `HSE_MHZ` nu corespunde cristalului |
| Telefonul se conectează, dar placa nu răspunde | TX/RX neîncrucișate (PA9↔RXD, PA10↔TXD); HC-05 pe altă viteză decât 9600; alimentare de 3,3 V instabilă (în lucrare a mers pe 5 V) |
| `ERR comanda necunoscuta` | verifică formatul comenzii din tabel |

## 7. Ce am verificat și ce nu

- **Compilat** pentru Cortex-M3 (STM32F103C8) cu `arm-none-eabi-gcc`, fără avertismente (4,9 KB flash, 10,6 KB RAM din 20 KB), pentru `HSE_MHZ` 6 și 8.
- **Logica testată pe calculator**, pe exact codul din `main.c`, cu LED-urile simulate. Aproape 29.000 de verificări (în ambele moduri de culoare), printre care:
  - toate cifrele pe toate pozițiile
  - un examen de 3 ore simulat integral, cu culoarea și timpul verificate la **fiecare secundă** și fără derivă
  - programul START/END (în viitor, în interval, peste miezul nopții, anulare, conflict cu cronometrul manual)
  - sincronizarea `C=` în fiecare secundă timp de 400 s
  - depășirea contorului `millis()` după ~49 de zile
  - un **replay cu cele 11 mesaje exacte** trimise de aplicația ta (extrase automat din `MainActivity.kt`)
- **Testele au fost verificate prin mutații**: am introdus deliberat 24 de bug-uri în logică. Testele au prins 23, iar al 24-lea era un mutant echivalent, fără efect real (limita de cifre la `T=`, oricum respinsă de verificarea intervalului). Două goluri găsite astfel (program peste miezul nopții, program vs. cronometru manual) au fost acoperite cu teste noi.
- **Șirul de biți** spre LED-uri verificat separat: ordinea G-R-B, 0,42 µs / 0,83 µs la perioadă de 1,25 µs, aceleași valori ca în codul CubeMX din lucrare.
- **Rulezi testele singur** (are nevoie doar de `gcc`): `./test/run_tests.sh`

- **Neverificat pe placă reală.** Nu am avut hardware, deci partea de registre (ceas PLL, TIM3 + DMA, USART1) e scrisă după manualul STM32F103 și configurația din lucrare, dar nu a rulat pe un STM32. Nici aplicația Android nu a rulat împreună cu codul.

## Structura proiectului

```
platformio.ini      configurația PlatformIO (STM32F103C8, ST-LINK)
src/main.c          firmware-ul
test/               teste ale logicii pe calculator (run_tests.sh)
```
