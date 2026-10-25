# Helix 1.0 — laboratorio

Otto app in `data/plugins/`. Il firmware che le esegue è `src/lab.cpp`. Il desktop e il primo avvio sono nel `README.md`. Pin vietati sull'N16R8: 26–37 (flash e PSRAM), 19–20 (USB), 8–9 (OLED), 10–13 (SD).

| App | Collegamento | Note |
|---|---|---|
| Oscilloscopio | GPIO 4 segnale, GND comune | Massimo 3,3 V. Per un segnale a 5 V: 10 kΩ dal segnale al GPIO 4 e 20 kΩ dal GPIO 4 a GND. ADS1115 opzionale su I2C 8/9, indirizzo 0x48. |
| Tester | GPIO 5 puntale, GND, 1 kΩ in serie; buzzer tra GPIO 6 e GND | Solo circuiti spenti. Livelli letti su 1, 7, 14, 15, 16, 21. |
| Seriale | RX GPIO 18, TX GPIO 17, GND | Solo 3,3 V. TX dell'ESP entra nel RX dell'altro. |
| Rete | nessuno | Station già connessa. Solo la /24 privata. |
| Router | nessuno | Station connessa. L'access point Helix-S3 viene sostituito finché non si preme stop. |
| Radio | nessuno | Beacon e RSSI. La station si stacca durante l'ascolto. |
| ESP-NOW | secondo ESP32, senza fili | Stesso canale Wi-Fi. Messaggi in chiaro. |
| Script | gli attuatori dello script | `set`, `pulse`, `beep`, `wait`, `if pin`, `now`, `log`. |

La scansione di rete non esce dalla LAN e prova solo le porte 80, 443, 22 e 8080 sugli host che rispondono al ping. La radio non decodifica il contenuto dei frame e non invia disassociazioni.
