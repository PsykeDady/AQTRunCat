# AQTRunCat — M0 Upstream Analysis

> Milestone M0. Analisi condotta **sul codice sorgente**, non sui README.
> Data: 2026-10-01.

| Progetto | Revisione analizzata | Licenza |
|---|---|---|
| RunCat Neo (`runcat-dev/RunCatNeo`) | `b3b1543` (2026-08-06) | Apache-2.0 |
| SystemInfoKit (`Kyome22/SystemInfoKit`, dipendenza di RunCat Neo, pin `7.2.2`) | `56bfde2` | Apache-2.0 |
| Runner Gallery (`runcat-dev/RunnerGallery`) | `b979658` (2026-08-20) | Apache-2.0 |
| runcat-tray (`chux0519/runcat-tray`) | `9fe316b` (2024-04-23) | BSD-2-Clause |

Ambiente di riferimento locale: Qt 6.11.2, Plasma 6.7.5, sessione Wayland.

> **Nota:** `requirements.md` nel workspace è troncato
> (`[The remainder of REQUIREMENTS.md stays unchanged.]`) e non contiene la
> definizione formale di M0/M1. Questo documento segue lo scope M0 descritto
> nel brief di sessione. Le proposte M1 al §10 vanno confrontate con il testo
> completo dei requisiti.

Notazione percorsi: `DS/` = `LocalPackage/Sources/DataSource/`, `M/` =
`LocalPackage/Sources/Model/`, `UI/` = `LocalPackage/Sources/UserInterface/`,
`SIK/` = `SystemInfoKit/Sources/SystemInfoKit/`.

---

## 1. Funzionalità di RunCat Neo (stato reale del codice)

App macOS 26+, SwiftUI, App Store, sandboxed (`ENABLE_APP_SANDBOX = YES`),
agente senza Dock (`LSUIElement = YES`). Architettura LUCA a tre livelli
(`DataSource → Model → UserInterface`).

Scene (`RunCatNeo/RunCatNeoApp.swift`):

1. **RunnerBar**: status item con il runner animato. Il click apre la
   **Dashboard** (`MenuBarExtra`, stile `.window`).
2. **MetricsBar**: secondo status item, opzionale (off di default), che mostra
   testo e icone delle metriche. Il click apre un pannello di toggle.
3. **Settings**: finestra a tab General / Runner / Metrics / Donation.
4. **Open Source Licenses**: finestra.

Elenco funzionale:

| Area | Funzione | Dove |
|---|---|---|
| Runner | 8 runner built-in: cat, dog, slime, drop, coffee, newton-cradle, engine, mochi | `DS/Entities/Runner/RunnerKind.swift` |
| Runner | Velocità proporzionale alla CPU, opzione "Slow down under load" (inversa) | `M/Services/RunnerService.swift` |
| Runner | Flip orizzontale | `M/Stores/RunnerBar.swift`, `M/Extensions/NSImage+Extension.swift` |
| Runner | Rendering monocromatico (template tinto col colore della menubar) o a colori | `UI/Views/RunnerBar/RunnerLayer.swift` |
| Runner | Selezione del runner da menu della Dashboard (sottomenu "Runner" con thumbnail) | `UI/Views/RunnerBar/Dashboard/MenuView.swift` |
| Custom runner | Editor: nome, rendering mode, import PNG (file picker o drag&drop), riordino/cancellazione frame, anteprima con velocità regolabile | `M/Stores/CustomRunnerSettings.swift` |
| Custom runner | Lista con riordino e cancellazione (non quello in uso) | idem |
| Runner Gallery | Solo un link informativo (popover "Guidance"). **Nessun download o installazione dall'app** | `M/Extensions/URL+Extension.swift` |
| Metriche | CPU (sempre attiva), Memory, Storage, Battery, Network (attivabili singolarmente) | `M/Services/SystemMetricsService.swift` + SystemInfoKit |
| Metriche | Intervallo di aggiornamento 3 / 5 / 10 s (default 5) | `DS/Entities/Metrics/UpdateInterval.swift` |
| Dashboard | Card di sistema con riepilogo e dettagli, grafico storico (61 campioni) per CPU e memoria, barra per storage | `UI/Views/RunnerBar/Dashboard/*` |
| Dashboard | Card Custom Metrics | `CustomMetricsCardView.swift` |
| Dashboard menu | Runner, Settings, Open Activity Monitor, About, OSS Licenses, Report Issue, Quit (più Debug Sleep/Wake nelle build debug) | `MenuView.swift` |
| Metrics Bar | Testo per CPU/Mem/Storage/Battery/Network e per ogni custom source abilitata; icona "sleeping cat" se tutto è spento | `UI/Views/MetricsBar/MetricsBarView.swift` |
| Custom Metrics | Osservazione di file JSON scelti dall'utente, rendering a card e nella Metrics Bar | `M/Services/CustomMetricsService.swift` |
| General | Launch at login (`SMAppService`) | `DS/Repositories/LaunchAtLoginRepository.swift` |
| Sistema | Stop del monitoraggio su sleep e restart su wake (`NSWorkspace` will-sleep / did-wake) | `M/AppDelegate.swift` |
| Donation | StoreKit (tip / subscription) | Non rilevante per Linux |
| i18n | 10 lingue (String Catalog) | `UI/Resources/*.xcstrings` |

Non presenti, contrariamente a quanto si potrebbe assumere: nessuna
temperatura CPU/GPU, nessun dato per core, nessun dettaglio sui processi,
nessuna notifica o soglia, nessuna esportazione o importazione dei runner,
nessun hotkey, nessun download dalla Gallery dentro l'app.

---

## 2. Funzionalità di runcat-tray

Un solo file, `main.c` (282 righe), GTK3 + `libappindicator3`.

- Tray icon via AppIndicator, che su Plasma passa per StatusNotifierItem.
- Animazione: a ogni tick `app_indicator_set_icon(<percorso assoluto PNG>)`,
  quindi l'host ricarica il file dal disco a ogni frame.
- Frame letti da `~/.config/runcat/icons/<mode>/`, ordinati con `strcmp`.
- 4 set di icone fissi nel codice: `cat`, `dab`, `mona`, `partyblobcat`. Un
  radio-menu "mode" cambia set.
- Menu: mode, una voce passiva `cpu: NN.NN%`, quit.
- CLI: `-l` FPS minimo (default 6), `-u` FPS massimo (default 90), `-d`
  directory delle icone.
- CPU letta da `/proc/stat` ogni 1 s.
- Nessuna persistenza, nessuna impostazione, nessuna altra metrica.

Calcoli reali:

```c
// CPU: somma solo user+nice+system, presume USER_HZ=100 e intervallo esatto di 1 s
percent = (Σ_t1 − Σ_t0) / 100 / num_cores * 100
// FPS: è il periodo, non gli FPS, a essere interpolato linearmente
period_ms = 1000/FPS_L − ((1000/FPS_L − 1000/FPS_H)/100) * percent   // 166.7 ms → 11.1 ms
```

Difetti trovati nel codice:

- idle viene implicitamente escluso, ma anche `iowait/irq/softirq/steal`
  spariscono dal totale. La stima regge solo con USER_HZ=100 e un timer
  puntuale.
- Il primo `g_timeout_add(1.0 / FPS_L, …)` vale 0 ms (troncamento).
- Più di 30 file nella directory causano un buffer overflow (`FRAMES[30]`
  senza controllo).
- L'ordinamento con `strcmp` mette `frame10` prima di `frame2`.
- L'help indica `-h` per il massimo, ma il codice usa `-u`.
- Una directory mancante provoca `exit(-1)`. Un `-d` con nome non noto dà
  `MODE = -1`.

Cosa resta valido: la scelta SNI/AppIndicator per la tray, la lettura di
`/proc/stat`, l'idea di FPS legati alla CPU. Il resto non va riusato.

---

## 3. Gap: funzionalità di RunCat Neo assenti in runcat-tray

| RunCat Neo | runcat-tray |
|---|---|
| Velocità = f(CPU) con mappatura definita (§4.3), modalità inversa | Solo diretta, mappatura diversa |
| Frame order (ascending/swing/pendulum/partyHorn/custom) | No, solo ordine alfabetico |
| Rendering monocromatico tinto in base al tema | No (PNG così come sono) |
| Flip orizzontale | No |
| Custom runner gestiti (import, validazione, riordino, delete, persistenza) | Solo "metti PNG in una directory" + `-d` |
| Selezione runner persistente | No |
| Dashboard con metriche e grafici | Solo voce di menu `cpu: x%` |
| Memory, Storage, Battery, Network | No |
| Metrics Bar | No |
| Custom Metrics (JSON + file watching) | No |
| Update interval configurabile | No (1 s fisso) |
| Launch at login | No |
| Sleep/wake handling | No |
| Settings UI | No |
| i18n | No |

In pratica runcat-tray copre circa il 5% di RunCat Neo: tray più animazione
CPU-driven.

---

## 4. Funzionamento interno di RunCat Neo (parti rilevanti)

### 4.1 Flusso dati

```
SystemInfoObserver (SIK, Task periodico ogni N s)
  └─ AsyncStream<SystemInfoBundle>
       └─ AppDelegate: for await value
            ├─ SystemMetricsService.updateMetrics → AppState.metrics (+ ring buffer CPU/Mem)
            └─ RunnerService.updateRunnerSpeed(cpuInfo) → AppState.runnerSpeeds
CustomMetricsService: un Task per sorgente → watch del file → AppState.metrics.customMetricsBundles
Stores (RunnerBar, Dashboard, MetricsBar) consumano gli stream (buffer "latest 1")
```

Lo stato globale è `AppState` (`DS/Entities/AppState.swift`), dietro un lock.
Gli stream sono `AsyncStreamBundle` con `bufferingNewest(1)` e conservano
`latestValue` per i consumer che arrivano dopo.

### 4.2 Runner: formato, asset, frame

**Built-in** (`RunnerKind.swift`, asset in `UI/Resources/Media.xcassets/Runners/<Name>/`):

| id | PNG | dimensione px | frameOrder |
|---|---|---|---|
| cat | 5 | 56×36 | ascending → 0..4 |
| dog | 5 | 70×36 | ascending |
| slime | 5 | 61×36 | partyHorn → `0,1,2,3,4,4,3,2,1` |
| drop | 5 | 43×36 | ascending |
| coffee | 10 | 38×36 | ascending |
| newton-cradle | 5 | 82×36 | pendulum → `0,1,2,1,0,3,4,3` |
| engine | 10 | 83×36 | ascending |
| mochi | 5 | 62×36 | swing → `0,1,2,3,4,3,2,1` |

- Nomi file: `<id>-frame-<N>.png`, RGBA, nero su trasparente. Il gatto
  guarda verso destra.
- I built-in hanno sempre `isTemplate = true` (monocromatici).
- `FrameOrder` è una sequenza di indici e può ripetere frame. La sequenza
  espansa (es. 8 elementi per mochi) è ciò che viene animato.
- Sono 36 px fisici di altezza: `NSImage.normalize()` scala ad altezza
  **18 pt** (immagine @2x). La larghezza è libera.

**Custom** (`CustomRunnerSettings.appendFrameImage`, `RunnerService.add`):

- Validazione all'import: PNG, **altezza esattamente 36 px**, **larghezza
  10–100 px**. Non controlla che i frame abbiano tutti la stessa dimensione.
- Limite frame: il controllo `frameImages.count <= 30` avviene *prima*
  dell'append, quindi passano **31 frame** (off-by-one).
- I file trascinati vengono ordinati con `localizedStandardCompare`
  (ordinamento naturale, `frame-10` dopo `frame-9`). Con il file picker
  restano nell'ordine di selezione.
- Ogni frame viene ri-codificato in PNG (`CGImageDestination`) e salvato.
- `frameOrder` è sempre `0..n-1` (`.custom(indices)`): **l'editor non
  permette swing/pendulum**. Le ripetizioni vanno fatte duplicando i PNG.
- Rendering mode scelto dall'utente: Monochrome (template) o Color.
- Il nome deve essere unico (`nameAlreadyExists`). L'id è un UUID.
- Non si può cancellare il runner in uso (`runnerInUse`).

Persistenza su disco (sandbox):
`~/Library/Containers/<bundle-id>/Data/Library/Application Support/RunCatNeo/`

```
RunCatNeo/
├── CUSTOM_RUNNERS.json          # array ordinato (l'ordine è quello mostrato in UI)
└── <UUID>/
    ├── frame-0.png
    ├── frame-1.png
    └── …
```

`CUSTOM_RUNNERS.json` (JSONEncoder, `sortedKeys`):

```json
[{"frameOrder":[0,1,2,3,4],"id":"6F1C…","isTemplate":true,"name":"My Runner"}]
```

Il runner selezionato sta in UserDefaults `RUNNER_ID` (id built-in o UUID). Se
manca o si rompe, si torna al fallback `cat`.

### 4.3 Animazione e relazione CPU → velocità/FPS

Codice: `RunnerService.updateRunnerSpeed` e `UI/Views/RunnerBar/RunnerLayer.swift`.

```swift
cpuValue = clamp(cpu.percentage.value / 5.0, 1.0, 20.0)   // cpu in [0, 99.9]
speed    = speedDecreasesUnderLoad ? 0.5 * (21.0 - cpuValue) : cpuValue
```

L'animazione è un `CAKeyframeAnimation` discreto con
`duration = frames.count / 2.0` a `speed = 1`, cioè **2 frame/s base**. La
velocità del layer (`CALayer.speed`) moltiplica:

```
fps = 2 × speed
```

| CPU | speed (normale) | fps | speed (inverso) | fps |
|---|---|---|---|---|
| 0–5% | 1 | 2 | 10 | 20 |
| 25% | 5 | 10 | 8 | 16 |
| 50% | 10 | 20 | 5.5 | 11 |
| 75% | 15 | 30 | 3 | 6 |
| ≥99.9% | 19.98 | ≈40 | 0.51 | ≈1 |

Proprietà importanti da replicare:

- **Lineare negli FPS** (runcat-tray è lineare nel periodo).
- La velocità cambia **solo a ogni campione** (3/5/10 s) e a gradino, senza
  smoothing.
- **Continuità di fase**: `setSpeed` imposta `timeOffset`/`beginTime`, così il
  frame corrente non salta quando cambia la velocità.
- Il primo campione CPU di SystemInfoKit è la media dal boot, perché
  `previousLoadInfo` parte da zero.

Il frame rate dell'anteprima nell'editor è un'altra cosa: un intervallo di
`0.2 − 0.05 × previewSpeed` s.

**Tecnica di rendering macOS:** un layer Core Animation viene iniettato dentro
`NSStatusBarButton`. Per la modalità template si usa un `maskLayer` animato su
uno sfondo pieno di colore `NSColor.textColor`. Un commento del codice spiega
la scelta: cambiare l'icona frame per frame costava il 7–8% di CPU contro lo
0,1% del layer. Sugli altri monitor resta un'immagine statica di fallback
(hack `sourceOutCompositing`, solo su arm64).

### 4.4 Orientamento

Esiste solo il **flip orizzontale** (`IS_FLIPPED_HORIZONTALLY`). L'effetto è
uno specchio, ottenuto con `CIFilter.perspectiveRotate(pitch: π)`. Nessuna
rotazione, nessun orientamento verticale, nessuna gestione di pannelli
verticali (non esistono su macOS).

### 4.5 Runner Gallery

Sito statico (`index.html` + `runners/manifest.json`), 44 runner.

```
runners/<name>/
├── <name>-frames.zip   # contiene <name>-frames/<name>-frame-N.png
├── metadata.json       # {"author","displayName","type":"monochrome|color","tags":[...]}
└── preview.png         # APNG
```

Regole (CONTRIBUTING + `.github/scripts/validate_runners.py`): nome
kebab-case; PNG non interlacciati, altezza 36, larghezza 10–100, **tutti della
stessa dimensione**, almeno 2 frame, numerati in modo consecutivo da 0; zip
≤ 2 MB, ≤ 64 entry, solo PNG validi (CRC verificato, nessun byte dopo IEND);
`metadata.json` ≤ 4 KB. Tag ammessi: `animal`, `dog`, `object`, `mechanism`,
`fitness`.

Integrazione in RunCat Neo: **nessuna**. L'utente scarica lo zip, lo
decomprime e importa i PNG nell'editor, scegliendo a mano il rendering mode
(che corrisponde a `type` nel metadata). La Gallery **non ha `frameOrder`**: i
frame si animano 0..n-1.

### 4.6 Installazione/rimozione runner

- **Installazione**: solo tramite l'editor (copia e ri-codifica dei PNG
  nell'Application Support). Nessun formato di pacchetto, nessuna directory
  osservata.
- **Rimozione**: si toglie l'elemento da `CUSTOM_RUNNERS.json` e si cancella
  la directory `<UUID>/`.
- **Riordino**: si riscrive l'array.

### 4.7 AQTRunCat può caricare runner compatibili RunCat Neo?

**Sì, con facilità.** Il formato effettivo è "N PNG alti 36 px + flag
monochrome/color + sequenza di indici". In dettaglio:

- **Gallery zip / directory `<name>-frames/`**: compatibilità completa e
  diretta. `metadata.json` fornisce displayName, author e type
  (`monochrome` → template).
- **Directory interna di RunCat Neo** (`CUSTOM_RUNNERS.json` + `<UUID>/frame-N.png`):
  importabile se l'utente copia la cartella dal Mac. Formato banale.
- **Built-in**: riutilizzabili come asset (vedi §8 sulla licenza), con il
  `frameOrder` preso da `RunnerKind.swift`.

---

## 5. Metriche: semantica reale, sorgenti, equivalenti Linux

Tutte le metriche di sistema vengono da **SystemInfoKit**. Un unico Task le
campiona tutte ogni `max(interval, 1)` s; l'intervallo è 3/5/10 s, default 5.
Le metriche disattivate valgono `nil` e spariscono dalla UI. La CPU è sempre
attiva perché pilota il runner. Il valore percentuale viene arrotondato a
0,1 (`Percentage.value = round(1000·raw)/10`) e limitato a **99,9%** (non
arriva mai a 100).

Storico: `RingBuffer(length: 61)` inizializzato a 2. Il grafico disegna
2 px per campione su 120 px e limita i valori a [2, 100]. Copre 61 campioni,
cioè 5 minuti al default. Esiste solo per CPU e memoria.

Formattazione nella Metrics Bar (`Percentage+Extension`, `ByteData+Extension`):
percentuale `"%4.1f%%"` (`"%4.0f%%"` se ≥ 100), rete `"%4.1f %@/s"` con unità
`MeasurementFormatter` in scala naturale. Le stringhe sono su due righe a
7 pt (⬆ upload, ⬇ download).

### 5.1 CPU

| | |
|---|---|
| Sorgente macOS | `host_statistics64(HOST_CPU_LOAD_INFO)` → tick aggregati user/system/idle/nice |
| Calcolo | Δ tra due campioni. `total = user+system+idle+nice`; **`percentage = (user+system)/total`** (min 0,999). Dettagli: System, User, Idle |
| Nota | **nice è nel denominatore ma non nel numeratore**: il carico niced non conta come "occupato" |
| UI | Card "CPU: x%" con System/User/Idle e grafico; Metrics Bar `xx.x%`; pilota il runner |
| Linux | `/proc/stat`, riga `cpu`: `user nice system idle iowait irq softirq steal`. **Proposta**: `busy = total − idle − iowait` (semantica htop/plasma-systemmonitor), con un'opzione o costante "upstream-compatible" che esclude nice. Δ diviso per il Δ reale dei tick, senza assumere USER_HZ né l'intervallo nominale. Dettagli: System = system+irq+softirq, User = user+nice, Idle = idle+iowait |

### 5.2 Memory ("Memory Usage", non "pressure": rinominata nel commit `f05bb3b`)

| | |
|---|---|
| Sorgente macOS | `host_statistics64(HOST_VM_INFO64)`, `host_page_size`, `HOST_BASIC_INFO.max_mem` |
| Calcolo | `app = active+inactive+speculative − (purgeable+external)`; `pressure = wired+compressed`; **`percentage = (app+wired+compressed)·page / max_mem`** (equivale a "Memory Used" di Activity Monitor, cache esclusa) |
| Dettagli | "Pressure" = (wired+compressed)/max_mem (**non** è la memory pressure del kernel), App Memory, Wired Memory, Compressed |
| UI | Card + grafico; Metrics Bar `xx.x%` |
| Linux | `/proc/meminfo`: **`used = MemTotal − MemAvailable`**, percentuale su MemTotal: stessa intenzione (cache e reclaimable esclusi). Dettagli nativi suggeriti: App ≈ `AnonPages` (o Active(anon)+Inactive(anon)), Kernel/"Wired" ≈ `SUnreclaim+KernelStack+PageTables+Unevictable`, Compressed ≈ `Zswap` (meminfo) o zram (`/sys/block/zram*/mm_stat`), più Swap used. Per una vera "pressure" si può usare PSI `/proc/pressure/memory` (avg10), da etichettare diversamente da upstream |

### 5.3 Storage

| | |
|---|---|
| Sorgente macOS | `URL("/").resourceValues([.volumeTotalCapacityKey, .volumeAvailableCapacityForImportantUsageKey])` |
| Calcolo | `used = total − availableForImportantUsage` (questo "available" include lo spazio purgeable); percentage = used/total (min 0,999) |
| UI | Card "Storage: x% used", dettaglio `used / total` (`--- / ---` se 0), barra; Metrics Bar `xx.x%` |
| Linux | `statvfs` / `QStorageInfo` sul volume di `/` (scelta compatibile; sarebbe utile anche una variante su `$HOME`). `available = f_bavail·f_frsize` (spazio riservato a root escluso). Su btrfs i valori `statvfs` sono approssimati: da documentare |

### 5.4 Battery

| | |
|---|---|
| Sorgente macOS | IOKit `AppleSmartBattery` (+ `AppleSmartBatteryPack` su macOS 27) |
| Calcolo | percentage = CurrentCapacity/MaxCapacity; maxCapacity = RawMax/Design (salute); cycleCount; temperatura/100 °C; isCharging; nome alimentatore o "NN W Power Adapter" |
| UI | Card "Battery: x%" con Power Source, Max Capacity, Cycle Count, Temperature. Icona `battery.{0,25,50,75,100}percent` o `.bolt` in carica, `powerplug` senza batteria. Metrics Bar: % oppure solo icona se non c'è batteria |
| Linux | **UPower via D-Bus** (`org.freedesktop.UPower`, `DisplayDevice` e device batteria: `Percentage`, `State`, `Capacity` (salute), `Temperature`, `ChargeCycles` (UPower ≥ 0.99.18), `IsPresent`), con segnali di variazione. Fallback `/sys/class/power_supply/BAT*` (`capacity`, `status`, `energy_full`/`energy_full_design` o `charge_*`, `cycle_count`; `temp` raro). Nome e potenza dell'alimentatore di norma non sono disponibili (AC `online` sì; USB-PD a volte via `ucsi`): mostrare "AC adapter" |

### 5.5 Network

| | |
|---|---|
| Sorgente macOS | `NWPathMonitor` (stato, tipo interfaccia primaria, nome) + `getifaddrs` (IP IPv4 dell'interfaccia primaria; contatori `if_data` AF_LINK) |
| Calcolo | Upload/download = Δ(Σ `ifi_obytes`/`ifi_ibytes` di **tutte** le interfacce AF_LINK, **loopback inclusa**) / **intervallo nominale** (non il tempo reale trascorso). Primo campione = 0 |
| UI | Card "Network: Wi-Fi/Ethernet/Cellular/…/No Connection", Local IP, Upload, Download; Metrics Bar ⬆/⬇ |
| Linux | Contatori in `/proc/net/dev` o `/sys/class/net/*/statistics/{rx,tx}_bytes`. **Escludere `lo`** e preferibilmente le interfacce virtuali (veth, docker, bridge, tun opzionale). Dividere per il Δt misurato (`QElapsedTimer`). Stato/tipo: `QNetworkInformation` (backend NetworkManager: reachability + transportMedium) o direttamente NetworkManager D-Bus (`PrimaryConnection`). IP con `QNetworkInterface` sull'interfaccia della default route (`/proc/net/route`) |

### 5.6 Cosa fa "Open Activity Monitor"

Apre `com.apple.ActivityMonitor`. Su Linux: comando configurabile, con
fallback in ordine `plasma-systemmonitor`, `gnome-system-monitor`, `xdg-open`.

---

## 6. Custom Metrics (ricostruzione precisa)

### 6.1 Schema JSON (`DS/Entities/CustomMetrics/*.swift`)

```jsonc
{
  "title": "Claude Code",            // string, OBBLIGATORIO
  "symbol": "staroflife",            // string|null, opzionale: nome SF Symbol; default "chart.bar.horizontal.page.fill"
  "metricsBarValue": "5.4%",         // string|null, opzionale: testo nella Metrics Bar; assente → "---"
  "metrics": [                       // array, OBBLIGATORIO (anche vuoto)
    { "title": "Model", "formattedValue": "Opus 4.7" },                          // title, formattedValue obbligatori
    { "title": "Context", "formattedValue": "5.4%", "normalizedValue": 0.054 }   // normalizedValue: number|null opzionale
  ],
  "lastUpdatedDate": "2026-06-05T04:50:40Z"   // string ISO 8601, OBBLIGATORIO
}
```

Dettagli di decodifica (`JSONDecoder` + `dateDecodingStrategy = .iso8601`):

- **`.iso8601` di Foundation NON accetta frazioni di secondo**:
  `"2026-06-05T04:50:40.123Z"` produce `invalidFormat`. Gli offset tipo
  `+09:00` sono accettati. Per essere compatibile al 100% con upstream un
  parser deve accettare almeno ciò che accetta upstream. AQTRunCat può essere
  più permissivo (frazioni ammesse) senza rompere la compatibilità.
- Le chiavi sconosciute vengono ignorate. Gli optional accettano `null`. JSON
  strict (niente commenti né virgole finali, per scelta documentata).
- `normalizedValue` viene limitato a [0, 1] solo in fase di disegno.
- **Non ci sono** campi per colore, unità, icone custom o severità. Le unità
  sono responsabilità del producer (`formattedValue` mostrato così com'è); la
  barra usa sempre l'accent color.

### 6.2 Sorgenti, directory, naming

- **Nessuna directory osservata e nessuna convenzione di nome.** L'utente
  aggiunge **singoli file** con un file picker
  (Settings → Metrics → Custom Metrics → *Add Custom Metrics Source*). Il
  file viene validato subito (`fileUnreadable` / `invalidFormat` → alert).
- Ogni sorgente si salva in UserDefaults `CUSTOM_METRICS_CONFIGURATION` come
  `{sources: [{id: UUID, displayName (= title al momento dell'aggiunta), symbol, fileURL, bookmark (security-scoped), createdAt}]}`.
  L'ordine è riordinabile e determina l'ordine delle card.
- Rimozione con conferma ("il file su disco non viene cancellato"). Toglie
  anche la sorgente dalla Metrics Bar.
- Visibilità nella Metrics Bar: per sorgente, `METRICS_BAR_CONFIGURATION.visibleCustomMetricsSourceIDs`,
  default nascosta.
- Le etichette in Settings e nel toggle della Metrics Bar usano `displayName`
  **congelato all'aggiunta**. La card usa invece il `title` corrente del file.

### 6.3 Aggiornamento e filesystem watching (`CustomMetricsService.makeObserver`, `FileWatcherClient`)

```
loop finché non cancellato:
  risolvi bookmark (refresh se stale) → startAccessingSecurityScopedResource
    fallisce → emitFailure, sleep 5 s, retry
  loadSnapshot()                       # lettura iniziale
  watch(file): open(O_EVTONLY) + DispatchSource eventi [.write, .rename, .delete, .extend]
    ogni evento → loadSnapshot()
    su rename/delete → lo stream termina
    (se open() fallisce lo stream termina subito)
  sleep 200 ms, poi riapri da capo     # gestisce il pattern write-tmp + mv atomico
  errori → emitFailure, sleep 5 s
```

- Lo stream usa `bufferingNewest(1)`: una raffica di scritture si riduce a
  una sola rilettura.
- Nessun polling: se il file non cambia, non succede nulla.
- Il ciclo di riconciliazione (`reconcile`) aggiunge e rimuove gli observer
  quando cambia la configurazione.
- Sleep/wake: `stopMonitoring` (svuota le card) / `startMonitoring`.

### 6.4 Gestione errori

| Caso | Effetto |
|---|---|
| File illeggibile, cancellato, permesso revocato | Snapshot precedente mantenuto, `isFailed = true`; retry ogni 5 s |
| JSON invalido | Idem, ma il watch resta attivo: recupero alla scrittura successiva |
| Nessuno snapshot precedente | Card sintetica con title/symbol salvati e `lastUpdatedDate = createdAt`, segnata failed |
| UI Dashboard | Footer "Last updated: **Failed**" in rosso |
| UI Settings | "⚠︎ Error Detected" accanto al path |
| UI Metrics Bar | Simbolo invariato, valore `---` |

Il tempo relativo ("3 min ago", `Date.AnchoredRelativeFormatStyle`) viene
ricalcolato a ogni apertura della Dashboard (commit `bb3265f`), non con un
timer.

### 6.5 Rendering

- **Card**: icona SF Symbol 24×24, title, per ogni metrica `"title: formattedValue"`
  (caption) e, se c'è `normalizedValue`, una barra 120×8. Footer
  "Last updated: …".
- **Metrics Bar**: icona 22×16, `metricsBarValue` monospaced-digit, larghezza
  misurata + 4 px con un massimo di **80 pt** (troncamento).

### 6.6 Producer di esempio (`docs/samples/`)

| Sample | Trigger | Output | Contenuto |
|---|---|---|---|
| `claude-code/runcat-statusline.py` | **Claude Code `statusLine` command** (legge il JSON da stdin) | `~/.claude/runcat-usage.json` (`RUNCAT_OUT_FILE`) | Model (`model.display_name`), Context (`context_window.used_percentage`), 5h/7d (`rate_limits.five_hour/seven_day.used_percentage` + `resets_at` → `~HH:MM` oppure `~M/D HH:MM`); `metricsBarValue` = context; symbol `staroflife`. Stampa il nome del modello come status line |
| `codex/runcat-hook.py` | **Codex lifecycle hook `Stop`** (`~/.codex/hooks.json`) | `~/.codex/runcat-usage.json` | Legge l'ultimo evento `token_count` dal transcript `.jsonl`: context = `last_token_usage.total_tokens / model_context_window`; rate limit `primary`/`secondary` con titolo da `window_minutes` (5h/7d/…). Non sovrascrive uno snapshot valido con dati incompleti. Esce sempre 0 e stampa `{}` |
| `bitcoin/update-bitcoin.sh` | `launchd` LaunchAgent ogni 600 s | `~/.runcat/bitcoin.json` | Prezzo da CoinGecko, `metricsBarValue` `$61.9K` |

Tutti scrivono in modo atomico (tmp nella stessa directory + `os.replace`/`mv`).
**Sono portabili su Linux così come sono**, salvo:
(a) i path di esempio `/Users/YOU` → `$HOME`;
(b) il `launchd` plist → **systemd user timer** (oppure cron);
(c) `stat -f` nel troubleshooting Codex è BSD → `stat -c`;
(d) i `symbol` sono nomi SF Symbol (vedi §7).

---

## 7. Componenti macOS-specifici ed equivalenti Linux/Qt

| macOS (RunCat Neo) | Equivalente consigliato in AQTRunCat |
|---|---|
| `MenuBarExtra` / `NSStatusItem` | `QSystemTrayIcon`, che su Linux con `org.kde.StatusNotifierWatcher` usa **StatusNotifierItem** via D-Bus (implementazione interna di Qt `QDBusTrayIcon`). Niente libappindicator, niente dipendenze KDE |
| Layer CA dentro il bottone (animazione a costo ~0) | Non esiste: su SNI ogni frame è un nuovo `IconPixmap` inviato via D-Bus (`NewIcon`). Rendering frame in `QPixmap` precalcolati, `QTimer` con continuità di fase, **limite FPS** (§9) |
| Template image tinta con `NSColor.textColor` | Ricolorare l'alpha mask con il colore del testo del pannello: da `QPalette` (con plasma-integration riflette lo schema KDE) più un override utente Auto/Chiaro/Scuro. Ricolorare a ogni cambio di palette (`QEvent::ApplicationPaletteChange` / `QStyleHints::colorScheme`) |
| Dashboard (`MenuBarExtra` window) | Popup `QWidget`/QML. Su Wayland non si possono posizionare finestre: vedi §9. Menu contestuale: `QMenu` esportato via DBusMenu |
| Metrics Bar (secondo status item testuale) | Secondo `QSystemTrayIcon` con testo **renderizzato nel pixmap** (SNI non ha label), con i limiti del §9. Alternativa a fedeltà piena: un plasmoid opzionale |
| SF Symbols (`symbol` delle custom metrics, icone di sistema) | Icone del tema freedesktop (`QIcon::fromTheme`) con una **tabella di mapping SF Symbol → nome freedesktop** per i simboli comuni (`cpu`, `memorychip`, `internaldrive`, `battery.*`, `network`, `staroflife`, `bitcoinsign`, `chart.bar.horizontal.page.fill`, …), fallback generico. Estensione possibile: accettare anche nomi icona freedesktop o un path |
| UserDefaults | `QSettings` INI in `~/.config/AQTRunCat/aqtruncat.conf` (`QStandardPaths::AppConfigLocation`) |
| Application Support + sandbox container | `~/.local/share/AQTRunCat/` (`AppDataLocation`): `runners/<id>/…` |
| Security-scoped bookmarks | Non servono (path assoluti). Sotto Flatpak vedi §9 |
| `DispatchSource` file watch | `QFileSystemWatcher` (inotify) sul **file e sulla directory padre**, re-add dopo rename/replace, debounce ~100 ms, retry 5 s se manca |
| `SMAppService` (login item) | XDG autostart `~/.config/autostart/aqtruncat.desktop` (Plasma 6 lo rispetta; con systemd passa da `xdg-autostart-generator`) |
| `NSWorkspace` will-sleep / did-wake | logind D-Bus `org.freedesktop.login1.Manager.PrepareForSleep(bool)` |
| `host_statistics64`, IOKit, NWPathMonitor, getifaddrs | `/proc/stat`, `/proc/meminfo`, `statvfs`, UPower D-Bus / sysfs, `/proc/net/dev` + `QNetworkInformation` |
| Activity Monitor | Comando configurabile (`plasma-systemmonitor` …) |
| About / OSS licenses window | `QMessageBox::about` / dialog con le licenze (Apache-2.0 upstream asset) |
| StoreKit donation | Fuori scope (eventuale link) |
| String Catalog `.xcstrings` | Qt Linguist `.ts`. Le traduzioni upstream sono Apache-2.0 e possono fare da base |
| `CIFilter` (flip, resize) | `QImage::flipped(Qt::Horizontal)` (Qt ≥ 6.9; prima `mirrored`), `QPainter` |

---

## 8. Formati compatibili con upstream e asset riutilizzabili

### 8.1 Formati da supportare (compatibilità)

1. **Custom Metrics JSON**: compatibilità completa in lettura, **priorità
   massima**. I producer esistenti (Claude Code, Codex, bitcoin) funzionano
   senza modifiche. Per la tolleranza vedi §6.1.
2. **Runner Gallery**: import di `<name>-frames.zip` o della directory, più
   `metadata.json` opzionale. Stesse validazioni di `validate_runners.py`
   (altezza 36, larghezza 10–100, stessa dimensione, PNG validi, limiti su
   zip ed entry, nessun path traversal).
3. **Directory custom runner di RunCat Neo** (`CUSTOM_RUNNERS.json` +
   `<UUID>/frame-N.png`): import opzionale, a basso costo.
4. **Formato interno AQTRunCat consigliato** (super-set leggibile da un
   umano), per esempio:

   ```
   ~/.local/share/AQTRunCat/runners/<id>/
   ├── runner.json      # {"id","name","author?","renderingMode":"monochrome|color","frameOrder":[...]}
   └── frame-0.png …
   ```

   Così si esprime anche `frameOrder` (swing/pendulum), che upstream supporta
   solo per i built-in.

Formati **da non** rendere compatibili: UserDefaults/plist (configurazione
interna, non portabile).

### 8.2 Licenze e riuso asset

| Asset | Licenza | Riutilizzabile? |
|---|---|---|
| Frame dei runner built-in RunCat Neo (`Media.xcassets/Runners/*`), `sleeping-cat.png` | Apache-2.0 (LICENSE del repo, © Kyome22; nessun NOTICE né licenza separata per gli asset) | **Sì**: includere copia della licenza Apache-2.0, attribuzione e indicazione di eventuali modifiche (§4 Apache) |
| Runner della Gallery | Apache-2.0 (i contributor accettano esplicitamente, CONTRIBUTING "Licensing and Originality") | **Sì**, alle stesse condizioni. L'autore di ciascun runner è in `metadata.json` |
| App icon RunCat Neo (`AppIcon.icon`), nome "RunCat", branding | Apache-2.0 §6: **i marchi non sono concessi** | **No** come identità di AQTRunCat. Il nome "RunCat Neo" si usa solo per descrivere la compatibilità |
| Stringhe di localizzazione RunCat Neo / SystemInfoKit | Apache-2.0 | Sì, come base delle traduzioni |
| Codice Swift | Apache-2.0 | Nessun riuso diretto previsto (linguaggio diverso). Le formule riprodotte sono fatti, non espressione |
| runcat-tray `icons/cat` | Presi da gnome-runcat (**GPL-3.0**) | Evitare (incompatibile con una licenza permissiva di AQTRunCat; ridondante rispetto al gatto upstream) |
| runcat-tray `dab` (Tenor), `mona` (GitHub Mona: marchio e copyright GitHub), `partyblobcat` | Provenienza non licenziata | **No** |
| Codice runcat-tray | BSD-2-Clause | Riusabile, ma non c'è niente che valga la pena copiare |

Raccomandazione: tenere gli asset upstream in `third_party/runcatneo-runners/`
con `LICENSE` (Apache-2.0) e un `NOTICE`/`ATTRIBUTION.md` (sorgente, commit,
autore). La licenza di AQTRunCat deve essere compatibile con Apache-2.0: se il
progetto sceglie GPL serve la v3, perché GPL-2.0-only è incompatibile.
Facoltativo ma corretto: avvisare il maintainer upstream per cortesia. Questa
è un'analisi tecnica, non un parere legale.

---

## 9. Problemi e limitazioni attese su KDE Plasma 6 + Wayland

1. **Icone tray quadrate.** Il system tray di Plasma dispone gli item SNI in
   celle quadrate (~22–24 px logici a seconda del pannello). I runner upstream
   sono larghi 38–100 px su 36 di altezza (rapporto fino a ~2,8:1): in una
   cella quadrata il gatto 56×36 scende a ~22×14, la engine 83×36 a ~22×10.
   **È la limitazione principale per la fedeltà visiva.** Mitigazioni:
   (a) accettarla in M1, centrando il frame nel quadrato senza distorcere;
   (b) più avanti, un **plasmoid opzionale** (QML, KDE-specifico, fuori dal
   core) che riceve i frame o legge lo stato via D-Bus e può avere larghezza
   libera. Da verificare con uno spike reale su Plasma 6.7 in M1.
2. **Costo per frame.** Ogni frame SNI significa un segnale D-Bus e un
   re-fetch della proprietà `IconPixmap` da parte di plasmashell. A 40 fps
   (CPU al 100%) il costo per plasmashell non è trascurabile: è lo stesso
   problema che upstream ha risolto con i layer CA, qui non disponibili.
   Serve un **tetto FPS configurabile** (es. default 20–25) e la misura della
   CPU di `plasmashell` + `aqtruncat` nello spike M1. Da verificare anche se
   `QDBusTrayIcon` in Qt 6.11 scrive icone temporanee su disco per alcuni host
   (gestione "temp icon") e quindi quanto I/O si genera.
3. **Nessun testo nella tray.** SNI non ha label su Plasma (l'estensione
   `XAyatanaLabel` non è supportata dal tray di Plasma). La Metrics Bar va
   resa come pixmap, ed è soggetta al punto 1: in una cella quadrata ci sta
   ~1 valore corto. Rimandare la Metrics Bar dopo M1 (o al plasmoid).
4. **Posizionamento del popup (Dashboard).** Su Wayland un client non può
   posizionarsi in coordinate globali; `QSystemTrayIcon::geometry()` su SNI è
   vuoto e `activated()` non dà coordinate utilizzabili. Opzioni:
   (a) finestra normale/frameless che il compositore piazza dove vuole;
   (b) **LayerShellQt** (libreria KDE, ma supportata anche da wlroots) per
   ancorarla a un angolo, come dipendenza opzionale a runtime;
   (c) in M1 limitarsi a `QMenu` contestuale (DBusMenu, nativo e ben
   posizionato da Plasma), con le metriche come voci di menu, sul modello di
   runcat-tray ma più ricco.
5. **Click sinistro vs menu.** Con SNI, Plasma invia `Activate` al click
   sinistro (`QSystemTrayIcon::Trigger`) e mostra il `QMenu` al destro. Il
   comportamento upstream (click → dashboard) è replicabile.
6. **Colore della tinta (template).** Il pannello Plasma può avere uno schema
   diverso da quello delle app (es. Breeze chiaro con pannello scuro) e la
   palette dell'app potrebbe non corrispondere. Serve un override utente e una
   verifica nello spike. Plasma non ricolora i pixmap SNI.
7. **HiDPI / scaling frazionario.** Gli asset sono @2x (36 px = 18 pt).
   Generare pixmap alla `devicePixelRatio` corretta, oppure fornire un
   `QIcon` con più dimensioni, ed evitare il ridimensionamento lato host.
8. **Flatpak (se e quando).** La tray SNI richiede `--talk-name=org.kde.StatusNotifierWatcher`.
   Il file picker via portal restituisce path FUSE del Document Portal
   (`/run/user/…/doc/…`), dove **inotify non è affidabile**: il watching delle
   Custom Metrics si romperebbe. Serve `--filesystem` sui percorsi dei
   producer (`~/.claude`, `~/.codex`, …) o un packaging nativo per primo.
   `/proc` e `/sys` sono accessibili. UPower e logind richiedono `--system-talk-name`.
9. **Replace atomico e inotify.** `QFileSystemWatcher` smette di osservare un
   path dopo `rename`/`unlink`. Bisogna osservare anche la directory padre e
   re-aggiungere il file. È lo stesso pattern di upstream (stream che termina
   → riapertura dopo 200 ms).
10. **Autostart/tray race.** All'avvio della sessione l'app può partire prima
    che `StatusNotifierWatcher` esista. `QSystemTrayIcon::isSystemTrayAvailable()`
    e un retry evitano un'icona mancante.
11. **Pannelli verticali.** Esistono su Plasma ma non su macOS: upstream non
    li gestisce. Le celle restano quadrate (punto 1), quindi nessun problema
    nuovo, ma le opzioni di orientamento (flip) restano solo orizzontali.

---

## 10. Percorso minimo e pulito verso M1

Obiettivo M1 proposto: **un runner animato in tray, guidato dalla CPU, con
semantica di velocità identica a upstream, runner built-in e Gallery
importabili, menu essenziale, impostazioni persistenti**. Nessuna Dashboard
grafica, nessuna Metrics Bar, nessuna Custom Metric in M1, salvo che i
requisiti completi dicano altro.

### 10.1 Spike preliminare (giorni, non settimane)
Un `QSystemTrayIcon` che cicla i 5 frame del gatto a 2/10/20/40 fps su
Plasma 6 Wayland. Misure: CPU di plasmashell e dell'app, resa visiva in cella
quadrata, nitidezza HiDPI, tinta chiara e scura. **L'esito fissa il tetto FPS
di default e la strategia di dimensionamento.**

### 10.2 Struttura M1 (C++20, Qt 6 Widgets/Gui, CMake, nessuna dipendenza KDE)

```
src/
├── core/                 # niente Qt GUI: testabile
│   ├── CpuSampler        # /proc/stat → busy%, Δ-tick, primo campione gestito
│   ├── SpeedModel        # clamp(cpu/5,1,20), inverso 0.5*(21-v), fps = 2*speed, cap FPS
│   ├── FrameOrder        # ascending/swing/pendulum/partyHorn/custom
│   └── RunnerRepository  # built-in (qrc) + ~/.local/share/AQTRunCat/runners, validazione 36px/10–100
├── app/
│   ├── RunnerAnimator    # QTimer, continuità di fase, pixmap precalcolati (tinta, flip, DPR)
│   ├── TrayController    # QSystemTrayIcon + QMenu (Runner ▸, CPU: x%, Slow down under load, Flip, Launch at login, About, Quit)
│   └── Settings          # QSettings
└── main.cpp
```

### 10.3 Contenuto M1, in ordine
1. CMake, struttura, licenza, `third_party/` con gli asset Apache-2.0 e
   l'attribuzione.
2. `CpuSampler` + test unitari su righe `/proc/stat` registrate.
3. `SpeedModel` + test (tabella §4.3 come casi di test).
4. Runner built-in (8, con frameOrder upstream) in qrc + `RunnerAnimator`
   con tinta monocromatica e flip.
5. Tray + menu + persistenza (runner selezionato, inverso, flip, intervallo
   3/5/10 s).
6. Import runner da zip/directory della Gallery (con validazione) e
   rimozione: anche solo da menu "Import runner…" con `QFileDialog`.
7. Desktop entry, autostart XDG, icona propria di AQTRunCat (non quella di
   RunCat).
8. Gestione sleep/wake (logind) e attesa del watcher SNI all'avvio.

### 10.4 Dopo M1 (ordine suggerito)
- **M2 – Metriche + Dashboard**: Memory/Storage/Battery/Network (§5),
  ring buffer, popup (window o LayerShellQt opzionale).
- **M3 – Custom Metrics**: parser compatibile (§6.1), watcher (§6.3), card,
  mapping SF Symbol → freedesktop. Sample Linux (systemd timer) e verifica
  diretta con i producer Claude Code/Codex.
- **M4 – Editor custom runner, Metrics Bar, i18n**, eventuale plasmoid.

### 10.5 Decisioni da prendere (aperte)
1. Semantica CPU: `total − idle − iowait` (raccomandato, nativo Linux)
   oppure esattamente upstream (nice escluso)?
2. Tetto FPS di default (dopo lo spike).
3. Licenza di AQTRunCat, che condiziona il riuso Apache-2.0 (GPL-2.0-only
   escluso).
4. Plasmoid opzionale per la larghezza piena: sì o no, e in quale milestone.
5. Custom Metrics: solo file aggiunti esplicitamente (come upstream) o anche
   una directory osservata (es. `~/.local/share/AQTRunCat/metrics.d/*.json`)
   come estensione Linux?
