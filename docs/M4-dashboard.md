# AQTRunCat — M4 Dashboard

M4 introduce la prima UI desktop di AQTRunCat: una Dashboard Qt Widgets
compatta, raggiungibile dalla tray, che presenta lo snapshot M3 senza duplicare
parsing o calcoli. La milestone non introduce funzionalità M5.

## 1. Architettura

Nel processo desktop esiste un solo `MetricSampler`, posseduto da
`runTrayApplication` e attivo per tutta la vita dell'applicazione:

```text
/proc + /sys + statvfs
          |
 SystemMetricsSampler       (src/metrics, C++ senza dipendenze Widgets)
          |
     MetricSampler          (QObject + QTimer, circa 1 Hz)
        /       \
       /         `----------------> DashboardWindow
      v                              CPU raw + snapshot M3
 CpuMonitor
      |
 EMA alpha 0.4 -> cpuToFps -> FpsStabilizer -> RunnerAnimator -> tray
```

`MetricSampler::start()` emette subito uno snapshot iniziale, che stabilisce le
baseline delle metriche a delta, e poi campiona con un `Qt::CoarseTimer`. Lo
start è idempotente. La visibilità della Dashboard non controlla né il sampler
né il runner; non esistono timer o polling propri della finestra.

La CLI rimane intenzionalmente separata: `aqtruncat metrics` crea il proprio
`SystemMetricsSampler` nel proprio processo. Non sono stati aggiunti daemon o
IPC.

## 2. Migrazione CPU del runner

`CpuMonitor` non legge più `/proc/stat` e non possiede più un timer. Consuma la
sezione CPU dello snapshot condiviso e mantiene esclusivamente l'EMA M1. Le
semantiche successive restano invariate:

```text
CPU raw M3 -> EMA(0.4) -> curva quadratica 10..40 -> stabilizzatore M1
```

Gli snapshot `Pending`, `Unavailable` o `Error` non producono un nuovo valore
per il runner. Il primo valore valido inizializza l'EMA come in M1. `--fps N`
continua a bypassare tutta la velocità CPU, mentre il sampler resta attivo per
la Dashboard.

## 3. Finestra e interazione tray

`DashboardWindow` è una singola istanza `Qt::Tool`, con
`WA_QuitOnClose=false`. La chiusura viene trasformata in hide; una successiva
apertura riusa la stessa finestra. `QApplication::setQuitOnLastWindowClosed`
rimane falso, quindi runner e sampler continuano dopo la chiusura.

- attivazione primaria (`QSystemTrayIcon::Trigger`): toggle show/hide;
- menu contestuale: `Show Dashboard` / `Hide Dashboard`, più il preesistente
  `Quit AQTRunCat`;
- `Quit` termina l'intera applicazione;
- richiesta iniziale 380 x 500 logical pixel, minimo 340 di larghezza; il
  size hint dello stile/layout prevale quando serve (380 x 613 osservati con
  Breeze e scaling 125%);
- nessun posizionamento tramite coordinate globali o API X11.

Su Plasma 6 Wayland Qt espone l'icona come StatusNotifierItem. L'attivazione
SNI è arrivata correttamente come `Trigger`; anche il fallback DBusMenu è stato
provato. Wayland non garantisce a una normale applicazione il posizionamento
esatto accanto alla tray: il monitor e la posizione sono quindi scelti dal
compositor. La finestra resta spostabile e viene aperta in una posizione
visibile secondo le regole standard di KWin.

## 4. Layout e presentazione

La Dashboard presenta, a circa 1 Hz:

- CPU raw aggregata, numero di CPU logiche e progress bar;
- memoria `used / total`, percentuale e indicazione della stima quando manca
  `MemAvailable`;
- swap separata, oppure `Disabled` quando il totale è zero;
- download/upload aggregati secondo la policy hardware-backed M3;
- capacità Root e, quando distinta, Home;
- read/write aggregati dei dischi fisici secondo la policy M3.

I valori usano `metricsformat` M3. Uno zero reale appare come `0 B/s`; uno stato
non disponibile appare come `N/A`, mentre la baseline iniziale appare come
`Waiting…`. Il fallimento di una sezione non nasconde le altre.

Le percentuali principali sono label testuali oltre che barre. Le barre hanno
nome e descrizione accessibile; nessuna informazione dipende soltanto dal
colore. Font, palette, progress bar e scaling provengono dallo stile Qt di
sistema: non sono presenti colori light/dark hardcoded né dimensioni pixel del
testo. Sul desktop di sviluppo con scaling Plasma 125% il layout è rimasto
leggibile e stabile durante il cambio dei valori.

## 5. Selezione Root/Home

Root è sempre richiesta. Home viene aggiunta soltanto quando `stat(2)` restituisce
per `$HOME` uno `st_dev` diverso da quello di `/`. Non si confrontano le stringhe
dei path e non si inferisce il mount dalla forma del nome. Se una delle due
identità non è determinabile, viene mantenuta la scelta conservativa di mostrare
solo Root. La funzione di lookup è iniettabile, così i test coprono in modo
deterministico filesystem uguale, diverso e lookup fallito.

La Dashboard usa le label `Root` e `Home`; per Home mostra `~`, evitando di
esporre il path personale. I calcoli restano quelli `df` di M3.

## 6. Test automatici

Sono state aggiunte tre suite:

- `desktopmetrics`: propagazione dello snapshot condiviso al consumer CPU,
  primo sample, CPU indisponibile e lifecycle idempotente del sampler;
- `storagepaths`: Root/Home sullo stesso filesystem, su filesystem distinti e
  lookup fallito;
- `dashboard`: stato iniziale, aggiornamenti di tutte le sezioni, zero reale,
  unavailable/pending, swap disabilitata, snapshot dal vero `MetricSampler` e
  show/hide/close/reopen della stessa istanza.

È inoltre coperta la regressione CLI in cui storage è l'unica metrica
disponibile: il comando deve riuscire, non dichiarare che tutte le metriche sono
assenti. I test Widgets usano il platform plugin offscreen e non dipendono da
Plasma.

Risultato finale: **20 suite, 317 casi Qt Test, 0 failure**.

## 7. Validazione Plasma 6 Wayland

Validazione effettuata nella sessione reale:

- avvio normale, runner animato e Dashboard aperta tramite Activate della tray;
- secondo toggle nasconde la finestra; il menu apre/nasconde e aggiorna la sua
  label; Quit chiude il processo;
- chiusura, riapertura e riuso della finestra sono coperti anche dal test del
  lifecycle, senza dipendere dal window manager;
- CPU Dashboard osservata fino al 100% sotto carico;
- RAM/swap plausibili; rete e disk I/O cambiano con attività reale;
- Root e Home entrambe presenti sulla macchina, dove `st_dev` è distinto;
- nessun flicker o salto di dimensione osservato.

Evidenza visiva ritagliata alla sola finestra:

- [Dashboard idle](M4-dashboard/dashboard-idle.png)
- [Dashboard sotto carico](M4-dashboard/dashboard-load.png)

Non è stato forzato un cambio del tema globale durante la sessione. Il codice
usa esclusivamente palette, font e stile Qt; il tema corrente e lo scaling 125%
sono stati verificati visivamente.

## 8. Regressione CPU/FPS e strumenti

`tools/test-cpu-runtime.sh` dopo la migrazione:

| Fase | CPU raw osservata | FPS applicati |
|---|---:|---:|
| idle | circa 17% | 11 |
| half load, tratto normale | circa 70–75% | 24–26 |
| full load | 100% | 39–40 |
| recovery | circa 17% | 11 |

Durante la fase half load un carico reale di background ha portato brevemente
la CPU raw a 81–95% e l'FPS a 31; il log mostra la corrispondenza e il recupero,
non una modifica della pipeline. Startup, primo sample, EMA e recovery sono
rimasti coerenti con M1–M3.

`test-cpu-fps-curve.sh --duration 1` ha prodotto nuovamente la sequenza
approvata 0→100→0 (`10, 10, 11, 13, 15, 18, 21, 25, 29, 34, 40, …`) usando
`--fps`; `cycle-runners.sh` ha mostrato cat e coffee; il tool metriche ha
completato baseline → CPU → disk write → recovery e ha saltato correttamente il
download in assenza di `--download-url`.

## 9. Performance

Su una finestra di misura di 15 secondi per stato:

| Stato | CPU processo, quota di un core | RSS |
|---|---:|---:|
| Dashboard nascosta | 0.467% | 58,980 KiB |
| Dashboard visibile | 0.467% | 64,528 KiB |

La finestra aggiunge circa 5.4 MiB RSS e nessun incremento CPU misurabile in
questa prova. Il sampler resta a 1 Hz; l'animazione continua separatamente a
10–40 FPS.

## 10. Limitazioni note

- Su Wayland la finestra non viene ancorata geometricamente alla tray; si usa
  il posizionamento affidabile del compositor.
- `Qt::Tool` evita normalmente una voce applicativa invasiva, ma la politica
  esatta di task switcher/taskbar resta del compositor.
- M4 mostra solo metriche aggregate e Root/Home. Selezione delle interfacce,
  filesystem arbitrari, Settings, runner persistente e slow-down restano M5 o
  milestone successive.
- La Dashboard non contiene grafici storici né metriche per-core/per-device,
  coerentemente con l'obiettivo quick system glance.
