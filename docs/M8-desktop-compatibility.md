# AQTRunCat — M8 Desktop Compatibility & Polish

Report di compatibilità, robustezza e prestazioni dell'implementazione Qt/Linux
generica (QSystemTrayIcon → StatusNotifierItem, finestre Qt Widgets). M8 non
introduce frontend desktop-specifici, KDE Frameworks, Plasmoid o packaging.

Legenda evidenze: **[OSS]** osservato/misurato, **[SRC]** letto da sorgenti o
documentazione, **[IPO]** inferenza. Stati: **PASS**, **PASS WITH
LIMITATION**, **NOT TESTED**, **NOT APPLICABLE**, **FAIL**. Un NOT TESTED non
è mai promosso a PASS per inferenza.

## 1. Ambienti

| Ambiente | Versioni | Come |
|---|---|---|
| Host (riferimento) | Arch Linux rolling, kernel 7.2.4-zen2, Plasma/KWin 6.7.5 Wayland, Qt 6.11.2, GCC/CMake di sistema, eDP-1 125 % + HDMI 100 % | sessione reale |
| Fedora 44 | Qt 6.11.2, GCC 16.2.1, CMake 4.3.0 | container docker (build/test); stesso build eseguito contro la sessione Plasma dell'host (runtime) |
| Ubuntu 26.04 LTS | Qt 6.10.2, GCC 15.2.0, CMake 4.2.3 | container docker (build/test) |
| Debian 13 trixie | Qt 6.8.2, GCC 14.2.0, CMake 3.31.6 | container docker (build/test) |
| Ubuntu 24.04 LTS | Qt 6.4.2 | container: configure rifiutato (Qt < 6.5); escluso su indicazione dell'utente, warning nel README |
| Tray alternativa | waybar (Fedora 44) in sway annidato nella sessione Wayland dell'host | container, D-Bus di sessione condiviso |
| XWayland | `QT_QPA_PLATFORM=xcb` nella sessione Plasma Wayland | host |

Non disponibili sulla macchina: sessione Plasma X11 (`startplasma-x11`
presente ma `kwin_x11` non installato), GNOME, XFCE, LXQt, altri compositor
installati. Nessun pacchetto è stato installato sull'host.


## 2. Matrice di compatibilità

### 2.1 Distribuzioni (build e test)

| Distro | configure | build | warning | ctest | casi | avvio CLI / `metrics` | runtime grafico |
|---|---|---|---:|---|---:|---|---|
| Arch (host) | PASS | PASS | 0 | 30/30 PASS | 477 | PASS | PASS (Plasma 6 Wayland) |
| Fedora 44 | PASS | PASS | 0 | 30/30 PASS | 473 + 4 skip | PASS | PASS WITH LIMITATION: build Fedora eseguito contro Plasma dell'host, tema Qt generico/GNOME (§3.3) |
| Ubuntu 26.04 LTS | PASS | PASS | 0 | 30/30 PASS | 473 + 4 skip | PASS | NOT TESTED (nessuna sessione grafica Ubuntu) |
| Debian 13 | PASS | PASS | 0 | 30/30 PASS | 473 + 4 skip | PASS | NOT TESTED (nessuna sessione grafica Debian) |
| Ubuntu 24.04 LTS | FAIL (atteso) | — | — | — | — | — | NOT APPLICABLE: escluso, Qt 6.4.2 < 6.5 (warning nel README) |

- I 4 skip nei container sono attesi e documentati dai test stessi: due test di
  indipendenza dalla locale (nessuna locale con virgola decimale installata) e
  due test sui permessi file (i container eseguono i test come root). Matrice
  rieseguita con il codice finale di M8 (tutte le correzioni incluse).
- Nei container `aqtruncat metrics` riporta "no physical network interface":
  l'interfaccia del container è virtuale, comportamento corretto della policy
  Automatic M3.
- Dipendenze di sviluppo accertate (anche nel README): Arch `cmake ninja gcc
  qt6-base libarchive`; Fedora `cmake ninja-build gcc-c++ qt6-qtbase-devel
  libarchive-devel`; Ubuntu/Debian `cmake ninja-build g++ qt6-base-dev
  libarchive-dev libgl-dev`. Serve un font installato (i container minimi
  richiedono `dejavu-sans-fonts`/`fonts-dejavu-core` per i test di rendering).
  Per il runtime Wayland su Fedora serve `qt6-qtwayland` (senza, Qt usa xcb).

### 2.2 Desktop, tray e display server

| Ambiente | Runner | Animazione | Click primario | Menu | Dashboard | Settings | Metrics Bar | Tema | Scaling | Uscita | Esito |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Plasma 6.7.5 Wayland, tema KDE (host) | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS chiaro/scuro | PASS 100/125 %; 150/200 % emulati | PASS | **PASS** |
| Plasma 6.7.5, XWayland (`QT_QPA_PLATFORM=xcb`) | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS (100 %) | PASS dopo fix §6.1 | **PASS WITH LIMITATION** (non è una sessione X11) |
| Plasma 6, build Fedora, tema Qt generico | PASS (quadrato letterbox, M1) | PASS | PASS | PASS | — | — | PASS | FAIL → corretto (§6.2) | — | PASS | **PASS WITH LIMITATION** |
| waybar (tray SNI GTK) in sway annidato | PASS (larghezza nativa) | PASS | NOT TESTED (nessuna iniezione input) | NOT TESTED | — | — | PASS (icon-size 20) | PASS WITH LIMITATION (contrasto) | — | PASS | **PASS WITH LIMITATION** |
| X11 legacy: stalonetray (XEmbed) + Xvfb, nessun watcher SNI | PASS | PASS | PASS (xdotool → Dashboard) | NOT TESTED | PASS | — | PASS (32 px) | PASS WITH LIMITATION (nero su sfondo scuro) | — | PASS (SIGTERM, exit 0) | **PASS WITH LIMITATION** |
| Nessuna tray (sessione D-Bus senza watcher, es. GNOME puro) | — | — | — | — | — | — | — | — | — | PASS | **PASS** (avvio, warning, nessun crash, exit 0) |
| Plasma 6 X11 (sessione reale) | | | | | | | | | | | **NOT TESTED** (`kwin_x11` non installato) |
| GNOME (con/senza AppIndicator) | | | | | | | | | | | **NOT TESTED** (non installato); comportamento Qt GNOME-theme verificato in §3.3 |
| XFCE / LXQt / altri | | | | | | | | | | | **NOT TESTED** (non installati) |

Evidenze visive in `docs/M8-desktop-compatibility/`: `waybar-tray.png`,
`xembed-stalonetray.png`, `windows-100.png`, `windows-200.png`.

## 3. Comportamento della tray

### 3.1 Plasma 6 Wayland (riferimento)

[OSS] Regressione completa con l'app reale: runner animato in modalità Normal
e Slow-down (a CPU 16,5 % Slow-down applica 31 FPS = `round(10+30·(1−0,165)²)`),
Metrics Bar a 3 metriche, Dashboard e Settings da menu e click primario,
Custom Metrics esplicite e auto-discovered, stati Unavailable/Error, uscita da
menu e da SIGTERM con exit 0 e nessun item residuo. Le regressioni M1–M7
automatiche sono verdi (§12).

### 3.2 Identità SNI e ordine (follow-up M7)

- [OSS] Tutti gli item di un processo Qt espongono `Id = Title =
  applicationName` ("AQTRunCat"). Plasma ordina per categoria e `Title` e, a
  parità, per ordine di registrazione; le sue preferenze di visibilità per
  voce si applicano per `Id`, quindi a runner e barra insieme.
- Una correzione generica non esiste con `QSystemTrayIcon`: l'`Id` non è
  configurabile per item. Cambiare `applicationName` prima di creare ogni item
  (provato nello spike M7) cambierebbe anche `Title`, ordinerebbe gli item
  alfabeticamente tra le altre app, renderebbe instabili le voci persistite da
  Plasma e altererebbe i percorsi `QStandardPaths`. **Non adottato**;
  limitazione dell'host/protocollo accettata.
- AQTRunCat crea gli item in modo deterministico (runner prima, poi la barra
  nell'ordine scelto) e non dipende semanticamente dall'ordine visivo; non usa
  hack per forzarlo.
- [OSS] Dopo un riavvio del watcher (kded6, §5) gli item si ri-registrano in
  parallelo e l'ordine visivo può cambiare (osservato: CPU, runner, NET↓,
  MEM). Ripristinare l'ordine richiederebbe di osservare il watcher (nuova
  dipendenza Qt D-Bus) e ricreare gli item: è il tipo di hack escluso. L'ordine
  torna corretto al riavvio dell'app o modificando la selezione.

### 3.3 Tema generico Qt / desktop non-KDE

- [OSS] Senza il platform theme KDE (build Fedora nel container, tema
  generico) Qt usa `QDBusTrayIcon`: invia 56×56 + 22×22 per il runner
  (letterbox, come in M1) e 44×44 + 22×22 per la barra; tutto funziona.
- [OSS] **Bug trovato**: con il tema GNOME di Qt (`XDG_CURRENT_DESKTOP=GNOME`)
  `QStyleHints::colorScheme()` segue il portale (Light/Dark in tempo reale)
  ma la palette resta chiara con testo `#000000`; runner e Metrics Bar
  restavano neri anche su pannello scuro. Con il tema generico puro lo schema
  è `Unknown`. Corretto in §6.2.
- Limite residuo: SNI/XEmbed non comunicano il colore del pannello. Su host
  dove pannello e schema dell'app divergono (es. barra superiore GNOME sempre
  scura con schema chiaro, sfondo blu di waybar, stalonetray scuro senza
  schema) il contrasto dipende dall'host. Nessun colore hardcoded è stato
  introdotto; un'eventuale preferenza manuale chiaro/scuro sarebbe una nuova
  funzionalità (decisione aperta, §9).

### 3.4 Altre tray

- waybar mostra il runner alla larghezza nativa (non quadrata), quindi più
  leggibile che su Plasma, e le icone 44 px della barra ridotte a 20 px restano
  leggibili. Click e menu non sono stati iniettati (nessun tool di input nel
  compositor annidato): **NOT TESTED** a livello di interazione, anche se
  passano per gli stessi metodi SNI/DBusMenu verificati su Plasma.
- XEmbed (stalonetray, X11 senza watcher SNI): Qt ripiega automaticamente
  sull'icona XEmbed; runner e 3 metriche visibili, click → Dashboard.

### 3.5 Wayland vs X11

Differenze legittime osservate: con xcb le finestre sono posizionate da KWin
come client XWayland e la scala segue l'output X (100 %); la tray è comunque
SNI su D-Bus, identica. Nessun hack X11 introdotto. Una sessione Plasma X11
reale non è disponibile (NOT TESTED).

## 4. Suspend / resume

- [SRC] Sampler e animazione usano `std::chrono::steady_clock` / timer Qt,
  cioè `CLOCK_MONOTONIC`, che su Linux **non avanza durante il suspend**. I
  contatori `/proc/stat` (jiffies) non avanzano durante il suspend; rete e
  disco sono fermi. Il primo intervallo dopo il resume vale quindi ~1 s
  monotono e confronta contatori quasi invariati: nessun valore enorme.
- [OSS] Caso peggiore simulato: processo congelato con SIGSTOP per 60 s
  (qui il monotono avanza di 60 s). Al SIGCONT un solo campione medio
  sull'intervallo (CPU 92,6 % durante una compilazione concorrente, rete in
  KB/s, disco in MB/s), poi campionamento a 1 Hz regolare; nessuna raffica di
  timer arretrati (45 campioni in ~45 s attivi); runner sempre 10–40 FPS con
  rampa EMA normale; exit 0.
- Test automatico aggiunto: `tst_network::longGapAveragesInsteadOfSpiking`
  (un'ora di intervallo dà la media, l'intervallo successivo è indipendente).
- Contatori azzerati da driver dopo il resume: già gestiti come counter reset
  (M3, `counterResetNeverGivesHugeRates`, `counterResetAndDeviceChanges`).
- Nessun baseline reset aggiuntivo necessario; nessuna dipendenza desktop.

### 4.1 Suspend reale (s2idle) durante il soak

Eseguito con il consenso dell'utente, che ha risvegliato la macchina:
`systemctl suspend` alle 04:36:38 con il soak in corso (app reale, runner
Normal, Metrics Bar CPU/NET↓/custom, producer attivo) e un `aqtruncat metrics
--watch` parallelo.

- [OSS] Kernel: `PM: suspend entry (s2idle)` 04:36:39 → `PM: suspend exit`
  04:39:03 (~144 s). In 151 s di tempo reale `CLOCK_MONOTONIC` è avanzato di
  8,1 s, `CLOCK_BOOTTIME` di 150,8 s: confermato che il monotono si ferma.
- [OSS] `metrics --watch`: il campione delle 04:36:39 è seguito da quello delle
  04:39:03 (≈1 s monotono): CPU 24,6 %, rete 0 B/s, disco 3,4/5,8 MiB/s.
  Nessun valore enorme. Nei secondi seguenti CPU 50–80 % e rete fino a
  1,1 MiB/s sono attività reale del sistema al resume (riconnessione Wi-Fi,
  sincronizzazioni), visibili anche nella memoria di sistema; exit 0.
- [OSS] App: viva, runner a 11 FPS con rampa normale, 4 SNI ancora
  registrati, Metrics Bar e Custom Metrics aggiornate (producer ogni 2 s),
  nessun warning nel log, traffico D-Bus invariato (runner 11 NewIcon/s, barra
  ~2,2/s).
- Falso allarme analizzato: dopo il resume l'app del soak mostrava 3,3 % di
  CPU e ~127 risvegli/s del thread principale (contro 0,5 % e ~27/s di
  un'istanza fresca) e RSS ~89 MiB. Causa: lo script di soak usa il tempo reale
  e al resume ha recuperato i passi arretrati, aprendo la finestra Settings,
  la cui anteprima del runner anima a 20 FPS fissi finché è visibile (M5).
  Un'istanza fresca con Settings aperte misura 2,9–3,1 % e ~115 risvegli/s;
  Settings + Dashboard portano l'RSS di un'istanza fresca da 61,6 a 81,1 MiB.
  A regime l'app sospesa e quella fresca coincidono; RSS stabile
  (88,8–89,6 MiB nei minuti successivi). Nessuna correzione necessaria.
- Esito: **PASS**. Il primo intervallo dopo il resume non viene interpretato
  come intervallo lungo (monotono fermo) e un eventuale intervallo lungo
  (stallo) produce medie, non picchi.

## 5. Scomparsa / riavvio della tray

| Prova | Risultato |
|---|---|
| `systemctl --user restart plasma-plasmashell` (host della tray) | [OSS] item non ricreati (stessi nomi D-Bus), nessun duplicato, mostrati nel nuovo pannello nell'ordine corretto, click → Dashboard, app viva. **PASS** |
| `systemctl --user restart plasma-kded6` (StatusNotifierWatcher) | [OSS] durante l'assenza Qt/KSNI stampa `kf.statusnotifieritem: ... SNI unavailable` (uno per item); al ritorno i 4 item si ri-registrano da soli, nessun duplicato, app viva, Quit OK. Ordine visivo cambiato (§3.2). **PASS WITH LIMITATION** |
| Avvio senza alcun watcher (sessione D-Bus privata) | [OSS] warning "no system tray available yet", app viva, SIGTERM → exit 0. **PASS** |
| Riavvio di plasmashell durante il soak | vedi §8 |

Qt gestisce re-registrazione e riapparizione: nessuna infrastruttura aggiunta.

## 6. Problemi trovati e correzioni

### 6.1 Quit ignorato con Dashboard e Settings aperte (bug di affidabilità)

- [OSS] Con Dashboard **e** Settings visibili, "Quit AQTRunCat" dal menu non
  chiudeva l'app (scoperto con xcb; riprodotto in modo deterministico su
  offscreen, xcb e Wayland con una sonda). `QCoreApplication::quit()` chiede
  prima a tutte le finestre di chiudersi e si annulla se una rifiuta; le due
  finestre rifiutano per scelta (chiudere = nascondere, M4/M5).
- Correzione: `requestApplicationQuit()` (`src/app/applicationquit.*`) usa
  `QCoreApplication::exit(0)`, che termina il loop senza consultare le
  finestre; usato dai menu del runner e della barra e da SIGINT/SIGTERM.
- Test: `tst_settingswindow::quitEndsTheApplicationWithDashboardAndSettingsVisible`
  (fallisce con `quit()`: exec restituisce il valore del timer di sicurezza).
- Verifica reale: xcb con entrambe le finestre aperte → Quit da menu, exit 0.

### 6.2 Runner e Metrics Bar neri su pannello scuro con temi Qt non-KDE

Upstream behaviour: template image tinta dal colore del testo della menubar.
AQTRunCat behaviour: colore del testo della palette, ma se la piattaforma
riporta uno schema (Light/Dark) in contraddizione con la palette si usa il
ruolo opposto della palette stessa (`Window`); su `colorSchemeChanged` runner
e barra vengono ritinti.
Reason: i temi Qt non-KDE riportano lo schema senza adattare la palette;
nessun colore hardcoded, nessun cambiamento dove palette e schema concordano
(Plasma).

- Codice: `trayForegroundColour()` in `src/app/runnerpresentation.*`, usato da
  `TrayPresenter` e `MetricsBarPresenter`.
- Test: `tst_runnerpreview::trayForegroundFollowsTheReportedColourScheme`.
- [OSS] Build Fedora, tema GNOME, sulla tray di Plasma: chiaro → scuro →
  chiaro ritinge in tempo reale (nero → chiaro → nero); percorso KDE dell'host
  invariato (BreezeDark: testo chiaro).

### 6.3 Warning ripetuti per file Custom Metrics illeggibile

- [OSS] Con un file esplicito reso illeggibile, Qt stampava
  `inotify_add_watch(...) failed: (Permesso negato)` a ogni tentativo di
  riaggiungerne il watch (5 in ~8 s, nel log/journal).
- Correzione: i file non leggibili non vengono aggiunti al watcher; la
  directory padre resta osservata e il timer di recovery M6 (solo in stato
  degradato) rileva il ritorno della leggibilità.
- Test: `tst_custommetricsservice::temporarilyUnreadableFileErrorsQuietlyAndRecovers`
  (fallisce sul warning senza la correzione; verifica Error con last known good
  e ritorno Available).

### 6.4 `tools/test-cpu-runtime.sh` (M7, già corretto)

Con la barra attiva lo strumento sceglie l'item del runner; rieseguito in M8.

### 6.5 Altri test di robustezza aggiunti

- `tst_custommetricsservice::rapidProducerUpdatesAreCoalescedAndEndOnTheLastValue`:
  200 atomic replace consecutivi → valore finale corretto, meno di 20
  pubblicazioni (debounce, nessun reload per scrittura).
- `tst_network::longGapAveragesInsteadOfSpiking` (§4).

## 7. Robustezza ed error handling

| Area | Caso | Evidenza | Esito |
|---|---|---|---|
| Metriche | `/proc` malformato | `tst_cpustat::rejectsMalformedInput`, `tst_memory::invalidInput`, `tst_network::rejectsMalformedLines`, `tst_diskio::rejectsMalformedLines`, `tst_systemmetrics::oneBrokenSourceDoesNotHideTheOthers` | PASS |
| Metriche | counter reset | `samplerRecoversAfterCounterReset`, `counterResetNeverGivesHugeRates`, `counterResetAndDeviceChanges` | PASS |
| Metriche | interfaccia che compare/scompare | `interfacesAppearingAndDisappearing`, `selectedInterfaceMayDisappearAndReturn` | PASS |
| Metriche | filesystem non disponibile | `tst_storage::queryReportsMissingPaths`, `invalidStatistics`, `missingSourcesAreUnavailableNotZero` | PASS |
| Metriche | dischi che cambiano | `counterResetAndDeviceChanges`, `partitionsAndVirtualDevicesAreNotCountedTwice` | PASS |
| Metriche | intervallo lunghissimo | `longGapAveragesInsteadOfSpiking` + SIGSTOP 60 s reale | PASS |
| Runner | runner configurato che scompare | [OSS] `runner remove` a caldo: l'app continua (frame in memoria); al riavvio fallback a `cat` e INI riparato; test `missingPersistedRunnerFallsBackAndRequestsRepair` | PASS |
| Runner | frame/file invalidi | `tst_runnerloader` (PNG corrotti, dimensioni incoerenti, oversize…) | PASS |
| Custom | JSON malformato / mancante / atomic replace / illeggibile / producer rapido / esplicita mancante / discovered che scompare / recovery | `tst_custommetric`, `tst_custommetricsservice` (9 casi) + verifiche runtime M7/M8 | PASS |
| Config | INI invalido, enum sconosciuti, runner inesistente | `invalidAndUnknownValuesFallBackSafely`, `invalidButPresentRunnerFallsBackWithoutDestroyingSetting` | PASS |
| Config | Metrics Bar >3, ID duplicati/sconosciuti, custom assente | `legacyMetricsBarListsNormaliseDeterministically`, `neverMoreThanThreeItems`, `metricsBarListsCustomSourcesAsTheyComeAndGo` | PASS |
| Tray | assente, riavviata, watcher riavviato | §5 | PASS / PASS WITH LIMITATION |

## 8. Prestazioni e soak

### 8.1 Baseline finale (host, Plasma 6 Wayland, runner Normal)

Metodo come M7 (§10 di `docs/M7-metrics-bar.md`): 10 s warm-up, 40 s di
misura, segnali D-Bus su 30 s, due passate (valori min–max). Baseline senza
AQTRunCat in questa sessione molto silenziosa: plasmashell 0,15 %, kwin
0,65–1,00 %.

| Configurazione | SNI | AQTRunCat CPU | RSS | runner NewIcon/s | barra NewIcon/s | plasmashell | kwin |
|---|---:|---:|---:|---:|---:|---:|---:|
| solo runner (barra OFF) | 1 | 0,40–0,42 % | 58,3–58,5 MiB | 10,97 | 0 | 1,93 % | 2,30–2,40 % |
| solo runner + Dashboard visibile | 1 | 0,50–0,53 % | 63,4–63,6 MiB | 10,97–11,00 | 0 | 1,93–1,95 % | 2,35 % |
| **runner + 3 metriche** (worst case) | 4 | 0,45–0,47 % | 60,4 MiB | 10,97 | 2,10–2,33 | 2,08–2,10 % | 2,42–2,48 % |
| runner + 3 metriche + Dashboard visibile | 4 | 0,55–0,60 % | 64,4 MiB | 10,97–11,00 | 2,10–2,30 | 2,05–2,10 % | 2,38–2,50 % |
| runner + CPU, NET↓, custom + producer ogni 2 s | 4 | 0,50–0,53 % | 60,8–60,9 MiB | 10,97–11,00 | 2,20–2,27 | 2,12–2,15 % | 2,52–2,55 % |

Lettura:

- Nessuna regressione rispetto a M7 (worst case M7: 0,47 %, 60,2 MiB, barra
  2,2–2,9/s) né a M4 (Dashboard visibile +5 MiB, M4 +5,4 MiB).
- Con una baseline finalmente silenziosa il costo desktop è misurabile: il
  runner a ~11 FPS costa ~+1,8 punti a plasmashell e ~+1,4 a kwin; la barra
  nel worst case aggiunge ~+0,15 punti a plasmashell e ~+0,1 a kwin.
- Dashboard visibile: +0,1 punti per AQTRunCat, nessun effetto su runner o
  barra (stesso traffico, stesso FPS): il sampler resta unico e a 1 Hz.
- Custom Metrics con producer attivo: costo trascurabile (nessun polling;
  reload solo su evento + debounce).

### 8.2 Soak (40 minuti, app reale)

`tools`-style script nello scratchpad: app reale `run --verbose`, runner
Normal, Metrics Bar CPU / NET↓ / custom auto-discovered con producer (atomic
replace ogni 2 s), Dashboard mostrata/nascosta ogni 60 s, Settings aperta
(anteprima a 20 FPS), scrittura+lettura di 64 MiB ogni 3 min, **suspend reale**
al minuto ~4,5 (§4.1) e **riavvio di plasmashell** al minuto 20. Campione ogni
minuto (tempo reale).

| Minuti | RSS | CPU AQTRunCat | SNI | fd | watch inotify | thread |
|---|---|---|---:|---:|---:|---:|
| 0–4 (prima del suspend) | 61,4 → 67,7 MiB | 0,5–1,0 % | 4 | 20–24 | 1–2 | 4–8 |
| 7 (resume + recupero dei passi, Settings aperta) | 70,9 → 90,4 MiB | 3,0–3,7 % | 4 | 24 | 2 | 8 |
| 8–39 | **88,2–94,1 MiB, oscillante senza tendenza** | 3,2–3,4 % (Settings aperta) | 4 | 20–24 | 2 | 4–8 |

- Riavvio di plasmashell (t = 1201 s): nessun effetto su item, RSS o CPU.
- Fine: Quit da menu → **exit 0**, **0 SNI residui**, **0 warning** nel log,
  runner sempre tra **10 e 32 FPS** (2265 campioni).
- Nessuna crescita progressiva di fd, watch inotify o thread; l'RSS resta
  nella fascia dovuta a Settings + Dashboard aperte (§4.1) e al tetto della
  `QPixmapCache` (M7 §11.3).
- Non coperto dal soak: cambio di runner/impostazioni a runtime (richiede
  interazione con i widget Settings; coperto da `tst_settingswindow`,
  `tst_desktopsettings` e dal lifecycle M7).

## 9. Limitazioni residue e decisioni aperte

1. **Contrasto della tray su host non-KDE**: SNI/XEmbed non comunicano il
   colore del pannello. Con la correzione §6.2 AQTRunCat segue lo schema
   riportato dalla piattaforma; dove pannello e schema divergono (barra GNOME
   sempre scura con schema chiaro, waybar colorata, stalonetray senza schema)
   il contrasto dipende dall'host. Possibile rimedio futuro: preferenza manuale
   "tray chiara/scura" (nuova funzionalità: decisione dell'utente).
2. **Identità SNI condivisa** (`Id = Title = AQTRunCat`): visibilità per-voce di
   Plasma comune a runner e barra (M7, invariato).
3. **Ordine dopo riavvio del watcher** deciso dall'host (§3.2).
4. **Tema Qt generico**: runner incorniciato in un quadrato e palette generica
   (M1, invariato).
5. **Settings aperta** costa ~2,5 punti di un core per l'anteprima a 20 FPS
   (M5, per progetto, solo mentre la finestra è visibile).
6. Ubuntu 24.04 LTS non supportata (Qt 6.4.2; README).
7. Il runtime grafico di Ubuntu/Debian non è verificato (solo build/test); il
   runtime Fedora è verificato contro la sessione Plasma dell'host.

### Verifiche non eseguibili e motivo

| Verifica | Motivo |
|---|---|
| Sessione Plasma 6 X11 reale | `kwin_x11` non installato; nessuna installazione di pacchetti sull'host. Coperto parzialmente da XWayland |
| GNOME, XFCE, LXQt e relative tray | non installati; tema GNOME di Qt verificato (§3.3), tray alternative coperte da waybar e XEmbed |
| Click/menu dentro waybar | nessuno strumento di iniezione input nel compositor annidato |
| Scala 150/200 % reale dell'output | emulata con `QT_SCALE_FACTOR`; 100 % (HDMI) e 125 % (eDP) reali |
| Runtime grafico Ubuntu/Debian | nessuna sessione grafica di quelle distribuzioni |

## 10. Rischi da affrontare in M9

- Flatpak: talk-name SNI e percorsi del document portal (inotify delle Custom
  Metrics, M0/M6).
- Dipendenze runtime da dichiarare nei pacchetti: Qt ≥ 6.5 Widgets/Gui/DBus
  plugin di piattaforma (wayland: `qt6-qtwayland` su Fedora), libarchive, un
  font.
- Contrasto della tray su desktop non-KDE (§9.1), visibile nelle prime prove
  utente.

## 11. File nuovi e modificati in M8

- Nuovi: `README.md`, `src/app/applicationquit.{h,cpp}`, questo documento,
  `docs/M8-desktop-compatibility/{waybar-tray,xembed-stalonetray,windows-100,windows-200}.png`.
- Modificati: `src/CMakeLists.txt`, `src/app/runnerpresentation.{h,cpp}`,
  `src/app/traypresenter.cpp`, `src/app/metricsbarpresenter.cpp`,
  `src/app/trayapp.cpp`, `src/app/custommetricsservice.cpp`,
  `tests/tst_runnerpreview.cpp`, `tests/tst_settingswindow.cpp`,
  `tests/tst_custommetricsservice.cpp`, `tests/tst_network.cpp`,
  `requirements.md`, `MEMORY.md`.

## 12. Test automatici

Host (Arch, workflow `cmake -S . -B build && cmake --build build && ctest
--test-dir build`): **30/30 suite, 477 casi passati, 0 saltati, 0 warning**
(M7: 472). Container Fedora 44 / Ubuntu 26.04 / Debian 13: 30/30, 473 passati
+ 4 skip attesi, 0 warning.

Test aggiunti in M8: `tst_runnerpreview::trayForegroundFollowsTheReportedColourScheme`,
`tst_settingswindow::quitEndsTheApplicationWithDashboardAndSettingsVisible`,
`tst_custommetricsservice::temporarilyUnreadableFileErrorsQuietlyAndRecovers`,
`tst_custommetricsservice::rapidProducerUpdatesAreCoalescedAndEndOnTheLastValue`,
`tst_network::longGapAveragesInsteadOfSpiking`. Ognuno dei tre test di bug è
stato verificato fallire senza la rispettiva correzione.

Test di integrazione/manuali (richiedono sessione desktop, non unit test):
regressione Plasma, XWayland, tema generico/GNOME, waybar, XEmbed, assenza di
tray, riavvio plasmashell/kded6, suspend reale, scaling, prestazioni, soak.


