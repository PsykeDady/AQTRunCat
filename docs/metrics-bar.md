# AQTRunCat — M7 Metrics Bar

M7 porta su Linux l'equivalente della Metrics Bar di RunCat Neo: valori
sintetici visibili direttamente nella tray, senza aprire la Dashboard. La
soluzione resta Qt/Linux generica: un `QSystemTrayIcon` (StatusNotifierItem)
separato per ogni metrica abilitata, accanto all'item del runner, che resta
invariato. Nessun Plasmoid, nessuna dipendenza KDE, nessun protocollo
specifico del compositor.

Legenda delle evidenze: **[OSS]** osservato/misurato su questa macchina,
**[SRC]** letto da sorgenti o binari installati, **[IPO]** ipotesi.

## 1. Spike Plasma: perché non esiste una vera "text bar" SNI

Ambiente: Arch, Plasma/KWin 6.7.5 Wayland, Qt 6.11.2, eDP-1 scala 1.25 (tray
qui), tema Breeze Light.

- [SRC] Il systray di Plasma (`org.kde.plasma.systemtray.so`, QML
  incorporato) disegna ogni StatusNotifierItem con un `Kirigami.Icon` in una
  cella quadrata (`implicitWidth: size`, `iconSizes.smallMedium` = 22 px
  logici con `scaleIconsToFit` false).
- [SRC] Non c'è alcun riferimento a `XAyatanaLabel`. `Title` compare come
  etichetta solo per gli item dentro il popup "nascosti", non nel pannello.
- [OSS] Un'icona più grande non ottiene più spazio: 22×22 e 44×44 producono
  lo stesso risultato visivo, Plasma la riscala nella cella.
- [OSS] Una label larga in un'unica icona ("CPU 23%", 120×36 o 64×22) viene
  ridotta a ~3–4 px logici di altezza: illeggibile. Un valore breve ("23%")
  occupa ~8 px logici: leggibile. "CPU" sopra "23%" è leggibile ma al limite.
- Conseguenze: runner + testo nella stessa icona degradano entrambi (stessa
  riduzione); una finestra flottante non è ancorabile alla tray su Wayland;
  più SNI separati sono possibili; un Plasmoid avrebbe una vera area di testo
  ma è Plasma-specifico (eventuale M8).

### 1.1 Identità e ordine di più item dello stesso processo

[OSS] probe con 4 item numerati (`busctl` + screenshot):

| Prova | Id / Title esposti | Ordine nel pannello |
|---|---|---|
| Title identici, creati 1-2-3-4 | tutti `Id = Title = <applicationName>` | 1 2 3 4 |
| Title identici, creati 4-3-2-1 | idem | 4 3 2 1 |
| Title distinti (`applicationDisplayName` cambiato prima di ogni item) | `Probe 8…5` | alfabetico per Title, mescolato con le altre app |

- Plasma ordina per categoria e `Title`; a parità di `Title` vale l'ordine di
  registrazione. Tutti gli item di AQTRunCat hanno `Title = AQTRunCat`, quindi
  restano raggruppati e, poiché il runner si registra per primo, la Metrics
  Bar compare subito dopo di lui.
- Gli item condividono anche `Id = AQTRunCat`: le preferenze di visibilità
  per-item di Plasma ("Mostra sempre / Nascondi") valgono per tutti gli item
  AQTRunCat insieme. Cambiare `applicationName` per item sarebbe un hack con
  effetti collaterali (percorsi `QStandardPaths`, identità dell'app): non
  adottato. La selezione per-metrica avviene nelle Settings di AQTRunCat.
- La registrazione presso `StatusNotifierWatcher` è asincrona e ogni item KSNI
  usa una propria connessione D-Bus: tra connessioni diverse D-Bus non
  garantisce l'ordine. Vedi §9 per la misura.

## 2. Architettura

```text
SystemMetricsSnapshot (MetricSampler M3/M4, 1 Hz) ─┐
custommetrics::Source (CustomMetricsService M6) ───┼→ MetricsBarModel ──→ MetricsBarPresenter ──→ N × QSystemTrayIcon
AppSettings (enabled, items) ──────────────────────┘   (semantico)          (renderer SNI)
```

- `MetricsBarModel` (`src/app/metricsbarmodel.*`) produce una lista ordinata
  di `metricsbar::Item`: `id`, `origin` (built-in/custom), `label`,
  `shortLabel`, `shortValue`, `detail`, `status`, `statusMessage`,
  `lastUpdated`. Non sa nulla di celle quadrate, pixel o Plasma. Non possiede
  sampler né timer, a parte un timer a 0 ms che fonde in un solo
  `itemsChanged` più input arrivati nello stesso ciclo dell'event loop, ed
  emette solo se la lista cambia davvero.
- `metricsbarrenderer.*` è il renderer SNI: trasforma un item in un'icona
  quadrata. Tutte le costanti legate alla cella stanno qui.
- `MetricsBarPresenter` (`src/app/metricsbarpresenter.*`) possiede gli item
  tray, li crea/distrugge/riordina e invia icona e tooltip solo quando cambiano.
  L'item tray è dietro una piccola interfaccia (`MetricsBarTrayItem`) per i
  test.
- Lifecycle: `trayapp` possiede modello e presenter per tutta la vita
  dell'app, come `MetricSampler` e `CustomMetricsService`. Dashboard e
  Settings sono consumatori/editor indipendenti; chiuderli non tocca la barra.
  All'uscita il presenter distrugge ogni item (de-registrazione SNI).
- Nessun secondo sampler: il modello riceve lo stesso `snapshotReady` a 1 Hz
  di runner e Dashboard (non il throttle di presentazione della Dashboard).
  Non entra nella pipeline CPU → EMA → curva → runner; Normal/Slow-down e
  `--fps` sono invariati.
- `TrayPresenter` del runner non è stato modificato. Il presenter della barra
  ha le proprie tre azioni (Dashboard, Settings…, Quit) condivise tra i menu
  dei suoi item: estrarre un'astrazione comune avrebbe toccato il runner senza
  un beneficio reale.

## 3. Metriche built-in e ID stabili

| ID | Label | Identità nell'icona | Valore | Fonte |
|---|---|---|---|---|
| `cpu` | CPU | `CPU` | `23%` | `cpu.usagePercent` (non smussato) |
| `memory` | Memory | `MEM` | `45%` | `memory.usagePercent` |
| `swap` | Swap | `SWAP` | `12%` | `swapUsagePercent`; senza swap → Unavailable |
| `network.download` | Network download | `NET↓` | `1.2M` | aggregato rete (policy M3/M5) |
| `network.upload` | Network upload | `NET↑` | `850K` | idem |
| `storage.root` | Root storage | `ROOT` | `62%` | storage `/` (semantica df) |
| `storage.home` | Home storage | `HOME` | `40%` | solo se Home ha un `st_dev` distinto (M4); altrimenti non offerto |
| `disk.read` | Disk read | `DSK↓` | `32M` | I/O dischi fisici aggregato |
| `disk.write` | Disk write | `DSK↑` | `4.0M` | idem |

Le frecce del disco seguono la convenzione già usata dalla Dashboard
(↓ lettura, ↑ scrittura); il tooltip dice sempre "Disk read"/"Disk write".

Custom: `custom:<identità canonica M6>`, cioè il path canonico del file
(stessa deduplicazione explicit/discovered/symlink di M6). Mai il `title`,
che è controllato dal producer e può cambiare.

## 4. Formattazione compatta built-in

Formatter dedicato `src/metrics/compactformat.*` (Qt-free, separato da
`metricsformat` di Dashboard/CLI, `std::to_chars`, indipendente dalla locale):

- Percentuali: arrotondate all'intero, limitate a [0, 100]: `0%`, `23%`,
  `100%` (99.5 → `100%`).
- Rate in byte/s, unità binarie a una lettera, la stessa scala della Dashboard
  (K = KiB/s, M = MiB/s, G = GiB/s, poi T, P, E):
  - sotto 1000 B/s: byte interi `0B`, `850B`;
  - sotto 9,95 dell'unità: un decimale `1.0K`, `1.2M`;
  - sotto 999,5: intero `10K`, `850K`;
  - altrimenti unità successiva (`999.5 KiB/s` → `1.0M`, mai `1000K`);
  - oltre E: `>999E` (saturazione dichiarata, mai un troncamento).
- Massimo 4 caratteri per ogni valore finito in range (verificato da test).
  Valori non finiti o negativi → nessun valore (stato, non `0`).

## 5. Custom Metrics e `metricsBarValue`

- La barra usa solo `metricsBarValue`, verbatim. Non viene mai ricavato da
  `formattedValue`, né abbreviato o reinterpretato: il producer sceglie il
  valore sintetico.
- Una sorgente il cui ultimo snapshot noto non ha `metricsBarValue` (assente,
  `null` o solo spazi) **non è eleggibile**: nessun item. Differenza voluta da
  upstream (che mostra `---`): la barra non riempie la tray di segnaposto.
  Settings lo segnala ("provides no Metrics Bar value").
- Identità nell'icona: il glifo del `symbol` con la stessa mappatura della
  Dashboard (`customSymbolText`, ora condivisa): `staroflife` → ✣, `bitcoinsign`
  → ₿, simboli Unicode usati così come sono; altrimenti nessuna riga di
  identità (il valore usa tutta la cella, il tooltip porta il titolo).
- Scomparsa/ricomparsa (policy):
  - file **esplicito** mancante o rotto: la sorgente resta nel servizio M6,
    l'item resta visibile con indicatore `—`/`!` e l'ultimo valore nel tooltip;
  - file **auto-discovered** cancellato o registrazione rimossa: la sorgente
    sparisce dal servizio e il suo item viene rimosso, ma la selezione resta
    persistita; quando il file torna, l'item ricompare nella stessa posizione;
  - Pending (retry M6 dopo un atomic replace): l'icona mantiene l'ultimo
    valore, così i salvataggi atomici non fanno lampeggiare la barra.
- Lo schema JSON M6 non è cambiato.

## 6. Rendering SNI

- Icona quadrata su una tela nominale di 22 × 22 unità logiche (la cella
  misurata), disegnata con `QPainter` a `max(2, devicePixelRatio)` volte:
  44×44 px su questa macchina. Lo spike non ha mostrato vantaggi da sorgenti
  più grandi, che aggiungerebbero solo payload D-Bus a ogni aggiornamento.
- Layout a due righe: identità (`CPU`, `NET↓`, `✣`) in una fascia alta di 8,5
  unità, valore in grassetto sotto. [OSS] La verifica visiva a confronto con
  la variante "solo valore" ha mostrato che il valore ha la **stessa**
  dimensione in entrambe: in una cella quadrata 3–4 caratteri sono limitati
  dalla larghezza, non dall'altezza. La riga di identità è quindi gratuita e
  rende distinguibili item come `23%`/`45%` o `NET↓`/`NET↑`.
- Fitting con `QFontMetricsF::tightBoundingRect`, font di sistema
  (`QApplication::font()`), nessun font o colore hardcoded:
  - valore: da 13 a 7 unità (passo 0,5), identità: da 9 a 6;
  - la soglia minima 7 deriva dallo spike (valore a ~8 px leggibile, due righe
    a ~7 px al limite); sotto non si scende;
  - identità che non entra → omessa (il valore resta);
  - valore che non entra nemmeno al minimo → `###` (convenzione dei fogli di
    calcolo: "non entra nella cella"), mai un taglio come `1234567` → `1234`;
    il valore completo è nel tooltip.
- Nessuna costante "4 caratteri": la misura è grafica e dipende da glifi e font.
- Colore: `QPalette::WindowText` attivo, lo stesso usato per tingere i runner
  monocromatici, quindi coerente con il pannello in tema chiaro e scuro e
  aggiornato su `ApplicationPaletteChange`/`ApplicationFontChange`. Sfondo
  trasparente: integrazione discreta, come il runner; la leggibilità è data
  dallo stesso contrasto testo/pannello del tema.

## 7. Stati, tooltip, interazione

| Stato | Icona | Tooltip |
|---|---|---|
| Available | valore | `CPU: 23.4 %` |
| Pending | `…` (custom in retry: ultimo valore) | `Waiting: waiting for the first sample` |
| Unavailable | `—` | `Unavailable: no swap is configured` |
| Error | `!` | `Error: <messaggio>` |
| valore troppo largo | `###` | valore completo |

Mai `0` per un valore assente. Il tooltip ha sempre metrica, valore completo e
stato; per le custom anche "Last updated"; l'ultima riga è "AQTRunCat Metrics
Bar", così gli item non sembrano un'altra applicazione.

- Click primario su un item della barra → mostra/nasconde la Dashboard (vista
  di dettaglio naturale; nessuna finestra per metrica).
- Menu contestuale di ogni item: Show/Hide Dashboard, Settings…, Quit
  AQTRunCat (chiude tutta l'app).

## 8. Settings e persistenza

Sezione "Metrics Bar" della finestra Settings M5:

- "Show the Metrics Bar in the system tray" (default **disattivato**);
- elenco con checkbox: prima le metriche selezionate nell'ordine scelto, poi
  le altre built-in, poi le sorgenti custom; Move Up / Move Down;
- conteggio "N of 3 metrics selected" (§8.1);
- note: Home "Only when Home is a separate filesystem", custom senza valore,
  custom "not currently available".

### 8.1 Limite di 3 metriche e runner sempre presente

- La barra è **opt-in**: disattivata di default. Da disattivata AQTRunCat
  espone solo l'item SNI del runner, come in M6.
- Al massimo **3 item della Metrics Bar** contemporaneamente, built-in e
  custom insieme (es. CPU + Memory + Network download).
- Il runner **non fa parte della barra**: non viene mai sostituito, rimosso o
  ricreato da essa e non conta nel limite. Configurazioni supportate:

  ```text
  barra OFF     🐱
  1 metrica     🐱 + [m1]
  2 metriche    🐱 + [m1] + [m2]
  3 metriche    🐱 + [m1] + [m2] + [m3]     ← worst case supportato: 4 SNI
  ```

- Il limite è applicato in due punti indipendenti:
  - `AppSettings` non memorizza mai più di 3 ID (`metricsbar::MaxItems`);
  - `MetricsBarModel::setConfiguration` considera solo i primi 3 ID ricevuti,
    anche se un chiamante aggira `AppSettings`.
- Settings con 3 metriche selezionate: le righe non selezionate sono
  disabilitate (non spuntabili), quelle selezionate restano deselezionabili e
  riordinabili; deselezionandone una, le altre tornano selezionabili. Il
  conteggio mostra "N of 3 metrics selected". Anche una spunta forzata di una
  quarta riga viene rifiutata da `AppSettings` e la lista si riallinea.
- Una sorgente custom selezionata ma momentaneamente assente **mantiene il
  suo slot**: nessuna quarta metrica "sale" al suo posto (comportamento
  deterministico, niente SNI che compaiono e scompaiono).

### 8.2 Persistenza

`AQTRunCat.ini` tramite `AppSettings`:

```ini
[metricsBar]
enabled=false
items=cpu, network.download, custom:/home/u/.local/share/AQTRunCat/metrics/claude.json
```

- `items` è la lista ordinata degli ID selezionati (≤ 3). Assente → default
  `cpu` (attivare la barra mostra subito qualcosa di utile); una lista vuota
  esplicita resta vuota dopo il riavvio.
- Normalizzazione deterministica di qualunque valore salvato (legacy,
  modificato a mano): rimossi ID vuoti, malformati (`custom:` senza path),
  sconosciuti e duplicati, poi tenuti i primi 3 nell'ordine salvato. Esempio
  testato: `["", "future.battery", "network.download", "custom:",
  "network.download", "custom:/data/a.json", "cpu", "memory", "swap"]` →
  `network.download, custom:/data/a.json, cpu`.
- Gli ID custom di sorgenti assenti restano selezionati e sono mostrati in
  Settings come "not currently available".
- Restore Defaults riporta anche la barra a disattivata + `cpu`; le
  registrazioni Custom Metrics restano (come in M6).
- Le modifiche si applicano subito: abilitare/disabilitare, selezionare o
  riordinare crea/distrugge/riordina gli item metrica senza riavvio; l'item
  del runner non viene mai toccato.

## 9. Aggiornamenti, ordine e costi

### 9.1 Soppressione degli aggiornamenti

- Il modello emette solo se cambia qualcosa (anche il solo `detail`).
- Il presenter invia l'icona solo se cambia il contenuto renderizzato
  (identità + testo): 23.1 % → 23.4 % resta `23%`, nessun `setIcon`.
- Tooltip: su KDE ogni `setIcon` emette già `NewIcon` + `NewToolTip`, e un
  `NewToolTip` da solo fa rileggere a plasmashell tutte le proprietà
  dell'item (icona compresa), quindi costa quasi quanto un'icona. [OSS] Ogni
  cambio d'icona produce 1 `NewIcon` + 2 `NewToolTip` nello stesso ciclo (uno
  da KSNI dentro `setIcon`, uno nostro), fusi da Plasma in una lettura. Una
  prima versione ("v1", §10) aggiornava il tooltip a ogni cambio del valore
  preciso: con 5 metriche aggiungeva ~0,65–0,9 `NewToolTip`/s isolati. Policy
  finale: il tooltip si aggiorna **insieme all'icona** (stesso ciclo, Plasma
  li fonde in una lettura) oppure quando cambia qualcosa di diverso dal valore
  preciso (stato, messaggio, label, `lastUpdated`). Il valore preciso del
  tooltip è quindi quello che ha prodotto l'icona visibile, sempre nella stessa
  fascia di arrotondamento.
- Palette o font cambiati → ri-render forzato di tutti gli item.
- Riuso delle icone: lo stesso contenuto (identità, testo, colore, font, DPR)
  riusa la stessa `QIcon`, da una cache LRU di 128 voci nel presenter.
  Motivo misurato in §11.3: ogni `QIcon` distinta passata a un
  `QSystemTrayIcon` lascia una pixmap nella `QPixmapCache` globale di Qt.

### 9.2 Ordinamento

Ordine applicativo = ordine in Settings = ordine di creazione degli item. Il
presenter tiene il prefisso già in ordine e ricrea solo gli item dopo la prima
differenza; un'aggiunta in coda non ricrea nulla. L'ordine reale nel pannello
è deciso dall'host: su Plasma 6.7 segue la registrazione a parità di `Title`
(best effort, vedi misure).

## 10. Costo misurato

Metodo [OSS]: script di misura nello scratchpad con la tecnica di
`spike/m1-tray/tools/perf.py` (Δ utime+stime da `/proc/<pid>/stat`, 10 s di
warm-up, 40 s di misura, % di un core) e, in una passata separata di 30 s,
conteggio dei segnali emessi dalle connessioni D-Bus del processo
(`dbus-monitor --profile`), separando il runner (il mittente di gran lunga più
attivo) dagli item della barra. Profilo XDG isolato, app reale `aqtruncat run`.

### 10.1 Gate di accettazione: runner animato (modalità normale) + ≤ 3 metriche

Runner `cat` **presente e animato in modalità Normal** (CPU reale → EMA →
curva → FPS, nessun `--fps`), due passate per configurazione (valori
min–max). "SNI" = item del processo registrati presso Plasma.

| Configurazione | SNI | AQTRunCat CPU | RSS | runner NewIcon/s | barra NewIcon/s | NewToolTip/s totali | plasmashell | kwin |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| barra disattivata | 1 | 0,40 % | 58,2–58,4 MiB | 11,0–11,3 | 0 | 11,0–11,3 | 4,62–4,65 % | 4,75–4,80 % |
| runner + CPU | 2 | 0,45–0,47 % | 59,4 MiB | 11,0 | 0,73–0,93 | 12,5–12,8 | 4,70–6,92 % | 5,03–6,83 % |
| runner + CPU, MEM | 3 | 0,42–0,47 % | 59,6–59,7 MiB | 11,0 | 1,27–1,33 | 13,5–13,6 | 5,03–5,08 % | 4,97–5,05 % |
| **runner + CPU, MEM, NET↓** (worst case supportato) | **4** | **0,47 %** | **60,2 MiB** | 11,0 | **2,23–2,30** | 15,4–15,6 | 4,92–5,10 % | 5,00–5,15 % |
| runner + CPU, NET↓, NET↑ (terna più volatile) | 4 | 0,47–0,50 % | 60,2–60,5 MiB | 11,0 | 2,77–2,90 | 16,5–16,8 | 4,83–4,85 % | 4,97–5,00 % |

Baseline senza AQTRunCat nella stessa sessione: plasmashell 3,77–5,97 %,
kwin 3,38–4,40 %.

Lettura:

- Il runner anima normalmente (~11 FPS a riposo su questa macchina) e il suo
  ritmo non cambia con la barra: la barra non entra nella pipeline CPU.
- Il costo per item è proporzionale ai **cambi del valore compatto**, non al
  campionamento: CPU e rete cambiano quasi ogni secondo, Memory raramente. Il
  worst case supportato aggiunge **2,2–2,9 aggiornamenti icona/s**, circa un
  quarto del traffico del runner al suo minimo (10 FPS).
- AQTRunCat: +0,07 % di un core e +1,8 MiB RSS nel worst case.
- plasmashell/kwin: +0,3–0,4 punti nelle passate pulite, dentro la
  variabilità delle baseline (rumore di fondo noto della macchina: `clamd`,
  browser). Stima M1 (~0,42 % di un core desktop-wide per aggiornamento/s):
  ~1 % di un core [IPO].
- Barra disattivata: un solo SNI (il runner), nessun render, nessun traffico.

### 10.2 Dati esplorativi (non gate)

- **Runner presente a 10 FPS fissi** (`--fps 10`), per isolare la barra:
  +1 metrica 10,9 NewIcon/s totali, +3 12,3–12,5, **+5 (fuori specifica)**
  13,1–13,2, con AQTRunCat 0,38–0,40 → 0,53–0,55 % e RSS 58,2 → 61,3 MiB. Una
  prima versione con tooltip aggiornato a ogni variazione del valore preciso
  ("v1") aggiungeva ~0,65–0,9 NewToolTip/s isolati con 5 metriche (§9.1).
- **Harness di componente senza runner** (processi di test separati che
  usano il nome `AQTRunCat` ma creano solo item metrica): rendering, ordine,
  lifecycle (§11.1), churn e memoria (§11.3). Servono a isolare i componenti;
  non rappresentano l'app e non sono un gate.

## 11. Lifecycle e validazione runtime

### 11.1 Lifecycle con SNI reali (harness, senza runner)

Harness nello scratchpad linkato alle librerie del progetto (`AppSettings` →
`MetricsBarModel` → `MetricsBarPresenter` con `QSystemTrayIcon` veri);
"registrati" = item del processo presenti in
`StatusNotifierWatcher.RegisteredStatusNotifierItems` [OSS]. Eseguito prima
dell'introduzione del limite di 3 (il presenter non ha limiti propri: li
applicano `AppSettings` e il modello).

| Passo | Item presenter | Registrati su Plasma | Creati in totale |
|---|---:|---:|---:|
| default (disattivata) | 0 | 0 | 0 |
| abilitata (default `cpu`) | 1 | 1 | 1 |
| 5 metriche | 5 | 5 | 5 |
| riordino (upload al 2° posto) | 5 | 5 | 9 (prefisso `cpu` mantenuto, 4 ricreati) |
| rimozione di 2 | 3 | 3 | 9 |
| disattivata | 0 | 0 | 9 |
| riattivata | 3 | 3 | 12 |
| dopo 20 toggle rapidi | 3 | 3 | 42 |
| presenter distrutto (quit) | 0 | 0 | — |

Nessun item orfano, nessun riavvio.

### 11.2 Stress con l'app reale (runner presente)

`aqtruncat run --verbose`, runner in modalità Normal, **9 built-in + 1
custom = 11 SNI (fuori specifica, prima del limite di 3)**, producer che
riscrive il JSON custom con atomic replace ogni 2 s; a metà il file
(auto-discovered) viene cancellato per 20 s e poi ricreato. Due run da 600 s:

| Run | RSS iniziale → finale | Andamento | SNI | warning Qt | orfani dopo quit | exit |
|---|---|---|---:|---:|---:|---:|
| #1 (icona nuova a ogni update) | 61,8 → 74,1 MiB | lineare ~42 KB/s, piatto da ~300 s (74,0–74,1) | 11 | 0 | 0 | 0 |
| #2 (riuso `QIcon`, finale) | 61,8 → 77,7 MiB | pendenza dimezzata nei primi 300 s, poi in lenta salita verso il tetto | 11 | 0 | 0 | 0 |

- Runner sempre tra 10 e 40 FPS (11–12 a riposo, 40 nei picchi reali).
- Scomparsa/ricomparsa custom [run #2]: SNI 11 → **10** con il file
  cancellato, → **11** quando torna, senza intervento (selezione persistita).
- Il run #2 non ha raggiunto il plateau in 600 s; la prova accelerata di
  §11.3 dimostra che la crescita è limitata.

### 11.3 Memoria: `QPixmapCache`, non una perdita

- [OSS] Isolamento sull'app reale (180 s): barra disattivata + producer
  attivo → RSS piatto (59,0 → 59,1 MiB); barra con 10 item + file statico →
  +1,2 MiB/30 s. La crescita sta nel percorso icona della barra, non in M6.
- [OSS] Harness Qt puro, 600 aggiornamenti: `setIcon` con una `QIcon` nuova
  (anche a contenuto identico) +4,77 MB (~8 KB = una pixmap 44×44 per
  update); solo tooltip +0; riuso di 2 `QIcon` +4 KB. Con
  `QPixmapCache::setCacheLimit(1024)` la crescita si ferma a +384 KB.
- [OSS] Prova accelerata con il presenter reale (12 000 icone distinte in
  240 s): RSS **piatto** da 30 s a 240 s sia con la cache di default (68,6 →
  68,7 MB) sia con la cache disabilitata (57,8 → 57,9 MB). La crescita è
  limitata: ~11 MB sono il tetto LRU della `QPixmapCache` globale di Qt
  (10 MiB di default), il resto è riscaldamento una tantum.
- Conclusione: nessuna perdita. Il riuso delle `QIcon` (§9.1) evita voci
  inutili per i valori ricorrenti; il tetto resta quello di Qt, che AQTRunCat
  non modifica (influirebbe su tutta la UI).
- Churn (harness, senza runner): creare e distruggere un item lascia ~8 KB
  nello stack Qt/KDE, identico con `QSystemTrayIcon` puri; rilevante solo a
  ogni cambio di configurazione.

### 11.4 Verifiche funzionali su Plasma 6 Wayland (app reale)

- Runner + CPU, MEM, NET↓: 4 SNI, il runner registrato per primo
  ("AQTRunCat — Cat"), poi i 3 item nell'ordine scelto. Tooltip letti via
  D-Bus: `CPU: 22.6 %`, `Memory: 56.6 % (17.6 GiB / 31.1 GiB)`,
  `Network download: 62.5 KiB/s`, ognuno con "AQTRunCat Metrics Bar".
- Click primario (`Activate`) su un item della barra → Dashboard mostrata
  (menu "Hide Dashboard"), di nuovo → nascosta.
- Quit dal menu (DBusMenu `Event clicked`) di un item della barra → l'intera
  app esce con **exit 0**, 0 SNI residui, log senza warning.
- Stati custom reali: file esplicito mancante → `—` con tooltip
  "Unavailable: File is not available."; JSON malformato → `!` con
  "Error: invalid JSON …"; JSON valido senza `metricsBarValue` → nessun item.
- Tema: BreezeLight → BreezeDark (`plasma-apply-colorscheme`) ritinge runner
  e barra senza riavvio; ripristinato BreezeLight subito dopo.
- Scala 125 % (eDP-1): tutte le schermate sono a questa scala.
- Ordine: in una prima esecuzione con 5 metriche un item custom è comparso
  prima di NET↑; nelle 3 successive e in tutte le prove seguenti l'ordine è
  stato quello configurato. Coerente con la registrazione asincrona per
  connessione (§1.1): best effort.

### 11.5 Evidenze visive (`docs/metrics-bar/`)

Ritagli della sola area AQTRunCat, eDP-1 a 125 %, BreezeLight salvo dove
indicato:

- `metrics-bar-basic.png`: runner + CPU, MEM, NET↓ (worst case supportato);
- `metrics-bar-dark.png`: stessa configurazione in BreezeDark;
- `metrics-bar-status.png`: runner + custom mancante (`—`) + custom
  malformato (`!`);
- `metrics-bar-multiple.png`: stress fuori specifica con 10 item (prima del
  limite di 3), con il fitting di `24.0%` e `216K`.

## 12. Limitazioni

1. SNI non ha una label testuale su Plasma: ogni metrica occupa una cella
   quadrata; ~4 caratteri utili, il resto è nel tooltip.
2. Tutti gli item condividono `Id = Title = AQTRunCat`: le impostazioni di
   visibilità per-item di Plasma valgono per tutti (runner incluso).
3. L'ordine nel pannello è dell'host: best effort (registrazione asincrona).
   Un riordino ricrea gli item dopo la prima differenza.
4. Il costo desktop è per aggiornamento (§10): metriche molto variabili (CPU,
   rete) aggiornano ~1 volta/s ciascuna.
5. Ogni `QIcon` distinta lascia una pixmap nella `QPixmapCache` di Qt (LRU,
   10 MiB): RSS fino a ~+12 MiB nel caso peggiore, poi stabile.
6. Identità custom solo se il `symbol` è mappabile; altrimenti il valore
   occupa la cella da solo e il titolo è nel tooltip.
7. Su host SNI diversi da Plasma (M8) la cella può avere dimensioni diverse;
   il renderer adatta il testo, ma non è stato validato.

## 13. Percorso futuro (senza impegno)

Il modello è indipendente dal renderer: un eventuale renderer Plasma (M8)
potrebbe consumare gli stessi `metricsbar::Item` e mostrare testo a
larghezza libera. Richiederebbe un canale verso il Plasmoid (es. D-Bus) ed è
fuori da M7.

## 14. Test e file

Suite nuove o estese (30 suite / 472 casi complessivi, 0 warning, prima di
M7: 26 / 385):

| Suite | Casi | Copertura |
|---|---:|---|
| `compactformat` (nuova) | 33 | 0 %/100 %, arrotondamenti, B/K/M/G/T, confini 999,5 e 9,95, saturazione, ≤ 4 caratteri, locale `it_IT` |
| `metricsbarmodel` (nuova) | 15 | ID stabili, nessuna selezione, built-in, ordine, stati (mai `0`), Home, ID sconosciuti, custom verbatim/non eleggibile/stati/scomparsa e ricomparsa, coalescing, abilita/disabilita, **limite 3** |
| `metricsbarrenderer` (nuova) | 22 | policy contenuto, fitting con identità, `100%`, `1.2M`, frecce, simboli, overflow `###`, identità omessa, vuoto, DPR 1/1,25/2/3, palette chiara/scura, sfondo trasparente |
| `metricsbarpresenter` (nuova) | 11 | creazione/aggiornamento/rimozione, nessun update a valore compatto invariato, policy tooltip, stati, riordino, riuso `QIcon`, spegnimento/quit, click e menu, cambio palette |
| `appsettings` (+3) | 11 | default disattivato, persistenza ordine e lista vuota, mai più di 3, normalizzazione legacy deterministica, Restore Defaults |
| `settingswindow` (+3) | 8 | selezione, ordine, applicazione runtime, ID obsoleti, **righe disabilitate a 3/3**, quarta spunta rifiutata, sorgenti custom che compaiono/scompaiono |

File nuovi: `src/metrics/compactformat.*`, `src/app/metricsbarmodel.*`,
`src/app/metricsbarrenderer.*`, `src/app/metricsbarpresenter.*`,
`src/app/customsymbol.*`, i quattro `tests/tst_*` sopra, questo documento e
`docs/metrics-bar/*.png`.

File modificati: `src/CMakeLists.txt`, `tests/CMakeLists.txt`,
`src/app/appsettings.*`, `src/app/settingswindow.*`, `src/app/trayapp.cpp`,
`src/app/dashboardwindow.cpp` (usa `customSymbolText`, comportamento
invariato), `src/metrics/systemmetrics.h` (commento),
`tests/tst_appsettings.cpp`, `tests/tst_settingswindow.cpp`,
`tools/test-cpu-runtime.sh` (con la barra attiva sceglie l'item del runner per
contare i `NewIcon`: verificato con barra a 3 metriche, idle 11 / half 24–27 /
full 38–40 / recovery 11–13 FPS, 3186 `NewIcon` in 145 s), `requirements.md`,
`MEMORY.md`.

Regressioni: build opzionale degli spike (`-DAQTRUNCAT_BUILD_SPIKES=ON`)
senza warning; `cycle-runners.sh`, `test-cpu-fps-curve.sh`,
`test-metrics-runtime.sh` e `test-cpu-runtime.sh` con rc 0; `shellcheck`
pulito; nessun processo o file temporaneo residuo; le directory create nel
profilo XDG reale dagli strumenti sono state rimosse.
