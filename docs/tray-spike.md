# AQTRunCat — M1.0 Tray Spike

> Domanda: *Qt 6 / `QSystemTrayIcon` è una base sufficientemente buona per
> visualizzare e animare AQTRunCat su KDE Plasma 6 + Wayland?*
>
> Risposta breve: **B — sì, con compromessi** (vedi §6). Data: 2026-10-01.

Legenda delle affermazioni:

- **[OSS]** osservato/misurato in questo spike;
- **[SRC]** verificato leggendo il sorgente della versione installata (comportamento documentato dal codice);
- **[IPO]** ipotesi non dimostrata.

---

## 1. Environment

| Componente | Versione |
|---|---|
| Distribuzione | Arch Linux |
| Kernel | 7.2.4-zen2-1-zen |
| CPU | Intel Core i7-11390H, 8 thread, governor `powersave` (laptop) |
| Qt | 6.11.2 (`qt6-base 6.11.2-3`, `qt6-wayland 6.11.2-1`) |
| KDE Plasma | 6.7.5 (`plasma-workspace`, `kwin`, `plasma-integration`) |
| KDE Frameworks (runtime, non dipendenza) | 6.30.0 (`kstatusnotifieritem`, `knotifications`, `kirigami`) |
| Wayland | `wayland 1.26.0`, sessione `XDG_SESSION_TYPE=wayland`, QPA `wayland` |
| Session bus | `dbus-broker` |
| Schermi | eDP-1 1920×1080 fisici, **scala 1.25** (1536×864 logici) — contiene la tray; HDMI-A-1 1920×1080 scala 1.0 (pannello senza tray) |
| Pannello con tray | orizzontale in alto, spessore 33/34 px logici, system tray con `scaleIconsToFit` = default (false) |
| Tema | Color scheme `BreezeLight` (BreezeDark testato temporaneamente e ripristinato), Plasma theme `default` |
| Compilatore | GCC (`g++`), CMake + Ninja, `RelWithDebInfo` |

Condizioni di rumore durante le misure: desktop reale in uso (browser, Teams,
~10 altri item SNI in tray, un plasmoid di terze parti che logga errori ogni 2 s) e
`clamav-daemon.service` in crash-loop (≈550 restart in 30 min, ~100% di un core).
Per questo tutte le misure CPU sono **per processo** (non di sistema) e
intervallate da baseline senza spike.

---

## 2. Implementation

Prototipo volutamente sacrificabile:

```
CMakeLists.txt                      # top-level minimo, solo add_subdirectory(spike/m1-tray)
spike/m1-tray/CMakeLists.txt        # eseguibile aqtruncat-tray-spike, frame in qrc
spike/m1-tray/main.cpp              # ~210 righe, tutto lo spike
spike/m1-tray/tools/perf.py         # benchmark CPU/RSS/context switch da /proc (strumento esterno)
spike/m1-tray/tools/dbus_count.py   # conteggio traffico SNI con dbus-monitor (strumento esterno)
third_party/runcatneo/              # 3 runner upstream non modificati + LICENSE Apache-2.0
THIRD_PARTY_NOTICES.md
```

Comportamento:

- `QApplication` + un `QSystemTrayIcon`, menu contestuale con una sola voce
  *Quit*; uscita pulita anche su `SIGINT`/`SIGTERM` (self-pipe) e `--duration`.
- Runner: `cat` (56×36), `coffee` (38×36, quasi quadrato), `engine` (83×36,
  il più largo dei built-in) copiati byte-per-byte da RunCat Neo `b3b1543`
  (Apache-2.0, verificato in M0; attribuzione in `THIRD_PARTY_NOTICES.md`).
  Ordine frame numerico (`-frame-N`), sequenza ascending.
- Semantica monocromatica ("template"): alpha del frame mantenuto, colore
  sostituito con `QPalette::WindowText` della palette applicativa
  (`--tint text`, default); `--tint none` o un colore esplicito (`#ff00ff`
  usato per misurare i pixel nelle schermate). Le `QIcon` sono costruite una
  sola volta e ricostruite solo su `ApplicationPaletteChange`.
- Animazione: `QTimer` `Qt::PreciseTimer` a `round(1000/fps)` ms che fa solo
  `tray.setIcon(icons[i])`. Statistiche di timing su stdout (`--stats`).
- Nessuna dipendenza oltre `Qt6::Widgets`; nessun codice Plasma-specifico.

```bash
cmake -S . -B build && cmake --build build
build/spike/m1-tray/aqtruncat-tray-spike --fps 2|10|20|40 [--runner cat|coffee|engine] [--tint text|none|#rrggbb] [--duration S] [--stats S]
```

---

## 3. Runner rendering

### 3.1 Cosa riceve Plasma

[OSS] letto da D-Bus (`IconPixmap` dell'item, `busctl get-property`):

| Runner | Sorgente | Percorso KDE (default su Plasma) | Percorso Qt generico (`QT_QPA_PLATFORMTHEME=generic`) |
|---|---|---|---|
| coffee | 38×36 | `[(38,36)]` + identico in ToolTip | `[(38,38), (22,22)]` letterbox |
| cat | 56×36 | `[(56,36)]` + identico in ToolTip | `[(56,56), (22,22)]` letterbox |
| engine | 83×36 | `[(83,36)]` + identico in ToolTip | `[(22,22), (64,64)]` (83 scartato: > 64) |

Nel percorso KDE l'immagine viaggia alla dimensione nativa, non quadrata;
nel percorso generico Qt la rende quadrata (letterbox) e aggiunge 22×22
[SRC qdbustraytypes.cpp, coerente con OSS].

### 3.2 Cosa appare nella tray

[OSS] misurato su schermata nativa di eDP-1 (scala 1.25) individuando i pixel
del runner tinto `#ff00ff`:

| Runner | Aspect sorgente | Pixel saturi del runner in tray (px fisici) | ≈ px logici | Scala lineare (slot 27 px / larghezza sorgente) |
|---|---:|---:|---:|---:|
| coffee | 1.06 | 27×17 | 22×14 | ≈ 0,71 |
| cat | 1.56 | 24×14 | 19×11 | ≈ 0,48 |
| engine | 2.31 | 15×10 | 12×8 | ≈ 0,33 |

"Pixel saturi" = pixel con tinta `#ff00ff` ben riconoscibile; esclude i bordi
antialiasati. Per engine (frame 83 px con ~12 px vuoti a sinistra e linee
sottili) le parti fini diventano sfumature quasi invisibili dopo la riduzione
a ⅓: la parte ancora "piena" misura solo 15×10.

- Lo slot della tray è **quadrato, ≈ 22 px logici** (27 px fisici a 1.25):
  [SRC] `main.qml` del systray usa `Kirigami.Units.iconSizes.smallMedium`
  (22) per ogni item quando `scaleIconsToFit` è false (default), in una
  griglia a celle quadrate. [OSS] coffee riempie esattamente ~27 px di larghezza.
- Plasma **ridimensiona preservando l'aspect ratio** per far stare l'immagine
  nel quadrato: niente distorsione, **niente clipping** [OSS]. Il runner
  largo viene quindi rimpicciolito in proporzione alla sua larghezza.
- Interpolazione: smussata (bilineare/area), nessun artefatto di
  nearest-neighbour visibile; bordi leggermente morbidi ma puliti [OSS, a vista
  sui ritagli 4×].
- Nessun problema di HiDPI frazionario osservato (1.25): l'immagine viene
  campionata dal 56×36 nativo, quindi c'è abbondanza di pixel sorgente [OSS].

### 3.3 Leggibilità

Ritagli in `docs/tray-spike/` (`*-1x.png` dimensione reale, `*-4x.png`
ingranditi nearest-neighbour; contengono anche le icone adiacenti per scala):

| Runner | Giudizio |
|---|---|
| coffee (38×36) | **Ottimo**: occupa lo slot come un'icona normale, nitido e riconoscibile. |
| cat (56×36) | **Riconoscibile e gradevole ma piccolo e "basso"**: ~60% dell'altezza delle icone vicine; la sagoma del gatto in corsa si legge. |
| engine (83×36) | **Al limite**: 15×10 px fisici, la forma non è riconoscibile senza sapere cosa sia. |

Generalizzando: con slot quadrato di lato *S*, un runner W×36 viene mostrato
alto `36·S/W`; sotto ~1.6:1 resta leggibile, sopra ~2:1 diventa illeggibile.
Gran parte dei runner della Gallery (M0: larghezze fino a 100 px) è nella
fascia problematica [IPO, estrapolato dai 3 casi misurati].

### 3.4 Tema chiaro/scuro

[OSS] con color scheme cambiato a runtime (`plasma-apply-colorscheme`):

- **Percorso KDE (default)**: lo spike riceve `ApplicationPaletteChange` e
  ritinge senza riavvio: `#232629` su BreezeLight, `#fcfcfc` su BreezeDark.
  Contrasto corretto in entrambi i casi (`light-cat`, `dark-cat`).
- **Percorso Qt generico**: la palette resta quella di default Qt
  (`#000000`): runner **nero su pannello scuro, praticamente invisibile**
  (`dark-generic-theme-coffee`, il coffee nero accanto al gatto bianco del
  percorso KDE).

Conseguenza: su Plasma `QPalette` è una sorgente di tinta corretta; fuori da
Plasma (o senza `plasma-integration`) servirà un'altra sorgente (es.
`QStyleHints::colorScheme()`, portal `org.freedesktop.appearance`) — lavoro futuro.

---

## 4. Performance

Metodo [OSS]: `spike/m1-tray/tools/perf.py`, CPU = Δ(utime+stime) da
`/proc/<pid>/stat` su finestre di 40 s dopo 8 s di warm-up, espressa in **% di
un core**; due passate in ordine inverso (2→40, 40→2) + 4 baseline senza
spike intercalate (`plasmashell` 0,10–0,17%, `kwin_wayland` 2,65–2,87%).
Δ = differenza rispetto alla media delle baseline. Traffico D-Bus con
`dbus_count.py` (`dbus-monitor --profile`, finestre di 15 s, misurato
separatamente per non perturbare la CPU). Runner `cat`, percorso KDE.

| Requested FPS | Effective behaviour | AQTRunCat CPU | Memory (RSS) | plasmashell impact | Visual result | Notes |
|---:|---|---:|---:|---:|---|---|
| 2 | 2,00 frame/s; `max_dt` 500,6 ms; 2,0 `GetAll`/s da plasmashell | 0,07% | 50 MB | +0,34% (kwin +0,39%) | Animazione lenta "a scatti" per definizione (2 fps) | Totale desktop ≈ **0,8%** |
| 10 | 10,00/s; `max_dt` 100,9 ms; 10,0 `GetAll`/s | 0,35% | 50 MB | +1,80% (kwin +1,80%) | Vedi nota * | Totale ≈ **4,1%** |
| 20 | 20,00/s; `max_dt` 51,5 ms; 20,0 `GetAll`/s | 0,74% | 50 MB | +3,80% (kwin +3,51%) | Vedi nota * | Totale ≈ **8,3%** |
| 40 | 40,00/s (12018 frame in 300,5 s); `max_dt` ≤ 27,9 ms; 40,0 `GetAll`/s | 1,41% | 50 MB | +8,22% (kwin +6,81%) | Vedi nota * | Totale ≈ **17%**; RSS plasmashell +30 MB poi plateau |

Righe aggiuntive (una passata ciascuna):

| Condizione | Spike | plasmashell Δ | kwin Δ | bus | RSS spike |
|---|---:|---:|---:|---:|---:|
| engine @ 40 | 1,45% | +8,06% | +6,98% | 0,57% | 50 MB |
| cat @ 40, Qt generico | 1,37% | +7,63% | +6,70% | 0,52% | 28 MB |
| cat @ 20, Qt generico | 0,72% | +3,73% | +3,55% | 0,27% | 29 MB |

\* **Fluidità percepita: non valutata a occhio dall'agente.** Ho verificato
che ogni frame richiesto viene emesso e prelevato da Plasma (1 `GetAll` per
frame a tutti i livelli), ma la percezione della fluidità su un'icona di
24×14 px richiede un osservatore umano: vedi §7, decisione 1.

Osservazioni:

1. **Il costo è lineare negli FPS: ≈ 0,42% di core per fps** sommando spike,
   plasmashell, kwin_wayland e dbus-broker. **40 fps costano il doppio di 20**
   (≈ 17% vs ≈ 8,3% di un core, in continuo).
2. **~95% del costo non è nel nostro processo**: plasmashell ≈ 0,2%/fps
   (≈ 2 ms di CPU per frame), kwin_wayland ≈ 0,17%/fps (ricomposizione del
   pannello). Lo spike costa ≈ 0,035%/fps (`setIcon` ≈ 48 µs + serializzazione
   e risposta al `GetAll`). Ottimizzare il nostro lato non cambia il quadro:
   l'unica leva generica è il numero di frame al secondo.
3. Larghezza del runner irrilevante per il costo (engine ≈ cat); percorso
   Qt generico ≈ percorso KDE.
4. Wakeup: spike ≈ 5 context switch per frame; plasmashell ≈ 29 per frame
   (≈ 1180/s a 40 fps vs 11/s in baseline).
5. Memoria spike: 50 MB RSS col platform theme KDE (carica 28 librerie KF6
   a runtime), 28 MB col tema generico. Stabile per 5 min a 40 fps.
6. **plasmashell a 40 fps**: RSS +30 MB nei primi ~105 s, poi plateau
   (+4 MB nei 195 s successivi), non restituita in idle nei 2 min seguenti;
   in un altro momento è scesa spontaneamente di ~48 MB. [IPO] high-water
   mark dell'allocatore / cache QML-pixmap, non leak lineare; un test di ore
   sarebbe necessario per escluderlo.
7. Nessun frame perso o coalescing osservato: il coalescing di Plasma (timer
   10 ms + un solo `GetAll` in volo) [SRC] non scatta fino a 40 fps su questa
   macchina [OSS].
8. Warning/errori: nessuno attribuibile allo spike in journal (unico messaggio:
   avviso locale "C" causato dai miei script di misura con `LC_ALL=C`).

Traffico D-Bus per frame [OSS]: `NewIcon` + `NewToolTip` (percorso KDE) →
un `GetAll` di plasmashell con ≈ 16 KB di pixel ARGB (icona + copia per il
tooltip); a 40 fps ≈ 645 KB/s di payload pixel; dbus-broker 0,54% a 40 fps.

---

## 5. Qt / Plasma findings

### 5.1 QSystemTrayIcon su Plasma non è il QSystemTrayIcon "di Qt"

- [OSS] Su Plasma il processo carica `KDEPlasmaPlatformTheme6.so`
  (plasma-integration) e con esso `libKF6StatusNotifierItem` e ~25 librerie
  KF6, pur avendo AQTRunCat solo `Qt6::Widgets` come dipendenza.
- [SRC] `KDEPlatformSystemTrayIcon::updateIcon` → `KStatusNotifierItem::setIconByPixmap`
  **e** `setToolTipIconByPixmap`: ogni `setIcon` produce due segnali
  (`NewIcon`, `NewToolTip`) e due serializzazioni eager dell'immagine alla
  dimensione nativa (nessun letterbox, nessuna dimensione aggiuntiva).
- [SRC] Senza tema KDE (`QT_QPA_PLATFORMTHEME=generic`, caso tipico di altri
  desktop) si usa `QDBusTrayIcon` di Qt: un solo `NewIcon`, pixmap
  ricalcolato a ogni lettura della proprietà, immagini rese quadrate
  (letterbox), aggiunta 22×22, scarto delle dimensioni > 64 px. Il file
  temporaneo su disco (`tempIcon`) è usato solo con indicator-application/Unity:
  non su Plasma. [OSS] confermati sizes e un solo segnale per frame.
- [OSS] Qt registra l'item su una **seconda connessione** al bus (nome unico
  separato), path `/StatusNotifierItem`, menu esportato su `/MenuBar`
  (`com.canonical.dbusmenu`), `Status=Active`, `Category=ApplicationStatus`,
  `Id`/`Title` = `AQTRunCat`.

### 5.2 StatusNotifierItem / D-Bus / Plasma

- [SRC] `StatusNotifierItemSource` (plasma-workspace 6.7.5) collega `NewIcon`,
  `NewToolTip`, `NewTitle`, … a `refresh()`: timer single-shot di 10 ms, poi
  `GetAll` asincrono; se un `GetAll` è già in volo marca `m_needsReRefreshing`
  (coalescing). Ricostruisce una `QIcon` con tutte le immagini ricevute.
- [OSS] 1 `GetAll` per frame da plasmashell (`:1.24`, `org.kde.StatusNotifierHost-1909`)
  a 2/10/20/40 fps; nessun altro consumer.
- **Caching**: [SRC] nessuna cache per frame lato Plasma (ogni `GetAll`
  ricostruisce l'icona) né lato KSNI oltre al confronto `cacheKey` col frame
  precedente; [OSS] crescita di RSS di plasmashell ad alta frequenza (§4.6).
- **Scaling** effettuato dal consumer (Plasma/Kirigami), non da Qt nel
  percorso KDE: slot quadrato 22 px logici, fit con aspect preservato [SRC+OSS].

### 5.3 Wayland

- [OSS] Il tray è puro D-Bus: lo spike non crea superfici Wayland, nessun
  problema di posizionamento; il menu contestuale è renderizzato da Plasma via
  DBusMenu. *Quit* invocato via `com.canonical.dbusmenu.Event(clicked)` →
  uscita rc=0.
- [OSS] Il costo compositor è reale e misurabile: kwin_wayland +0,17%/fps.
- Non testati (fuori scope): click sinistro/`Activate`, popup posizionati,
  riavvio di plasmashell con tray che sparisce/ricompare.

### 5.4 Costo dell'animazione

Riassunto: il collo di bottiglia è architetturale (ogni frame = segnale +
`GetAll` + ricostruzione icona in plasmashell + repaint kwin), indipendente
da Qt e dalla dimensione del runner. Su questa macchina è sostenibile a tutti
i livelli testati, ma il costo continuo a 40 fps (≈ 17% di un core su un
laptop) contrasta con il requisito "lasciarlo acceso tutto il giorno senza
accorgersene" (requirements §18); a 20 fps è ≈ 8%, a 10 fps ≈ 4%.

---

## 6. Recommendation

### **B — Qt tray is sufficient with compromises**

`QSystemTrayIcon` funziona correttamente su Plasma 6 + Wayland: si avvia, si
registra, anima con timing preciso fino a 40 fps senza frame persi, segue il
tema chiaro/scuro in tempo reale, esce pulito, nessun warning. Non serve alcuna
dipendenza KDE in compilazione.

Compromessi, con l'evidenza che li fonda:

1. **Runner larghi rimpiccioliti dallo slot quadrato di 22 px.**
   Evidenza: §3.2 — cat 24×14 px fisici (scala ≈ 0,48), engine 15×10
   (≈ 0,33, non riconoscibile), coffee 27×17 (ok); [SRC] cella quadrata
   `smallMedium`. Il gatto classico resta riconoscibile; i runner con aspect
   > ~2:1 no. Non risolvibile in modo generico via SNI.
2. **Costo CPU proporzionale agli FPS, quasi tutto in plasmashell + kwin.**
   Evidenza: §4 — ≈ 0,42% di core per fps; 40 fps ≈ 17%, 20 fps ≈ 8,3%.
   La curva upstream (2–40 fps) è riproducibile, ma il tetto massimo andrà
   scelto conoscendo questo costo (decisione aperta, non fissata qui).
3. **Tinta monocromatica affidabile solo con la palette KDE.**
   Evidenza: §3.4 — col tema generico il runner resta nero su pannello scuro.
4. **Memoria di plasmashell** cresce di ~30 MB ad alta frequenza.
   Evidenza: §4.6. Da rivalutare con test lunghi in M8.

Perché non C: nessuno di questi limiti blocca M1 ("il gatto vive e risponde
alla CPU" è dimostrabile con il gatto classico). Diventerebbe **C** se si
decidesse che (a) la fedeltà dei runner larghi della Gallery è un obiettivo
primario, o (b) il costo a fps elevati è inaccettabile anche con un tetto
ragionevole — in entrambi i casi l'evidenza rilevante è §3.2 e §4, e l'unica
via per larghezza piena e costo per frame più basso sarebbe un'integrazione
Plasma-specifica (spike separato, ad es. in M8 come previsto dai requisiti).

### Possibile lavoro futuro (annotato, non implementato)

- Ritaglio automatico dei margini trasparenti dei frame (engine ha ~12 px
  vuoti a sinistra): generico, recupera qualche pixel di scala.
- Valutare con l'utente l'impostazione Plasma "Scale icons to fit"
  (`scaleIconsToFit`) sul pannello: [IPO] con spessore 33 px probabilmente
  non cambia la dimensione (arrotondamento a icon size standard), non testato
  per non alterare il desktop.
- Sorgente di tinta per desktop non-KDE (`QStyleHints::colorScheme`, portal).
- Test di ore per la RSS di plasmashell; test con riavvio di plasmashell.
- Ridurre il payload: nel percorso KDE il tooltip trasporta una copia
  dell'icona (+100% dati per frame) perché plasma-integration la imposta
  sempre; non controllabile via API `QSystemTrayIcon` [SRC].

---

## 7. Decisioni aperte

1. **Verifica visiva 20 vs 40 fps (umana)**: lanciare
   `build/spike/m1-tray/aqtruncat-tray-spike --fps 20` e poi `--fps 40` e
   giudicare se la differenza su un'icona di ~24 px giustifica il raddoppio
   del costo (≈ 8% → ≈ 17% di un core).
2. **Tetto FPS** per M1.1 (tra 20 e 40, o diverso), alla luce di §4.
3. Se la fedeltà dei runner larghi (Gallery) è requisito primario: in quel
   caso pianificare uno spike Plasma-specifico (categoria C) — non ora.
4. Se mantenere `coffee`/`engine` in `third_party/` dopo lo spike o solo `cat`
   per M1.1.
