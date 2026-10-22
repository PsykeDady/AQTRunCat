# F5 — Additional Built-in Metrics

Stato: **completata** il 2026-10-22, versione **1.5.6** (F5.1–F5.6; regola di
versionamento in `requirements.md`). Branch `feature/f5`; nessuna
operazione Git di scrittura è stata eseguita (il commit spetta all'utente).

F5 aggiunge sette metriche **Built-in**: uptime, GPU, FPS dei giochi,
aggiornamenti dei pacchetti della distribuzione, aggiornamenti Flatpak,
kernel e distribuzione. Non sono Custom Metrics né Integrations: escono dallo
stesso campionatore di sistema e dallo stesso backend (`aqtruncatd`) di CPU,
memoria e rete, e raggiungono con la stessa API la tray Qt/SNI, la CLI e il
Plasmoid.

Immagini di validazione: `docs/F5-additional-built-in-metrics/`
(`dashboard.png`, `plasmoid-light.png`, `plasmoid-dark.png`).

## 1. Indagine sull'architettura (prima di scrivere codice)

| Componente | Risultato | Uso in F5 |
|---|---|---|
| `metrics::SystemMetricsSampler` (Qt-free) | unico lettore di `/proc`/`/sys`, chiamato a 1 Hz da `MetricSampler` nel backend; sorgenti iniettabili (`MetricsSources`) | le nuove sezioni entrano nello stesso `SystemMetricsSnapshot`; nessun secondo campionatore |
| `api/apischema` | un oggetto JSON per id di `metricsbar::builtInMetrics()`; il frontend Qt ricostruisce lo snapshot dal JSON (`systemSnapshotFromMetrics`) | nuovi oggetti `builtin`, stessa forma; ricostruzione inversa per la tray |
| `BackendService` | pubblica delta coalescati (`MetricsChanged`, generazione +1), `SetSetting` validato, una sola istanza per bus | servizio degli aggiornamenti posseduto dal backend; chiavi nuove in `SetSetting`/`GetSettings` |
| `aqtruncatctl` | `metrics [ID…]`, `--json` = JSON dell'API | nessun comando nuovo: le metriche nuove sono normali id |
| Plasmoid | presenta in modo generico `formattedValue`/`detail`/`items`/`normalized`; nessuna logica per id | nessun codice per metrica; solo sezione rinominata e impostazioni condivise |
| Custom Metrics v2 (F1) | esecuzione limitata in `CustomMetricsService` (process group, timeout, limiti di output, stop limitato) | estratti `app/processgroup.h` e `BoundedProcess` con le stesse regole; i controlli aggiornamenti **non** sono Custom Metrics |
| Metrics Bar | id stabili, massimo 3 voci, persistenza in `metricsBar/items` | 7 id nuovi selezionabili; nessuno aggiunto di default |

Precedenze rispettate: comportamento esistente invariato (CPU/EMA/curve,
Metrics Bar, F1–F4.2), nessuna dipendenza nuova obbligatoria, nessun
collettore nel QML.

## 2. Id, sorgenti e unità

Tutti `category: "builtin"`, `kind: "system"`.

| Id | Sorgente | `value` / `unit` | Presentazione | Metrics Bar |
|---|---|---|---|---|
| `uptime` | `/proc/uptime` (primo campo, `CLOCK_BOOTTIME`: sospensione inclusa) | secondi interi / `seconds` | `1d 1h 1m` | `uptime` (`3d`, `14h`, `52m`) |
| `gpu:<device>` (una per GPU) | DRM `/sys/class/drm/card<N>/device` + sysfs del driver, NVML per NVIDIA | utilizzo % (o `null`) / `percent`; `items` = utilization, memory, temperature | modello + tre voci | `gpu.utilization`, `gpu.memory`, `gpu.temperature` = la GPU selezionata |
| `fps` | log CSV di MangoHud (`output_folder`) | FPS misurati / `fps` | `144 FPS` + applicazione, frame time | `fps` (`144`) |
| `updates.packages` | `pacman -Qu` / `apt-get -s dist-upgrade` / `dnf -C check-update` | numero / `count` | `3 updates`, `Up to date` | `updates.packages` |
| `updates.flatpak` | `flatpak remote-ls --updates --cached` | numero / `count` | `7 updates` (app, runtime) | `updates.flatpak` |
| `kernel` | `uname(2)` | `null` (testo) / `null` | `Linux 7.2.10-zen1-1-zen`, `x86_64` | no (testo) |
| `distribution` | `/etc/os-release`, poi `/usr/lib/os-release`, `/etc/lsb-release`, file specifici | `null` (testo) / `null` | `Arch Linux`, `rolling · ID arch` | no (testo) |

`<device>` è lo slot PCI (`0000:00:02.0`); per GPU non PCI
`platform:<dispositivo>`. Gli oggetti GPU sono ordinati per id. Nessun
conteggio, testo o durata è trasformato in una percentuale: `normalized` è
`null` per uptime, FPS, conteggi, kernel e distribuzione, e anche per
l'oggetto GPU stesso, perché ogni voce ha la propria barra.

`data` (valori grezzi, sempre con `section {state, message}`):

- `uptime`: `seconds` (float).
- `gpu:*`: `device`, `card`, `driver`, `vendor`, `vendorId`/`deviceId`
  (hex), `model`, `integrated` (bool|null), `bootVga`, `suspended`,
  `selected`, `frequencyMHz`, e per campo `utilization {state, message,
  percent, method}`, `memory {state, message, usedBytes, totalBytes}`,
  `temperature {state, message, celsius, sensor}`.
- `fps`: `source`, `condition` (`measured` | `stale` | `inactive` |
  `not-configured` | `missing` | `disabled` | `unsupported`), `fps`,
  `frametimeMs`, `lastFps`, `ageSeconds`, `application`.
- `updates.*`: `manager`, `count`, `checking`, `lastSuccess`,
  `metadataTime` (ISO 8601 UTC), `metadataStale`, `remote`, `note`,
  `ignored`; Flatpak anche `apps`, `runtimes`, `installations[{name, state,
  message, apps, runtimes}]`.
- `kernel`: `sysname`, `release`, `version`, `machine`.
- `distribution`: `name`, `prettyName`, `id`, `idLike[]`, `versionId`,
  `version`, `codename`, `buildId`, `variant`, `source`.

## 3. F5.1 Uptime

`/proc/uptime` letto a ogni campione (una `read` da circa 30 byte). È
l'uptime del sistema, non quello di un processo. Il formato tronca ai minuti
(`59 s → 0m`, `3600 → 1h 0m`, `90061 → 1d 1h 1m`); la forma compatta usa
l'unità maggiore, tronca, e non supera 4 caratteri (`999d` come tetto). Un
file mancante dà Unavailable, un valore malformato Error (mai 0).

## 4. F5.2 GPU

### 4.1 Rilevamento

Ogni `card<N>` di `/sys/class/drm` con un `device` (i connettori come
`card1-eDP-1` non contano). Vengono scartati i framebuffer firmware e il KMS
virtuale (`simpledrm`, `simplefb`, `efifb`, `vkms`) e i dispositivi PCI la cui
classe non è "display controller" (0x03). Più nodi DRM per lo stesso
dispositivo valgono una GPU sola. Il modello viene dal driver
(`product_name` di amdgpu), da NVML oppure da `pci.ids` (hwdata, cercato una
volta per id e messo in cache); in mancanza `Intel GPU [8086:9a49]`.
`integrated`: Intel sul bus radice (slot `0000:00:…`) è integrata, le altre
Intel e le NVIDIA sono discrete; per AMD il bus non lo dice, quindi resta
`null` (sconosciuto, mai indovinato).

### 4.2 Telemetria per vendor

| Driver | Utilizzo | VRAM | Temperatura | Altro |
|---|---|---|---|---|
| `amdgpu` | `gpu_busy_percent` (`busy-percent`) | `mem_info_vram_used`/`_total` | hwmon `edge`, poi `junction`, poi `temp1` | frequenza da `pp_dpm_sclk` (riga `*`) |
| `i915`, `xe` | **attività**: quota di tempo fuori dallo stato di risparmio RC6, dal delta di `gt/gt0/rc6_residency_ms` (`power/rc6_residency_ms` sui kernel vecchi; `tile0/gt0/gtidle/idle_residency_ms` per xe) (`rc6-residency`) | integrata: Unavailable, "usa la memoria di sistema"; discreta: non esposta dal driver | hwmon del dispositivo (`pkg`, `vram`) se esiste; integrata: Unavailable (il sensore è quello del package CPU, non viene sostituito) | `gt_act_freq_mhz` / `freq0/act_freq` |
| `nvidia` (proprietario) | NVML `nvmlDeviceGetUtilizationRates` (`nvml`) | NVML `nvmlDeviceGetMemoryInfo` | NVML `nvmlDeviceGetTemperature` | nome da NVML |
| `nouveau`, `radeon`, virtio, … | Unavailable ("not reported by the … driver") | Unavailable | hwmon se c'è | — |

NVML (`libnvidia-ml.so.1`, parte del driver NVIDIA) viene caricata con
`dlopen` alla prima GPU NVIDIA attiva e tenuta caricata. Non è né una
dipendenza di build né di runtime: senza NVML la GPU resta nella lista con i
tre campi Unavailable e il motivo. Le funzioni usate sono API pubbliche e
documentate di NVML (`nvmlInit_v2`, `nvmlDeviceGetHandleByPciBusId_v2`, …).
Nessun `nvidia-smi`, nessun sottoprocesso, nessun permesso di root.

Sulle GPU Intel l'**attività** non è l'utilizzo per motore che mostra
`intel_gpu_top`. Quello richiede l'interfaccia PMU di perf
(`perf_event_paranoid`/`CAP_PERFMON`) oppure la scansione del `fdinfo` di
tutti i processi. Il tempo fuori da RC6 è l'unico indicatore senza
privilegi e a basso costo; lo si chiama "Activity" ovunque e
`data.utilization.method` vale `rc6-residency`. Il primo campione è
Pending, e un contatore che torna indietro (sospensione, reset) dà Pending
con una nuova base.

### 4.3 GPU in runtime suspend

Se `device/power/runtime_status` è `suspended` (dGPU dei portatili ibridi a
riposo) **non** si legge nulla: leggere i file del driver o chiamare NVML la
risveglierebbe e consumerebbe batteria. I tre campi sono Unavailable con il
motivo ("the GPU is suspended …"), il dispositivo resta in lista con
`suspended: true`.

### 4.4 Più GPU e selezione

L'API pubblica una metrica per GPU, quindi nulla è ambiguo per CLI e
Plasmoid. La Metrics Bar ha bisogno di una GPU sola: l'impostazione
condivisa `gpuDevice` vale `auto` (prima GPU nota come discreta, altrimenti
la prima) oppure un id. Il backend risolve la scelta a ogni campione e la
scrive in `data.selected`, così i frontend non devono ripeterla. Una GPU
scelta e assente (eGPU scollegata) resta salvata, e nel frattempo vale
`auto`.

### 4.5 Limiti

- Intel: attività, non utilizzo per motore; VRAM delle Arc discrete e
  temperatura delle iGPU non disponibili.
- AMD: integrata o discreta resta sconosciuto; per le APU la "VRAM" è la
  memoria riservata (carve-out).
- NVIDIA: con `nouveau` c'è solo la temperatura; NVML assente → nessun
  valore.
- Multi-GT (Meteor Lake e successive): viene letto solo `gt0` (render).
- Validato a runtime solo su Intel i915 (TigerLake, l'unica GPU della
  macchina). AMD, xe, NVIDIA/NVML e nouveau sono coperti da test con un
  `/sys` finto e un NVML finto, non da hardware reale.

## 5. F5.3 FPS

### 5.1 Indagine

| Via | Esito |
|---|---|
| Utilizzo GPU/CPU, refresh del display (DRM vblank) | **rifiutata**: non è l'FPS di un'applicazione (lo vieta anche il requisito) |
| Frame rate del compositor (KWin) | rifiutata: è la frequenza di composizione, non quella del gioco, e non esiste un'interfaccia generica |
| Iniezione (`LD_PRELOAD`, layer Vulkan forzato), lettura della memoria dei processi | rifiutata (sicurezza, requisito) |
| Socket di controllo di MangoHud (`control=`) | **non fornisce FPS**: verificato nel sorgente (`src/control.cpp`): accetta `hud`, `logging`, `fcat` e invia solo l'handshake di versione |
| Log CSV di MangoHud | **scelta**: verificato nel sorgente (`src/logging.cpp`): con il logging attivo ogni campione viene aggiunto al file (`ios::app`) e seguito da `flush()`; il file `*_summary.csv` arriva solo alla fine |
| Layer overlay di Mesa (`VK_LAYER_MESA_overlay`, `output_file=`) | non implementato: va abilitato per applicazione con variabili d'ambiente, solo Vulkan, ed è meno diffuso di MangoHud |

Non esiste una sorgente generale e affidabile senza strumenti esterni.
F5 implementa quindi un adattatore **opzionale** e verificabile verso ciò
che MangoHud già scrive per conto suo, quando l'utente lo usa con il
logging attivo.

### 5.2 Comportamento

- Impostazione condivisa `fpsSource`: `mangohud` (default) oppure `off`.
- Cartelle: le `output_folder` di `~/.config/MangoHud/*.conf`
  (`MangoHud.conf` e i file per applicazione; `~` espanso). La
  configurazione viene riletta al massimo ogni 5 s. Le cartelle si
  riesaminano quando cambiano, al massimo ogni 5 s; il log più recente si
  controlla a ogni campione (un `stat` più la lettura degli ultimi 16 KiB).
- Si usa il `*.csv` più recente, ignorando i `*_summary.csv`. La riga di
  intestazione è la prima che inizia con `fps,` (gestito anche il formato
  `log_versioning`). Conta solo l'**ultima riga completa** (terminata da
  `\n`); una riga con un numero di campi diverso è un errore, mai un
  valore.
- Freschezza dal tempo di modifica del file: ≤ 3 s **measured**; da 3 a 60
  s **stale** (nessun valore corrente, `lastFps` nel dettaglio); oltre 60 s
  **inactive**. Gli altri stati: `missing` (MangoHud non installato:
  nessun manifest del layer Vulkan `MangoHud*.json` e nessun `mangohud` nel
  PATH), `not-configured` (installato ma senza `output_folder`),
  `disabled` (`off`), `unsupported` (log senza intestazione o malformato,
  Error).
- L'applicazione è ricavata dal nome del log (`Game_2026-…csv` → `Game`).

Per attivarlo, in `MangoHud.conf`:
`output_folder=/home/<utente>/mangohud-logs`, `log_interval=100`, e
`autostart_log=1` oppure `Shift_L+F2` nel gioco.

### 5.3 Limiti

- Serve che MangoHud stia registrando; senza log non c'è FPS (lo stato lo
  dice).
- Un gioco alla volta: conta il log più recente.
- `MANGOHUD_CONFIG`/`MANGOHUD_CONFIGFILE` passate al gioco e il MangoHud in
  Flatpak (`org.freedesktop.Platform.VulkanLayer.MangoHud`, che scrive nella
  sandbox dell'applicazione) non sono visibili ad AQTRunCat.
- MangoHud non è installato sulla macchina di sviluppo: la validazione
  runtime ha usato un log scritto in tempo reale nel formato verificato nel
  sorgente (§16), non un gioco vero.

### 5.4 Sorgente OFF: FPS nascosto (correzione, 2026-10-22, sempre 1.5.6)

Prima della correzione `off` mostrava comunque FPS nella Dashboard (`N/A` e
"the FPS source is turned off"), nella Metrics Bar (`—`) e nel Plasmoid
(`—`), come se fosse una metrica non disponibile. Causa: nessun client
distingueva `disabled` dagli altri stati senza valore. Ora:

- **`off` è una scelta dell'utente, non uno stato**: FPS sparisce da ogni
  superficie. Dashboard: niente riga e niente sezione Gaming (FPS è la sua
  unica metrica). Metrics Bar: nessuna voce e nessun segnaposto; la voce
  non occupa uno dei tre posti, e la selezione salvata resta, quindi FPS
  ricompare da solo quando si sceglie di nuovo MangoHud. Plasmoid: nessuna
  riga.
- **Una sola regola**: nascosto se e solo se `condition` è `disabled`
  (`metrics::fpsHidden()` per Dashboard e Metrics Bar, `hiddenMetric()` in
  `logic.js` per il Plasmoid, applicata ai dati del backend).
  `missing`, `not-configured`, `inactive`, `stale`, `unsupported` e
  `measured` restano visibili, ciascuno con il proprio stato. Il valore
  predefinito di `FpsMetrics::condition` è ora `inactive`: solo una
  sorgente `off` esplicita nasconde FPS.
- **API invariata**: la metrica `fps` resta in `GetMetrics`/`MetricsChanged`
  con `state` `unavailable` e `data.condition` `"disabled"`, e
  `aqtruncatctl metrics fps` la riporta come prima. Anche CLI e
  diagnostica restano invariate.
- **Subito, senza riavvio**: a un cambio di `fpsSource` (Settings Qt,
  Plasmoid, `aqtruncatctl`, file INI) il backend rilegge solo FPS e pubblica
  le metriche senza attendere il campione successivo. Con `off` la lettura
  non tocca il filesystem: nessuna scansione delle cartelle, nessuna
  lettura della configurazione MangoHud o dei log (già vero prima della
  correzione).

### 5.5 Installer: riavvio di Plasma opzionale (2026-10-22)

Durante la verifica di §5.4 il Plasmoid continuava a mostrare FPS: il
pacchetto installato era aggiornato, ma plasmashell girava da prima
dell'installazione e teneva il QML/JS caricato all'avvio. Rimuovere e
riaggiungere il widget non basta: serve un nuovo plasmashell. Su richiesta
dell'utente, `install.sh` (che finora non riavviava mai plasmashell, §5
F4.2 di MEMORY) ora:

- **chiede** "Restart Plasma now …? [y/N]" solo se aggiorna un widget già
  installato **e** c'è un plasmashell dell'utente in esecuzione, su
  terminale; N, Invio o nessun terminale: Plasma non viene toccato e viene
  stampato il comando da usare. `--restart-plasma` e `--no-restart-plasma`
  rispondono senza chiedere (`--restart-plasma` è incompatibile con
  `--no-plasmoid` e con `-U`); `--dry-run` mostra `restart Plasma`;
- riavvia con `systemctl --user restart plasma-plasmashell.service` (anche
  sotto sudo, nella sessione dell'utente). Il comando aspetta che il nuovo
  plasmashell prenda il nome D-Bus. L'unit concede 40 s per fermarsi e 40 s
  per partire (`TimeoutSec=40sec`): probabile causa, non verificata, dell'attesa di circa un
  minuto osservata. Se plasmashell non c'è, fa `reset-failed` e un nuovo
  `start` (copre start limit e avvii falliti). Senza l'unit systemd (e non
  sotto sudo) usa `kquitapp6 plasmashell` e poi `kstart plasmashell`;
- aspetta fino a 90 s che plasmashell ricompaia, dicendo prima che pannelli
  e desktop spariscono per un po' (fino a circa un minuto). Altrimenti
  avvisa con i comandi da lanciare da un terminale o da una console di
  testo (Ctrl+Alt+F3). L'installazione resta riuscita in ogni caso.

Non cambia nulla per pannelli, layout e istanze del widget. Test:
`installer` 314 controlli (+39). Un plasmashell finto (una copia di `sleep`
con il nome passato in `AQTRUNCAT_PLASMASHELL_NAME`) e un `systemctl` finto
coprono domanda/N/y, opzioni, riavvio senza systemd, Plasma che non torna,
nessun Plasma in esecuzione e `--dry-run`. Tutte le esecuzioni del test
cercano solo il plasmashell finto: la sessione reale non viene mai vista.

## 6. F5.4 Aggiornamenti della distribuzione

### 6.1 Gestore e comando

Si rileva ciò che c'è davvero, non il nome della distribuzione:
`pacman` + `/var/lib/pacman/local`, `apt-get` + `/var/lib/dpkg/status`,
`dnf` + `/var/lib/rpm` o `/usr/lib/sysimage/rpm`. Se ce n'è più di uno vince
quello che corrisponde a `ID`, poi a `ID_LIKE` di os-release; altrimenti
l'ordine fisso pacman, apt, dnf. Se non ce n'è nessuno: Unavailable.

| Gestore | Default (solo metadati locali) | Refresh remoto (opt-in) | Conteggio |
|---|---|---|---|
| pacman | `pacman -Qu` (confronta con i database sync già presenti) | `checkupdates` (pacman-contrib: sincronizza una **copia privata** dei database in `/tmp`, mai `/var/lib/pacman`); se manca: locale + nota | righe `nome vecchia -> nuova`; le `[ignored]` (IgnorePkg) sono contate a parte; exit 1 senza output = 0 |
| apt | `apt-get -s -q -o Debug::NoLocking=true dist-upgrade` (simulazione da utente normale) | impossibile senza root → locale + nota | righe `Inst nome [vecchia]` = aggiornamenti di pacchetti installati; le nuove dipendenze (`Inst` senza `[…]`) non contano |
| dnf (4 e 5) | `dnf -q -C check-update` (solo cache) | `dnf -q --refresh check-update` (cache dell'utente) | righe `nome.arch versione repo`, exit 100; la sezione "Obsoleting" non è contata due volte |

Età dei metadati: pacman = il più recente di `/var/lib/pacman/sync/*`; apt
= `update-success-stamp` o la directory `lists`; dnf = le cache in
`/var/cache/libdnf5` e `/var/cache/dnf`; dopo un refresh remoto = adesso.
Oltre 7 giorni sono **stale**: il conteggio resta, con un avviso
("metadata … out of date").

### 6.2 Pianificazione e sicurezza

- `UpdatesService` nel backend: due job indipendenti (pacchetti, Flatpak),
  ciascuno con al massimo un comando in corso (mai sovrapposti). Il primo
  controllo parte 30 s dopo l'avvio del backend, il successivo
  `updatesIntervalMinutes` dopo la **fine** del precedente (default 60;
  valori 0 = off, 15, 30, 60, 180, 360, 720, 1440). Con il refresh remoto
  l'intervallo minimo è di 60 minuti.
- Un cambiamento dei database (aggiornamento, refresh di sistema: watch di
  `/var/lib/pacman/local` e `sync`, `/var/lib/dpkg`, `apt/lists`, rpmdb,
  `.changed` di Flatpak) provoca **un** nuovo controllo dopo 30 s di quiete.
  Durante un controllo viene solo segnato e rifatto dopo.
- Ogni comando: eseguibile diretto trovato nel PATH, argomenti fissi, nessuna
  shell, nessun sudo/pkexec, stdin da `/dev/null`, `LC_ALL=C`, `NO_COLOR`,
  `TERM=dumb`, `DEBIAN_FRONTEND=noninteractive`. Timeout 120 s e limite di
  4 MiB per stream: oltre, SIGKILL all'intero **process group**. Stop del
  backend: SIGTERM, attesa limitata, SIGKILL.
- Un errore (exit inatteso, timeout, output malformato) è Error con la prima
  riga di stderr ripulita (massimo 200 caratteri): niente conteggio, ma
  `lastSuccess` resta.
- Mai `install`, `upgrade`, `-S`, `-y`, `update`. Verificato a runtime
  (§16): nessuna connessione di rete e database invariati.

## 7. F5.5 Aggiornamenti Flatpak

- Senza `flatpak` nel PATH: Unavailable "Flatpak is not installed", mai 0.
- Installazioni: `user`, `system` e quelle configurate in
  `/etc/flatpak/installations.d/*.conf` (`[Installation "nome"]`, nomi
  validati).
- Per installazione: `flatpak remote-ls --user|--system|--installation=N
  --updates --cached --app|--runtime --columns=ref`. `--cached` usa i
  summary già scaricati, senza rete. Il refresh remoto opt-in toglie
  `--cached`. Nessun `flatpak update`.
- **Che cosa conta**: il numero di ref distinti con un aggiornamento, per
  installazione, sommato (app + runtime). La stessa ref installata sia
  nell'utente sia nel sistema conta due volte, perché sono due
  aggiornamenti. Le estensioni `.Locale`/`.Debug` non sono elencate da
  `remote-ls` senza `--all` e non contano. Il dettaglio separa app e
  runtime.
- Se anche una sola installazione fallisce (per esempio "No cached summary
  for remote 'flathub'"), la metrica è Error con l'installazione e il motivo,
  senza conteggio: una somma parziale sottostimerebbe.
- Età dei metadati: il file più recente tra i summary in cache
  (`~/.cache/flatpak/system-cache/summaries` e le cache delle
  installazioni).

## 8. F5.6 Kernel e distribuzione

Concetti e metriche separati. Il kernel viene da `uname(2)` (sysname,
release, version, machine). La distribuzione viene da os-release letto
**come dati**, mai eseguito: righe `CHIAVE=valore`, virgolette singole o
doppie, escape `\" \\ \$ \``, commenti. Le righe malformate (chiave non
valida, virgolette non chiuse, spazi non quotati, `$(...)` non quotato)
vengono scartate. File limitato a 64 KiB. Ordine delle sorgenti:
`/etc/os-release`, `/usr/lib/os-release`, `/etc/lsb-release`,
`/etc/arch-release`, `/etc/fedora-release`, `/etc/redhat-release`,
`/etc/debian_version`. Se nessuna esiste: Unavailable. Entrambe vengono
rilette ogni 10 minuti, non a ogni campione.

## 9. Backend, API e compatibilità

- `schema` passa a **2** (estensione additiva); `api` resta 1, così come
  nomi, metodi e segnali. Nessun metodo nuovo: le metriche nuove sono
  normali oggetti `builtin`; unità nuove `seconds`, `fps`, `count`.
- I client devono già ignorare id e campi sconosciuti. Un frontend F5 con un
  backend F4 (schema 1) vede le sezioni F5 in Pending (testato); un
  Plasmoid F4 con un backend F5 le mostra in modo generico.
- Impostazioni nuove in `GetSettings` / `SetSetting` / `SettingsChanged`:
  `gpuDevice` (`"auto"` o id), `fpsSource` (`"mangohud"`|`"off"`),
  `updatesIntervalMinutes` (0, 15, 30, 60, 180, 360, 720, 1440),
  `updatesRemoteRefresh` (bool). Valori non validi: `InvalidArgument`
  (`aqtruncatctl` exit 4). INI: `gpu/device`, `fps/source`,
  `updates/intervalMinutes`, `updates/remoteRefresh`; le impostazioni
  esistenti restano invariate.
- Un solo backend per tutti i client: uptime, GPU e FPS escono dall'unico
  campione a 1 Hz; gli aggiornamenti si controllano una volta per tutti i
  client.

## 10. CLI

```text
$ aqtruncatctl metrics uptime kernel distribution updates.packages updates.flatpak
Built-in metrics
  uptime            Uptime                42m  since boot
  updates.packages  Package updates       Up to date  pacman · checked 09:40 · metadata from Oct 16, 15:10
  updates.flatpak   Flatpak updates       7 updates  1 app, 6 runtimes · checked 09:40 · metadata from 08:34
  kernel            Kernel                Linux 7.2.10-zen1-1-zen  x86_64
  distribution      Distribution          Arch Linux  rolling · ID arch
$ aqtruncatctl --json metrics gpu:0000:00:02.0 fps
$ aqtruncatctl settings set updatesIntervalMinutes 180
$ aqtruncatctl settings set gpuDevice auto
```

`aqtruncat metrics` (locale, senza backend) aggiunge Uptime, GPU, FPS e
System. Gli aggiornamenti restano del backend.

## 11. Dashboard, Plasmoid, Metrics Bar

- **Dashboard Qt**: sezioni nuove GPU (una scheda per GPU con
  Utilization/Activity, Memory, Temperature; barre solo per un valore
  percentuale valido, altrimenti `N/A` e il motivo nel tooltip), Gaming
  (FPS con sorgente/applicazione, oppure il motivo e l'ultima lettura con un
  avviso se stale; sezione nascosta con sorgente `off`, §5.4), Updates (Packages e Flatpak con gestore, ora del
  controllo e avviso se i metadati sono vecchi o in errore), System
  (Uptime, Kernel, Distribution). Le schede GPU si ricostruiscono solo se
  cambia l'insieme dei dispositivi.
- **Plasmoid**: nessun codice per metrica, perché le nuove sono oggetti
  generici con `items`. La sezione "System" si chiama ora **Built-in
  Metrics** (anche nell'opzione di aspetto); la pagina delle impostazioni
  condivise ha GPU della Metrics Bar, sorgente FPS, intervallo dei controlli
  e refresh remoto, visibili solo con un backend di schema 2. Le scelte
  stanno in funzioni pure di `logic.js` (testate). Niente raccolta né
  comandi nel QML; animazione, riconnessione e delta invariati.
- **Metrics Bar**: id nuovi `uptime`, `gpu.utilization`, `gpu.memory`,
  `gpu.temperature`, `fps`, `updates.packages`, `updates.flatpak`
  (didascalie `UP`, `GPU`, `VRAM`, `GPU°`, `FPS`, `UPD`, `FLAT`), nessuno
  preselezionato. Il limite di tre voci è invariato; kernel e distribuzione
  non sono offerti; un valore mancante resta `—`, mai `0`; `fps` con
  sorgente `off` non compare (§5.4). La sorgente di
  velocità del runner (CPU) non cambia.
- **Settings Qt**: gruppo "Built-in Metrics" con le stesse quattro
  impostazioni (una GPU assente resta "(not present)"); "Restore Defaults"
  le riporta ai valori predefiniti.

## 12. Prestazioni (misurate, Plasma 6.8, i7-11390H, build RelWithDebInfo)

Backend su un bus privato con XDG isolato, 60 s dopo 5 s di assestamento:

| Caso | CPU del backend | RSS |
|---|---|---|
| F4 installato (`/usr/local`, 1.4.2), controlli n/a | 0.100 % di un core | 21.0 MiB |
| F5, controlli aggiornamenti spenti (uptime, GPU, FPS, kernel/distro) | 0.150 % | 21.8 MiB |
| F5 con un giro di controlli nella finestra | 0.283 % + figli 3.2 % (in 60 s) | 22.1 MiB |

Un giro completo costa circa 1,8 s di CPU: `pacman -Qu` circa 0,7 s, con le
firme `gpg` dei database; le quattro chiamate Flatpak circa 1 s. Con
l'intervallo orario di default è circa 0,05 % di un core in media. Nessun
processo resta in background tra i controlli.

## 13. Sicurezza

Nessun privilegio elevato, nessun sudo/pkexec, nessuna installazione,
aggiornamento o rimozione di pacchetti, nessuna shell, nessun comando
derivato da metadati di sistema (os-release è solo testo), nessun refresh
remoto silenzioso, nessuna credenziale, nessuna iniezione o lettura di
memoria per l'FPS, nessuna dGPU svegliata. I comandi sono fissi nel codice;
l'API non accetta comandi dai client.

## 14. Installer e packaging

`install.sh` invariato in tutte le modalità (nessun file installato nuovo).
Versione 1.5.6 in CMake, PKGBUILD, spec RPM (+ changelog),
`debian/changelog`, pagina man e `metadata.json` del Plasmoid
(`packaging-versions` verde). Dipendenze **deboli** nuove, nessuna
obbligatoria: Arch `optdepends` hwdata, flatpak, pacman-contrib, mangohud;
RPM `Suggests` hwdata, flatpak, mangohud; Debian `Suggests` pci.ids,
flatpak, mangohud. La pagina man e l'help di `aqtruncatctl` descrivono le
metriche e le impostazioni nuove.

## 15. Test automatici

51 suite ctest, tutte verdi (build pulita da directory nuova, 0 warning).
Nuove (totali QtTest, righe di dati comprese):

| Suite | Casi | Contenuto |
|---|---:|---|
| `uptime` | 16 | parsing, valori malformati, formati limite, sampler da `<proc>/uptime` |
| `systeminfo` | 14 | os-release come dati (escape, niente espansioni, righe malformate), fallback, file troppo grande, `uname`, refresh ogni 10 min, kernel e distro indipendenti |
| `gpu` | 14 | `/sys` finto: amdgpu completo, campi mancanti, Intel RC6 (pending, 25 %, contatore all'indietro), xe + hwmon, NVML finto, NVML assente, GPU sospesa non letta, ordine e selezione ibrida, framebuffer e classi non display scartati, `pci.ids` |
| `fps` | 12 | output_folder, ultima riga completa, inizio a metà riga, log malformati, nome applicazione, stati disabled/missing/not-configured/inactive/measured/stale, log più recente, formato sconosciuto, intestazione senza righe |
| `packageupdates` | 10 | scelta del gestore per capacità e distro, comandi solo lettura (nessun verbo di modifica, nessuna elevazione), conteggi pacman/checkupdates/apt/dnf4/dnf5/flatpak, installazioni Flatpak, testo d'errore |
| `updatesservice` | 16 | strumenti finti in un PATH privato e una radice finta: pacman (+ stale), refresh remoto opt-in e nota, apt e dnf per capacità, nessun gestore, errore, timeout che uccide il process group, nessuna sovrapposizione, off, ricontrollo dopo un cambiamento, Flatpak assente/conteggi per installazione/errore parziale, stop senza processi residui |

Estese: `apischema` 12 (id/tipi/unità/stati F5, round trip, backend schema
1), `appsettings` 16, `metricsbarmodel` 20, `dashboard` 16,
`settingswindow` 10, `systemmetrics` 9 (report ermetico), `backendipc` 13
(metriche e impostazioni F5 via `aqtruncatctl`, schema 2),
`plasmoidlogic` 20; `installer` 314 controlli (§5.5) e le altre suite invariate.
I test non leggono la GPU, il kernel, la distribuzione o i database dei
pacchetti dell'host. `backendipc`, `plasmoidclient` e
`tools/plasmoid-preview.sh` spengono i controlli aggiornamenti nei loro XDG
isolati.

## 16. Validazione runtime (host Arch, Plasma 6.8 Wayland)

Automatica, su un bus D-Bus privato (`backendipc-bus.conf`, senza service
dir) e con XDG isolato; la sessione Plasma e il backend 1.4.2 dell'utente non
sono stati toccati:

- uptime 42m = `uptime -p` "up 42 minutes"; kernel `7.2.10-zen1-1-zen`
  x86_64; distribuzione Arch Linux (rolling, ID arch).
- GPU: Intel TigerLake-LP GT2 [Iris Xe Graphics] `0000:00:02.0`, i915,
  integrata, attività 30–39 % a desktop acceso; memoria e temperatura
  Unavailable con il motivo; frequenza 300–350 MHz.
- Aggiornamenti reali, solo lettura: `pacman -Qu` → "Up to date" (database
  sync del 16/10); Flatpak → 7 aggiornamenti (1 app, 6 runtime) dai summary in
  cache. Con `strace -f -e connect,execve`: eseguiti solo `pacman -Qu`, i suoi
  `gpg` di verifica e le quattro `flatpak remote-ls … --cached`; **nessuna
  connessione non-Unix** (solo il bus di sistema e quello privato). Impronta
  di `/var/lib/pacman/{local,sync}` e delle installazioni Flatpak
  **identica** prima e dopo.
- FPS: con un log scritto in tempo reale nel formato MangoHud (una riga ogni
  100 ms) in un HOME isolato: prima "no MangoHud log yet", durante "142 FPS ·
  Game · 6.9 ms", 6 s dopo lo stop "not been written for 6 s · Last reading:
  141 FPS", 66 s dopo "no game is logging". Senza configurazione: "MangoHud
  is not installed". Un primo tentativo con righe malformate (virgola
  decimale della locale) non ha mai prodotto un valore; ora è un errore
  esplicito.
- CLI: `aqtruncatctl metrics`/`--json`/`settings set`, `aqtruncat metrics`.
- Dashboard Qt alimentata dal backend reale tramite `BackendClient`,
  offscreen (`dashboard.png`); popup del Plasmoid offscreen chiaro e scuro
  con il backend reale (`plasmoid-*.png`, controlli aggiornamenti spenti dallo
  strumento).
- Prestazioni: §12.

**Da verificare a mano** (sessione dell'utente): Dashboard e Metrics Bar
nella tray reale (voci `GPU`, `UPD`, `FPS` come SNI), Plasmoid nel pannello
reale con i dati F5, un gioco vero con MangoHud, una macchina con GPU
AMD/NVIDIA/ibrida e i sistemi Debian/Ubuntu e Fedora con un desktop (apt e
dnf sono stati provati solo in container: `apt-get -s` come utente normale
→ 2 aggiornamenti contati, `dnf -C check-upgrade` come uid 1500 → exit
100, 32 pacchetti).

## 17. Limiti noti

- FPS solo con MangoHud che registra su file (vedi §5.3).
- Intel: attività RC6 invece dell'utilizzo per motore; temperatura e VRAM
  delle iGPU non esistono come sensori propri.
- apt: niente refresh remoto senza root (dipende dal timer del sistema o da
  PackageKit); dnf da utente con `--refresh` scrive nella propria cache
  (`~/.cache/libdnf5`).
- I conteggi riflettono i metadati locali: con metadati vecchi si vede
  "out of date" e un numero possibilmente più basso del reale.
- Flatpak: una ref in errore in una qualunque installazione blocca il
  conteggio totale (scelta voluta).
- Nessuna traduzione; nessuna temperatura CPU (non richiesta).

## 18. File

Nuovi: `src/metrics/{uptime,systeminfo,gpu,fps,packageupdates}.{h,cpp}`,
`src/app/{boundedprocess,updatesservice}.{h,cpp}`,
`src/app/processgroup.h`,
`tests/tst_{uptime,systeminfo,gpu,fps,packageupdates,updatesservice}.cpp`,
questo documento e le sue immagini.

Modificati: `src/metrics/{systemmetrics,textparse,compactformat,metricsformat}.*`,
`src/api/{apischema,backendservice,backendclient}.*`,
`src/app/{appsettings,metricsampler,metricsbarids,metricsbarmodel,dashboardwindow,settingswindow,trayapp,custommetricsservice}.*`,
`src/cli/metricscommand.cpp`, `src/ctl/main.cpp`, `src/CMakeLists.txt`,
`plasmoid/package/{metadata.json,contents/code/logic.js,contents/config/main.xml,contents/ui/{FullRepresentation,configGeneral,configAppearance}.qml}`,
`tests/{CMakeLists.txt,plasmoid/tst_logic.qml,tst_apischema,tst_appsettings,tst_backendipc,tst_dashboard,tst_metricsbarmodel,tst_plasmoidclient,tst_settingswindow,tst_systemmetrics}.cpp`,
`tools/plasmoid-preview.sh`, `CMakeLists.txt`, `data/aqtruncat.1`,
`packaging/{arch/PKGBUILD,rpm/aqtruncat.spec,debian/changelog,debian/control}`,
`README.md`, `docs/F4.1-headless-backend-ipc-cli.md`, `requirements.md`,
`MEMORY.md`.
