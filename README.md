# Helix 1.0

Sistema operativo da browser per **ESP32-S3-N16R8** (16 MB flash, 8 MB PSRAM). La scheda fa da desktop: ti colleghi in Wi-Fi e apri un’interfaccia con finestre, senza installare nulla sul telefono.

Versione firmware: `1.0`, definita in `src/config.h` (`HELIX_VERSION`). La mostra l’OLED, il seriale all’avvio e il comando `version`.

## Cosa contiene

| Pezzo | Dove | A cosa serve |
| --- | --- | --- |
| Firmware | `src/` | Wi-Fi, server, GPIO, SD, OLED, comandi |
| Desktop | `data/` | Pagine servite da LittleFS |
| App di sistema | `data/plugins/` | Un file JS per applicazione |
| Esempi SD | `examples/apps/` | Pagine da copiare sulla microSD |
| Laboratorio | `LAB.md` | Cablaggi di oscilloscopio, tester, UART, radio, ESP-NOW, script |
| Mappa flash | `partitions.csv` | Due app da 2,5 MB e LittleFS da circa 11,2 MB |

Il desktop non scarica librerie da internet. Funziona anche sull’access point, senza rete di casa.

## Hardware

| | |
| --- | --- |
| Modulo | ESP32-S3-WROOM-1-N16R8 |
| CPU | 240 MHz, due core |
| Flash | 16 MB, modalità `qio_opi` |
| PSRAM | 8 MB octal |
| USB | GPIO 19 e 20, nativa, CDC attivo all’avvio |
| OLED | SSD1306 128×64, I2C `0x3C` (se non risponde, prova `0x3D`) |
| MicroSD | SPI a 20 MHz, solo 3,3 V |

### Non usare questi pin

| Pin | Perché |
| --- | --- |
| 26–37 | Flash e PSRAM. Toccarli blocca la scheda. |
| 19, 20 | USB. |
| 8, 9 | OLED (SDA, SCL). |
| 10, 11, 12, 13 | SD (CS, MOSI, SCK, MISO). |
| 0, 3, 45, 46 | Strapping: solo se sai cosa fai. |

Pin liberi per GPIO, PWM e ADC: **1, 2, 4, 5, 6, 7, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47, 48**. Con il Wi-Fi acceso l’ADC affidabile è ADC1, cioè i GPIO 1–7. Gli altri pin analogici possono dare valori instabili.

Tutti i segnali esterni sono a **3,3 V**. Un segnale a 5 V va diviso prima di entrare in un GPIO.

### OLED 0.96"

| OLED | ESP32-S3 |
| --- | --- |
| GND | GND |
| VCC | 3V3 |
| SDA | GPIO 8 |
| SCL | GPIO 9 |

Senza display Helix parte lo stesso.

### MicroSD SPI

| Modulo | ESP32-S3 |
| --- | --- |
| GND | GND |
| VCC | 3V3 |
| CS | GPIO 10 |
| MOSI / DI | GPIO 11 |
| SCK / CLK | GPIO 12 |
| MISO / DO | GPIO 13 |

Alimenta il modulo a 3,3 V. Un adattatore da 5 V può alzare MISO oltre il limite del pin. Senza scheda il sistema parte e riprova il mount ogni 8 secondi. Sull’OLED compare `SD assente`.

Formatta la scheda in FAT32. Helix crea da sola la cartella `/apps` se manca.

## Compilare e caricare

Serve PlatformIO (estensione di VS Code, oppure `pio` da terminale). La cartella del progetto è quella che contiene `platformio.ini`. L’ambiente si chiama `esp32-s3-n16r8`.

Collega la scheda in USB. Se l’upload non parte: tieni premuto **BOOT**, premi e rilascia **RESET**, poi rilascia BOOT.

Due caricamenti, in quest’ordine:

```bash
pio run -t upload --environment esp32-s3-n16r8
pio run -t uploadfs --environment esp32-s3-n16r8
```

In VS Code sono gli stessi comandi: **Upload** e **Upload Filesystem Image**.

- `upload` scrive il programma.
- `uploadfs` scrive `data/` su LittleFS: `index.html`, `style.css`, `kernel.js` e `plugins/`. Senza questo passo il browser non ha il desktop, anche se il firmware è nuovo.

Monitor seriale a 115200:

```bash
pio device monitor
```

La prima riga utile è `helix 1.0  ESP32-S3-N16R8`.

### Arduino IDE

Scheda **ESP32S3 Dev Module**, Flash **16MB**, PSRAM **OPI**, USB CDC On Boot **Enabled**, partition scheme dal file `partitions.csv`. PlatformIO resta la strada prevista: il filesystem e le librerie sono già nel `platformio.ini`.

### Quando riflashare cosa

| Hai cambiato | Carica |
| --- | --- |
| File in `src/` o `platformio.ini` | solo firmware |
| File in `data/` | solo filesystem |
| Tutti e due, o non ricordi | tutti e due |

Dopo un cambio del desktop, sul telefono ricarica la pagina. L’`index.html` è `no-cache`; CSS e JS hanno `?v=` nel nome.

## Primo avvio e Wi-Fi di casa

1. All’accensione, se non c’è una rete salvata, si alza l’access point aperto **Helix-S3**, indirizzo `192.168.4.1`.
2. Collega il telefono a **Helix-S3**. Se Android dice che la rete non ha internet, scegli **Resta connesso** / **Usa questa rete**.
3. Non usare la finestrella automatica di accesso. Apri **Chrome** e vai su `http://192.168.4.1`.
4. Apri **Wi-Fi**, premi **Scansiona**, tocca la rete di casa e scrivi la password (minimo 8 caratteri, oppure vuota se la rete è aperta). Poi **Salva e collega**.

L’ESP32-S3 parla solo **Wi-Fi a 2,4 GHz**. Se il router ha due nomi, scegli quello senza «5G» in fondo. Il nome di un telefono tipo «moto g84 5g» è il modello, non una banda: un hotspot a 5 GHz non comparirà, e sull’OLED resta `solo 2.4GHz`.

Le credenziali stanno nella NVS, namespace `helix`, e non vengono scritte nel log. Una nuova coppia SSID/password sostituisce la precedente.

Quando la station entra nella rete di casa:

- l’access point **Helix-S3 si spegne**;
- l’OLED mostra `wifi ok` e l’indirizzo ricevuto dal router (di solito `192.168.1.x`);
- passa il telefono sulla **stessa rete di casa** e apri quell’indirizzo in Chrome.

Se il collegamento cade, l’access point torna da solo dopo il timeout (15 secondi).

## OLED

Quattro righe, aggiornate dal task di sistema:

1. `HELIX 1.0`
2. modo radio (`ap`, `sta`, `connecting`, …)
3. indirizzo IP
4. ultima riga di log (`credenziali …`, `wifi ok …`, `timeout …`, `solo 2.4GHz`)

I messaggi `ws #` del browser non finiscono sul display.

## Desktop

Barra in alto: logo **H** (apre Impostazioni), scritta Helix, icone delle app di sistema, orologio, pillola con rete e IP.

Ogni finestra ha riduci, ingrandisci e chiudi. Le app ridotte restano nella barra.

| Icona | App | Ruolo |
| --- | --- | --- |
| CPU | Monitor | CPU dei due core, heap, PSRAM, LittleFS, SD, temperatura, RSSI |
| file | File | LittleFS e SD: lista, download, upload, elimina |
| >_ | Terminale | Log e comandi. Scrivi e premi **Invia** |
| IO | GPIO | mode, uscita, PWM, ADC sui pin consentiti |
| WF | Wi-Fi | Scansione e credenziali |
| libri | Libreria | App HTML trovate in `/apps` sulla SD |
| ingranaggio | Impostazioni | Tema, accento, carattere, profilo, password |

In libreria e nel menu ci sono anche Note, Calcolatrice, Orologio, Tris, editor di codice, lettore PDF locale e le app di laboratorio (vedi `LAB.md`).

Tema, accento, sfondo e nome stanno in `/config.json` su LittleFS. La password, se la imposti, è un hash SHA-256: allo sblocco la pagina chiede la password prima del desktop. Sfondi: `pcb`, `grid`, `dusk`, `carbon`, `paper`, oppure un JPEG caricato (`/wallpaper.jpg`, max 200 KB). L’avatar è `/avatar.jpg`.

Le icone fissate su desktop e barra si ricordano nel browser (`localStorage`), non sulla scheda.

### App sulla microSD

Dal computer, copia `examples/apps` nella radice della scheda, così ottieni:

- `/apps/orologio.html` — un file singolo
- `/apps/snake/index.html` più `game.js` — una cartella

Regole:

- un file `/apps/nome.html`, oppure
- una cartella `/apps/nome/index.html` con CSS e JS relativi.

Helix le elenca e le apre in una finestra, servite da `/sd/apps/...`. Senza scheda l’elenco è vuoto e il resto del desktop funziona.

## Terminale

I comandi vanno in HTTP, non dipendono dal WebSocket. La risposta arriva entro circa un secondo.

| Comando | Effetto |
| --- | --- |
| `help` | elenco |
| `status` | versione, rete, IP, CPU, temperatura, OLED, SD |
| `free` | heap e PSRAM |
| `uptime` | secondi dall’avvio |
| `temp` | gradi del die |
| `version` | `1.0` |
| `whoami` | nome profilo |
| `echo …` | ripete il testo |
| `reboot` | riavvio |
| `log` | ultime righe di log |
| `ls [percorso]` | LittleFS |
| `cat <file>` | legge un file su LittleFS |
| `rm <percorso>` | cancella su LittleFS |
| `mkdir <percorso>` | cartella su LittleFS |
| `sd` | stato scheda |
| `apps` | elenco `/apps` |
| `sd-ls` `sd-cat` `sd-rm` `sd-mkdir` | come sopra, sulla SD |
| `wifi-scan` | chiede una scansione |
| `wifi-status` | modalità, SSID, IP |
| `wifi-connect SSID PASS` | salva e collega. SSID **senza spazi**: se il nome ha spazi, usa la pagina Wi-Fi |
| `wifi-ap on\|off` | riaccende o spegne il portale |
| `gpio` | stato di tutti i pin |
| `gpio N mode in\|out\|pwm\|adc\|free` | modo |
| `gpio N write 0\|1` | uscita |
| `gpio N pwm DUTY [Hz]` | duty 0–255 |
| `gpio N read` | lettura |

## Come è fatto

Due task FreeRTOS:

| Task | Core | Fa | Non fa |
| --- | --- | --- | --- |
| `net` | 0 | Wi-Fi, DNS del portale, invio WebSocket | non tocca i GPIO |
| `sys` | 1 | telemetria a 1 Hz, comandi, pin, SD, OLED ogni 500 ms | non chiama `WiFi.*`: legge uno snapshot |

Il server HTTP è asincrono sulla stessa radio. I callback non restano dentro le operazioni lunghe: accodano. Il WebSocket (`/ws`) esiste ancora, ma monitor, terminale, Wi-Fi e GPIO usano HTTP, perché sul telefono il socket spesso non si apre.

| Metodo | Percorso | Corpo |
| --- | --- | --- |
| GET | `/api/status` | telemetria JSON |
| GET | `/api/cli?since=N` | risposte del terminale dopo il numero N |
| POST | `/api/cli` | `line=status` in form-urlencoded |
| GET | `/api/wifi/scan` | `{scanning:true}` oppure l’elenco |
| POST | `/api/wifi/connect` | `ssid` e `pass` in form-urlencoded |
| POST | `/api/wifi/ap?on=1` | portale acceso o spento |
| POST | `/api/gpio` | `pin`, `op` (`mode`, `write`, `pwm`, `read`) e i campi del comando |
| GET | `/api/os` | tema, nome, se c’è una password |
| POST | `/api/os` | JSON di profilo e tema |
| POST | `/api/login` | `{ "pass": "..." }` |
| POST | `/api/os/wallpaper` | JPEG, max 200 KB |
| POST | `/api/os/avatar` | JPEG |
| GET/POST | `/api/fs/...` e `/api/sd/...` | lista, lettura, scrittura, upload, delete, mkdir |
| GET | `/api/sd/apps` | elenco `/apps` |
| GET | `/sd/...` | file della scheda |
| POST | `/api/reboot` | riavvio |

Una app nuova è un file in `data/plugins/` che chiama `HelixOS.register({...})` e va aggiunto a `plugins/index.json`. Il file è avvolto in una funzione: i nomi interni non devono uscire, perché gli script condividono la pagina. Il contratto è in testa a `data/kernel.js`.

L’anello di log (48 righe) e uno scratch da 32 KB stanno in PSRAM.

## Problemi frequenti

| Sintomo | Cosa fare |
| --- | --- |
| Pagina bianca o 404 su `kernel.js` | Manca `uploadfs`, oppure il telefono ha una copia vecchia: ricarica da Chrome su `http://192.168.4.1`. |
| Android non apre il sito | «Resta connesso» sulla rete Helix-S3, poi Chrome. La finestrella di accesso del sistema è limitata. |
| Scansione vuota | Avvicina la scheda al router. Controlla che il 2,4 GHz sia acceso. |
| OLED `solo 2.4GHz` | Quella rete non ha un SSID a 2,4 GHz, o il nome non coincide. |
| OLED `timeout nome` | Password sbagliata, rete spenta, o canale non trovato. Riscrivi le credenziali dalla pagina Wi-Fi. |
| Dopo il collegamento Helix-S3 sparisce | È previsto. Usa l’IP sull’OLED dalla rete di casa. |
| Monitor vuoto o terminale muto | Carica il filesystem della 1.0. Poi `status` e **Invia**. |
| GPIO sembra ignorare il primo tocco | Succedeva se lo stato vecchio cancellava il comando. Sulla 1.0 il comando è HTTP e per un secondo e mezzo il pannello non si fa sovrascrivere. |
| `browser-integration.js` in console | Estensione del browser, non è Helix. |
| Upload del firmware fallisce | Tieni BOOT, premi RESET, rilascia BOOT, riprova. Porta USB dati, non solo di carica. |
| La scheda resta sulla rete sbagliata | Dalla pagina Wi-Fi salva la rete giusta: sostituisce la NVS. |

## File da non confondere

`browser-integration.js`, errori di un’estensione e la richiesta di `favicon.ico` non sono file del progetto. I file del desktop sono solo quelli in `data/`.
