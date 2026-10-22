# F6 — Hover Information

Stato: **completata** il 2026-10-22, versione **1.6.3** (F6.1–F6.3; regola di
versionamento in `requirements.md`). Branch `feature/f6`; nessuna
operazione Git eseguita (le scritture Git le fa l'utente).

Obiettivo: informazioni compatte al passaggio del puntatore sul runner, nel
tray Qt/SNI e nel Plasmoid, riusando i dati che il backend già pubblica.
Nessun nuovo campionamento, nessun comando esterno, nessuna modifica
all'API D-Bus.

## 1. Situazione trovata

| Superficie | Prima di F6 |
|---|---|
| Runner nel tray Qt (`TrayPresenter`) | tooltip fisso `AQTRunCat — <runner>` (solo `AQTRunCat` prima del primo runner) |
| Item della Metrics Bar (`MetricsBarPresenter::toolTipFor`) | `Etichetta: valore completo`, stato + messaggio se non disponibile, `Last updated` per i custom, riga `AQTRunCat Metrics Bar`; il tooltip viene ritrasmesso solo insieme a un cambio d'icona o quando cambia qualcosa di diverso dal valore (scelta M7: su SNI un cambio di tooltip costa come un cambio d'icona) |
| Plasmoid, rappresentazione compatta | tooltip nativo di Plasma (`PlasmoidItem.toolTipMainText`/`toolTipSubText`, mostrato da `CompactApplet.qml` della shell con `PlasmaCore.ToolTipArea`, posizionato da Plasma per ogni bordo): `AQTRunCat` + nome del runner o stato del backend |
| Plasmoid, popup | righe con valore, barra, riga di dettaglio (opzione *Detail lines*); nessun tooltip per riga |
| Plasmoid, metriche | i delta `MetricsChanged` non vengono analizzati a popup chiuso (`wantMetrics`, scelta F4.2 per non pesare su plasmashell) |
| Dashboard Qt | mostra già tutti i dettagli; alcuni tooltip (storage, GPU, stati): non toccata (fuori scope: niente riprogettazione) |

## 2. Formato condiviso

Stesso testo nei due frontend, costruito con la stessa formattazione del
backend (`metricsformat`: unità binarie, una decimale, separatore `.`
indipendente dal locale):

```text
AQTRunCat — Cat                       (Plasma: titolo "AQTRunCat", prima riga "Cat")
CPU: 18.4 %
RAM: 46.5 % · 7.2 GiB / 15.5 GiB used
Swap: 2.4 % · 300.0 MiB / 12.0 GiB
```

Regole:

- percentuali `formatPercent` (`18.4 %`, nessun tetto a 99.9), dimensioni
  `formatBytes` (MiB/GiB, mai `1024.0 MiB`);
- RAM: `+ " (estimated)"` quando il kernel non fornisce MemAvailable;
- **nessuna riga Swap** quando lo swap non è configurato (non è un valore
  mancante); con swap configurato e 0 usati: `Swap: 0.0 % · 0 B / 12.0 GiB`
  (lo zero vero resta uno zero);
- valore mancante a parole, mai `0`: `waiting for data` (pending, oppure ok
  senza valore), `unavailable`, `error`; i messaggi tecnici del backend
  (percorsi, errori di lettura) restano nella Dashboard e nel popup;
- backend: `Connecting to the backend…` prima del primo stato,
  `Backend not running` dopo il periodo di grazia del client (3 s): **nessun
  valore** quando non sono più correnti; durante la grazia restano gli ultimi
  valori (come Dashboard e popup, nessun lampeggio a un riavvio rapido).

Il dettaglio memoria/swap è prodotto da una sola funzione
(`metrics::formatMemoryDetail` / `formatSwapDetail`), usata sia dal campo
`detail` dell'API (`apischema.cpp`, testo invariato) sia dal tray: il
Plasmoid, che mostra solo testi del backend, ottiene così le stesse
stringhe. `hover::whenText` (ex `whenText` privato di `apischema.cpp`, stesso
formato `14:05` / `Oct 21, 14:05`) è condiviso tra il dettaglio API degli
aggiornamenti e la Metrics Bar.

## 3. F6.1 — Tray Qt

- `src/app/hoversummary.{h,cpp}` (libreria `aqtruncat_engine`, solo
  QtCore): `summaryLines(Connection, const SystemMetricsSnapshot *)` e
  `runnerToolTip(runner, lines)`; funzioni pure, nessun timer, nessun I/O.
- `trayapp.cpp`: lo snapshot che `BackendClient::systemSnapshot` già emette
  (1 Hz, lo stesso di Dashboard e Metrics Bar) e `connectionChanged`
  aggiornano il riepilogo; nessun campionamento nel frontend.
- `TrayPresenter::setSummary(lines)`: confronto testuale, **nessun invio se il
  testo non cambia**; un testo cambiato viene applicato insieme al frame
  successivo (`showFrame`, subito prima di `setIcon`), così un host che
  rilegge l'item dopo una modifica lo fa una volta per entrambe; senza frame
  entro 250 ms parte comunque (timer singolo). Il cambio di runner lo
  applica subito. Menu, clic (Dashboard), animazione e Settings invariati;
  nessuna icona aggiuntiva.
- Limite SNI: `QSystemTrayIcon` ha un solo testo di tooltip, che Qt e il
  tema KDE mettono nel *title* del tooltip SNI (nessun sottotitolo
  dall'API Qt). Plasma lo mostra su più righe; è lo stesso meccanismo dei
  tooltip della Metrics Bar dal M7.

## 4. F6.2 — Plasmoid

- Tooltip nativo esistente esteso (nessuna finestra flottante, nessun
  secondo tooltip): `main.qml` `toolTipSubText` =
  `Logic.hoverText(status, runner, metrics, metricsCurrent)`,
  `toolTipTextFormat: Text.PlainText` (nomi di runner con `<` restano testo).
  La posizione per pannelli orizzontali/verticali e desktop è quella di
  Plasma (`ToolTipArea.location = Plasmoid.location`); il tooltip non appare
  a popup aperto (`active: !expanded` della shell).
- `logic.js`: `hoverLines(metrics)` (stesse righe del tray, dai testi
  `formattedValue`/`detail` dell'API, nessuna formattazione propria),
  `hoverStateText`, `hoverText` (stati `connecting`/`incompatible`/
  `unavailable` con le stesse parole del tray).
- Metriche **solo quando servono**: `CompactRepresentation` lega
  `root.hovered` a `containsMouse || activeFocus`; `wantMetrics` include
  `root.hovered`. Al passaggio del puntatore il client chiede `GetMetrics`
  (ultimo campione del backend, non ne provoca uno nuovo) e segue i delta
  finché il puntatore resta; fuori dall'hover nessuna analisi JSON, come
  prima. `BackendClient.metricsCurrent` (= connesso, metriche volute, non
  saltate) impedisce di mostrare valori vecchi: finché `GetMetrics` non
  risponde il tooltip mostra solo il runner.
- Clic, popup, animazione, integrazione nel pannello invariati
  (`loadsInPlasma` in `plasmoidviewer`: pannello orizzontale, verticale,
  desktop, nessun warning QML).

## 5. F6.3 — Dettagli per metrica

Solo superfici con hover affidabile e solo dati già nel modello del backend.

**Metrics Bar (Qt/SNI)**: nuovo campo `metricsbar::Item::context` (righe
che cambiano raramente, dentro il confronto che decide l'invio del
tooltip), mentre le righe che cambiano col valore stanno in `detail`
(inviato solo insieme all'icona, regola M7 invariata):

| Metrica | Dettaglio aggiunto |
|---|---|
| CPU | `8 logical CPUs` (se noto) |
| Memoria | seconda riga `12.0 GiB available` (+ `(estimated)`) |
| Swap | invariato (`(usati / totali)` c'era già) |
| Rete ↓/↑ | `Interfaces: wlan0, …` (interfacce conteggiate) |
| Disco lettura/scrittura | `N disks counted` |
| Storage Root/Home | seconda riga `usati / totali used` |
| Uptime | invariato (durata leggibile c'era già) |
| GPU (utilizzo/memoria/temperatura) | `driver · integrated/discrete · suspended`; per Intel `Activity: time out of the RC6 power-saving state` |
| FPS | `Source: MangoHud` |
| Aggiornamenti pacchetti | `Package manager: pacman`, `Last successful check: 14:05`, `Checking now…`, `Package metadata is more than 7 days old` |
| Aggiornamenti Flatpak | `Last successful check: …`, `Checking now…` |

Un errore mantiene l'ultimo controllo riuscito ma nessun conteggio; FPS con
sorgente OFF resta nascosto (regola F5 invariata).

**Popup del Plasmoid**: con *Detail lines* spento, l'hover su una riga
mostra il dettaglio del backend e quelli degli item
(`Logic.rowToolTip`); con le righe visibili nessun tooltip (sarebbe un
duplicato); messaggi di stato e avvisi sono sempre visibili e non vengono
ripetuti. Il dettaglio API copre già i dati della tabella (GPU: driver,
integrata, MHz e item attività/memoria/temperatura; FPS: MangoHud e
applicazione; aggiornamenti: gestore, ora del controllo, età dei metadati).

**Dashboard**: non modificata (mostra già tutto; niente riprogettazione).

## 6. Backend e API

Nessuna modifica di contratto: API 1, schema 2, stessi metodi e segnali,
stessi testi `detail` (ora prodotti da `formatMemoryDetail`/
`formatSwapDetail`, identici a prima). Nessun nuovo campo, nessuna
frequenza di campionamento cambiata.

## 7. Prestazioni

- Tray: un riepilogo per snapshot (≤ 1 Hz): tre formattazioni e un
  confronto di stringhe. Misura su bus privato con host SNI finto e tema KDE
  (Wayland reale, nessuna finestra, sessione dell'utente non toccata):
  16 s → 158 `NewIcon`, 171 `NewToolTip`; 30 s → 310 / 324. Il plugin KDE
  emette già un `NewToolTip` per ogni frame (l'icona del tooltip segue
  l'icona); F6 aggiunge ≈ 1 segnale al secondo, quando il testo cambia,
  inviato insieme a un frame.
- Plasmoid: costo zero fuori dall'hover; durante l'hover una `GetMetrics` e
  l'analisi dei delta (come a popup aperto). Test: 20 ingressi/uscite in
  1 s non aumentano le generazioni del backend oltre il suo campionamento
  a 1 Hz.
- Nessun comando esterno, nessuna operazione bloccante all'apertura del
  tooltip (il testo è già pronto).

## 8. Test

| Suite | Nuovo |
|---|---|
| `hoversummary` (nuova, 8 casi) | valori validi, unità e arrotondamenti (`99.96 → 100.0 %`, `1023.98 MiB → 1.0 GiB`, swap 0 B), memoria stimata, nessuno swap, valori mancanti a parole senza messaggi tecnici, stati backend, testi condivisi con l'API, `whenText` |
| `metricsbarpresenter` (+2) | righe `context` nel tooltip e reinvio solo quando cambiano; tooltip del runner: titolo subito, riepilogo con il frame successivo, nessun reinvio per testo uguale, fallback senza frame, menu/attivazione invariati |
| `metricsbarmodel` (+1) | dettagli dai dati dello snapshot, nulla se il dato manca, errore con ultimo controllo ma senza conteggio |
| `plasmoidlogic` (+5) | `hoverLines`, senza swap, valori mancanti, `hoverText` (stati, metriche non correnti), `rowToolTip` |
| `plasmoidclient` (+2) | con il backend reale: `metricsCurrent` segue l'hover, nessun campionamento extra; **stesse righe** da `logic.js` e da `hoversummary.cpp` sullo stesso payload `GetMetrics` |

Risultati: build pulita in directory nuova 0 warning; ctest **52/52**;
`packaging-versions` 1.6.3; `loadsInPlasma` 3/3 (non saltato);
shellcheck pulito; `git diff --check` pulito.

## 9. Validazione runtime (isolata)

- Tray: `aqtruncat run` della build con `aqtruncatd` della build, bus
  privato (`build/tests/backendipc-bus.conf`), XDG temporanei, host SNI
  finto (Python, solo per la validazione, non nel repository) che legge la
  proprietà `ToolTip` reale ogni secondo. Visti: titolo subito, riepilogo
  dal primo snapshot (`CPU: waiting for data` al primo campione, poi i
  valori), aggiornamento a 1 Hz; backend fermato e non riavviabile → 3 s
  con gli ultimi valori, poi `Backend not running`; backend di nuovo
  disponibile → riconnessione e valori. Uscita 0, nessun processo rimasto
  (il backend `/usr/local` dell'utente non è stato toccato).
- Plasmoid: `tools/plasmoid-preview.sh` (offscreen, backend reale, bus
  privato) ora disegna anche il testo del tooltip con la stessa espressione
  di `main.qml`: `AQTRunCat` / `Cat` / `CPU: 17.2 %` /
  `RAM: 61.9 % · 19.2 GiB / 31.1 GiB used` / `Swap: 37.9 % · 6.0 GiB / 15.9 GiB`.
- Nota: un primo tentativo senza piattaforma grafica utilizzabile ha fatto
  terminare `aqtruncat` con SIGABRT (Qt senza plugin di piattaforma);
  `systemd-coredump` ne conserva il core (PID 517267, 2026-10-22 19:14),
  ruotato dal sistema.

## 10. Verifiche manuali rimaste all'utente

1. Sessione Plasma reale, tray: passare il puntatore sul runner; controllare
   leggibilità del tooltip su più righe (Plasma lo mostra come titolo),
   aggiornamento mentre è aperto, nessun lampeggio.
2. Plasmoid nel pannello (dopo aver riavviato plasmashell per caricare il
   nuovo QML: `install.sh --restart-plasma` o
   `systemctl --user restart plasma-plasmashell`): tooltip con riepilogo in
   pannello orizzontale, verticale e sul desktop; con *Detail lines* spento,
   hover sulle righe del popup.
3. Tooltip della Metrics Bar con i nuovi dettagli (CPU, memoria, rete,
   aggiornamenti).
4. Desktop non KDE (GNOME con estensione AppIndicator, XFCE): resa dei
   tooltip su più righe.
