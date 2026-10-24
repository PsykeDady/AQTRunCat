# AQTRunCat — F4.1 Backend headless, API locale e CLI

F4 è diviso in due parti:

- **F4.1** (questo documento): un backend indipendente da qualsiasi frontend,
  un'API IPC locale documentata e una CLI di riferimento;
- **F4.2**: il Plasmoid nativo per KDE Plasma (completato il 2026-10-21,
  `docs/plasma-widget.md`). F4.1 è il suo prerequisito.

Dopo F4.1 AQTRunCat è un servizio di metriche per la sessione utente. Il
frontend Qt/SNI, la CLI e, in futuro, il Plasmoid o client GTK, TUI e Waybar
leggono tutti lo stesso stato autorevole.

```text
                     aqtruncatd  (QtCore + QtDBus, nessuna GUI)
               ┌──────────────────────────────────────────┐
               │ MetricSampler (CPU, memoria, rete, disco)│
               │ CustomMetricsService (F1) + adattatori   │
               │   Claude Code (F2) / Codex (F3)          │
               │ CpuMonitor → RunnerSpeedController (fps) │
               │ AppSettings (AQTRunCat.ini) · Autostart  │
               └───────────────────┬──────────────────────┘
                     D-Bus di sessione · JSON versionato
          io.github.psykedady.AQTRunCat  /io/github/psykedady/AQTRunCat
                    interfaccia io.github.psykedady.AQTRunCat1
              ┌────────────────────┼─────────────────────┐
        aqtruncat run         aqtruncatctl         altri client
      (tray Qt/SNI, client)   (CLI di riferimento)  (Python/GTK/Rust,
                                                    Plasmoid in F4.2)
```

## 1. Analisi iniziale

Prima di F4.1 il processo `aqtruncat run` (Qt Widgets) possedeva tutto:
`MetricSampler`, `CustomMetricsService` con gli adattatori, `CpuMonitor` e
`RunnerSpeedController`, `AutostartManager`, la gestione delle integrazioni,
la Dashboard, Settings, la Metrics Bar e le icone SNI.

L'analisi ha mostrato che quasi tutto il motore usa **già solo QtCore**:
campionatore, custom metrics, scheduler, adattatori (processi esterni),
impostazioni (`QSettings`), autostart e calcolo della velocità. Dipendono da
Qt Widgets o dal tray solo `TrayApp`, `RunnerAnimator`/`RunnerIcon`,
`DashboardWindow`, `SettingsWindow` e `MetricsBarModel`/le icone della barra.

Non serviva quindi riscrivere niente: bastava **spostare** la proprietà del
motore in un processo diverso e far parlare il frontend con quel processo.
Le modifiche strutturali sono state:

- separare le librerie CMake: `aqtruncat_engine` (QtCore), `aqtruncat_api`
  (QtCore + QtDBus) e `aqtruncat_desktop` (Widgets, solo UI);
- introdurre `CustomMetricsModel`, l'interfaccia astratta delle sorgenti che
  la UI già usava, implementata dal servizio locale e dal client IPC;
- estrarre gli id della Metrics Bar (`metricsbarids`) dal modello grafico;
- aggiungere `AppSettings::reload()`, che rilegge l'INI e notifica solo i
  valori cambiati.

## 2. Modello di processo: opzione B, backend dedicato

| | A — `aqtruncat --headless` | **B — `aqtruncatd` dedicato** (scelta) |
|---|---|---|
| Dipendenze del backend | l'eseguibile linka Widgets, Gui e i runner (QtSvg…) | QtCore + QtDBus (verificato con `ldd`: Qt6Core, Qt6DBus, libdbus) |
| Attivazione D-Bus | `Exec` dovrebbe passare un flag a un binario GUI | file `.service` con `Exec=…/aqtruncatd` |
| Frontend chiuso | il processo headless e quello del tray sono lo stesso binario: confusione su chi "possiede" il motore | ruoli separati: il tray è solo un client |
| Memoria del backend | ~60 MB (stack grafico caricato) | ~17 MB |
| Processi | 2 (headless + tray) | 2 (backend + tray); 1 senza tray |
| Installer e pacchetti | invariati | un binario in più (gestito) |

Conseguenze:

- `aqtruncat` (avvio, autostart, voce di menu, `aqtruncat run`) resta valido:
  ora è il frontend e si collega al backend, avviandolo se serve;
- il motore esiste **una volta sola** per sessione. Gli adattatori Claude e
  Codex girano solo nel backend: nessun frontend campiona da solo;
- `aqtruncatctl` è un client puro dell'API.

## 3. Ciclo di vita

**Istanza unica.** `aqtruncatd` esporta l'oggetto e poi chiede il nome
`io.github.psykedady.AQTRunCat` con `DoNotQueue` e senza consentire la
sostituzione. Se il nome ha già un proprietario, la seconda istanza stampa
`another AQTRunCat backend already owns io.github.psykedady.AQTRunCat` ed
esce con 0. Il campionamento parte solo **dopo** aver ottenuto il nome, così
un doppione non fa lavoro.

**Attivazione.** Due strade, senza obbligo di systemd:

1. **D-Bus activation** (principale): `share/dbus-1/services/io.github.psykedady.AQTRunCat.service`
   con `Exec=<bindir>/aqtruncatd`. Qualsiasi chiamata con autostart (il
   frontend, `aqtruncatctl`, `gdbus call`, un client Python) avvia il
   backend. Con dbus-broker serve `ReloadConfig` dopo l'installazione:
   l'installer lo chiede (best effort).
2. **Ripiego** del frontend: se il bus risponde `ServiceUnknown` /
   `NameHasNoOwner` (ad esempio un file di servizio non ancora riletto),
   `aqtruncat run` avvia `aqtruncatd` che si trova **accanto a sé**, staccato
   (`startDetached`) e al massimo una volta ogni 10 s. Un avvio
   contemporaneo da parte di qualcun altro non crea duplicati: il secondo
   backend perde la gara per il nome ed esce. `aqtruncatctl` fa lo stesso
   (avvia l'`aqtruncatd` accanto a sé e aspetta il nome fino a 5 s) quando
   l'attivazione non è disponibile; `--no-start` disattiva entrambe le
   strade.

**Arresto.** SIGTERM o SIGINT (chiusura pulita: gli adattatori ancora attivi
vengono fermati con il loro process group), oppure la fine della sessione
(`org.freedesktop.DBus.Local.Disconnected` al logout). Il backend **non**
termina quando l'ultimo client si scollega: resta disponibile per CLI,
Plasmoid e altri client fino al logout.

**Combinazioni di frontend:**

(Verificato in F4.2, `docs/plasma-widget.md` §8 e §14.)

| Qt/SNI | Plasmoid (F4.2) | Comportamento |
|---|---|---|
| attivo | — | esperienza tray invariata; il tray avvia il backend |
| — | attivo | il Plasmoid usa il backend (attivazione D-Bus), nessun tray |
| attivo | attivo | entrambi leggono un solo backend |
| — | — | il backend parte solo su richiesta (CLI, altri client) |

Installare o abilitare un frontend non abilita mai l'altro. L'autostart
(Settings › Start at login, oppure `autostart true`) resta quello di M9:
avvia il frontend `aqtruncat`, che porta con sé il backend. Senza tray non
serve un autostart del backend, perché l'attivazione su richiesta basta.

**Crash o riavvio del backend.** I client seguono il nome con
`QDBusServiceWatcher`. Il frontend, quando il nome perde il proprietario:

- mantiene i valori per un **periodo di tolleranza di 3 s**, così un riavvio
  rapido (ad esempio un aggiornamento) non provoca sfarfallio;
- poi mostra metriche e sorgenti come *unavailable* con il messaggio
  "the AQTRunCat backend (aqtruncatd) is not running";
- ritenta ogni 3 s e, se serve, usa il ripiego di avvio.

Appena compare un nuovo proprietario il client chiede di nuovo `GetState`.

**Aggiornamenti.** L'installer chiude con SIGTERM solo i processi dell'utente
il cui eseguibile è *esattamente* `bin/aqtruncat` o `bin/aqtruncatd` di
quella installazione, aspetta fino a 10 s e non riavvia niente. Al
successivo avvio del frontend o a una chiamata di un client parte il nuovo
backend.

## 4. Trasporto e identità

- Trasporto: **D-Bus di sessione** dell'utente. Nessun socket di rete,
  nessun server HTTP.
- Nome: `io.github.psykedady.AQTRunCat`.
- Oggetto: `/io/github/psykedady/AQTRunCat`.
- Interfaccia: `io.github.psykedady.AQTRunCat1`. L'introspezione standard è
  disponibile (`busctl --user introspect …`).
- Tipi D-Bus: solo `s` (stringhe JSON UTF-8 compatte), `b` e `t`. Nessun
  tipo Qt nel contratto.

**Versionamento.**

- `api` = 1 è la versione *maggiore* e fa parte del nome dell'interfaccia
  (`…AQTRunCat1`). Una modifica incompatibile introdurrebbe
  `…AQTRunCat2`, eventualmente esposta in parallelo.
- `schema` è la versione dei payload JSON: 1 fino a F4.2, **2 da F5**
  (2026-10-22: metriche built-in uptime, GPU, FPS, aggiornamenti, kernel e
  distribuzione, unità `seconds`/`fps`/`count`, quattro impostazioni; vedi
  `docs/additional-built-in-metrics.md`), **3 da 2.1.2** (campo `enabled`
  delle Custom Metrics e metodo `SetCustomMetricEnabled`, vedi
  `docs/custom-metrics-commands.md` §8), **4 da 2.1.4** (campo `showBar`,
  §10), **5 da 2.1.5** (gruppi: `kind` `group`, `data.groupId`,
  `data.configuredEnabled`, §11), **6 da 2.1.6** (metodo
`ReloadCustomMetrics`, capability `custom-metrics-reload` e campo
`warnings` delle Custom Metrics, §12). Le
aggiunte compatibili (nuovi
  campi, nuove capability, nuovi id di metrica) non cambiano `api`.
- I client **devono ignorare i campi sconosciuti** e trattare un valore
  sconosciuto di `state` come `error`.
- `capabilities` (in `GetInfo`) elenca le funzioni opzionali: `metrics`,
  `metrics-delta`, `runner`, `settings`, `set-setting`, `custom-metrics`,
  `integrations` e, da F4.2, `runners` (catalogo dei runner, §5.4.1); da
  2.1.6 `custom-metrics-reload` (`ReloadCustomMetrics`).

## 5. Contratto API

### 5.1 Metodi

| Metodo | Firma | Risultato |
|---|---|---|
| `GetInfo` | `() → s` | `{api, schema, service:"aqtruncatd", version, capabilities[], generation, startedAt, customMetricsDirectory}` |
| `GetState` | `() → s` | `{api, schema, info, generation, sampleTime, metrics[], runner, settings, integrations[]}`: lo stato completo, punto di partenza e di recupero |
| `GetMetrics` | `() → s` | `{schema, generation, sampleTime, metrics[]}` |
| `GetRunner` | `() → s` | `{runnerId, speedMode, fps, targetFps}` |
| `GetSettings` | `() → s` | oggetto impostazioni (§7) |
| `GetIntegrations` | `() → s` | array di integrazioni (§5.4) |
| `GetRunners` (F4.2) | `() → s` | catalogo dei runner con file dei fotogrammi e ordine di riproduzione (§5.4.1); solo con la capability `runners` |
| `SetSetting` | `(s key, s jsonValue) → ()` | valida e salva; l'effetto arriva come `SettingsChanged` / `RunnerChanged` |
| `RegisterCustomMetric` | `(s path) → ()` | registra un file Custom Metrics (stesse regole di Settings) |
| `UnregisterCustomMetric` | `(s path) → ()` | rimuove la registrazione esplicita |
| `SetCustomMetricEnabled` (2.1.2, schema 3) | `(s path, b enabled) → ()` | scrive `"enabled"` nel file della Custom Metric (sostituzione atomica, il resto del file invariato) e lo applica subito: disabilitata non viene più eseguita né mostrata. `NotFound` per una sorgente sconosciuta, `InvalidArgument` per la sorgente di un'integrazione (si usa `SetIntegrationEnabled`), `Failed` se il file non si può scrivere |
| `ReloadCustomMetrics` (2.1.6, schema 6) | `() → s` | rilegge tutta la configurazione Custom Metrics (file registrati, cartella di scoperta, gruppi e figli) e applica ciò che è cambiato senza riavvio; risponde `{added[], changed[], removed[], unchanged, errors[], warnings[]}` (voci `{id, sourceId, name, message}`; `errors` = sorgenti con configurazione non valida, che restano visibili in `error`; `warnings` = un avviso per voce, la metrica funziona: icona mancante, valore fuori range). Ogni Custom Metric ha `warnings[]` con gli avvisi della sorgente (icona mancante); un valore fuori range è sulla sua voce (`warning: true` e il motivo in `detail`). Il `MetricsChanged` con il nuovo insieme arriva **prima** della risposta. `Failed` se una cartella non è leggibile: in quel caso nulla è cambiato. Vedi `docs/custom-metrics-commands.md` §12 |
| `SetIntegrationEnabled` | `(s id, b enabled) → s` | `enable`/`disable` dell'adattatore; restituisce il suo messaggio |
| `SetIntegrationLiveUpdates` | `(s id, b on) → s` | `live-updates on|off` dell'adattatore |

`jsonValue` è un testo JSON (`"\"slow-down\""`, `true`, `["cpu","memory"]`).
Una parola nuda che non è JSON viene presa come stringa (`slow-down`).

**Errori** (nome D-Bus, messaggio leggibile):

| Errore | Quando |
|---|---|
| `io.github.psykedady.AQTRunCat1.Error.InvalidArgument` | valore o chiave non validi, chiave in sola lettura, file Custom Metrics non valido |
| `io.github.psykedady.AQTRunCat1.Error.NotFound` | integrazione sconosciuta o adattatore non installato; file non registrato |
| `io.github.psykedady.AQTRunCat1.Error.Failed` | l'adattatore ha fallito il comando; il file di una Custom Metric non si può scrivere |

I metodi delle integrazioni rispondono **in differita**, quando il comando
dell'adattatore è terminato; gli altri client continuano a essere serviti nel
frattempo. Gli argomenti passati all'adattatore sono fissi
(`enable`, `disable`, `live-updates on|off`): l'API non esegue mai comandi
forniti dal client.

### 5.2 Segnali

| Segnale | Argomenti | Quando |
|---|---|---|
| `MetricsChanged` | `(t generation, s changes)` | almeno una metrica è cambiata, aggiunta, rimossa o spostata |
| `RunnerChanged` | `(s runner)` | runner, modalità di velocità, fps applicati o fps obiettivo cambiati |
| `SettingsChanged` | `(s settings)` | una qualunque impostazione esposta è cambiata (da qualsiasi scrittore) |
| `IntegrationsChanged` | `(s integrations)` | lo stato di gestione di un'integrazione è cambiato |

`changes` ha la forma:

```json
{"schema":1,"generation":352,"sampleTime":73512.004,
 "metrics":[{…solo le metriche cambiate o nuove…}],
 "removed":["custom:/percorso/rimosso.json"],
 "order":["cpu","memory",…,"custom:/…"]}
```

`sampleTime` (in `GetState`, `GetMetrics` e in ogni delta) è l'istante
dell'ultimo campione di sistema in secondi di `CLOCK_MONOTONIC` (lo stesso
orologio per tutti i processi della macchina), oppure `null` prima del primo
campione. Serve per calcolare intervalli e per limitare la cadenza di
presentazione. Non è un'ora del giorno.

### 5.3 Schema di una metrica

Ogni metrica è un oggetto con **tutti** i campi seguenti (eventualmente
`null`):

| Campo | Tipo | Significato |
|---|---|---|
| `id` | string | identità stabile (§5.3.1) |
| `category` | `builtin` \| `integration` \| `custom` | classificazione di Dashboard e Settings |
| `kind` | `system` \| `snapshot` \| `command` | provenienza: campionatore interno, file snapshot, comando M6 |
| `integration` | string \| null | `claude-code`, `codex`… solo per `category = integration` |
| `kind` `group` (schema 5) | — | un gruppo di Custom Metrics (v2F1.5): contenitore mai eseguito, `state` `ok` (o `error` se il suo `metric.json` non è valido), titolo e icona, nessun valore né `items`. I figli sono metriche `command` con `data.groupId` = `data.sourceId` del gruppo. I client mostrano il gruppo con i figli abilitati dentro e lo nascondono se non ne ha |
| `showBar` (schema 4) | bool | solo `custom` e `integration`: il `display.showBar` effettivo della configurazione (assente = `true`). `false`: i client non disegnano barre per la metrica né per le sue voci; `normalized` resta nel payload. Assente con un backend precedente = barre come prima |
| `enabled` (schema 3) | bool | solo `custom` e `integration`: il campo `"enabled"` del file (assente = `true`). `false`: `state` `unavailable`, `message` "Disabled.", nessun valore né `items`; i frontend non la mostrano (Settings sì, per riattivarla). Assente con un backend precedente = abilitata |
| `title` | string | titolo da mostrare |
| `state` | `ok` \| `pending` \| `unavailable` \| `error` | stato attuale |
| `message` | string | spiegazione di `pending`/`unavailable`/`error` (sicura da mostrare) |
| `value` | number \| null | valore numerico, **mai 0 inventato**: `null` se non c'è (e per le metriche solo testuali, `kernel` e `distribution`) |
| `unit` | `percent` \| `bytes-per-second` \| `seconds` \| `fps` \| `count` \| null | unità di `value` (le ultime tre da schema 2) |
| `formattedValue` | string | valore già formattato (`22.4 %`, `8.9 KiB/s`, `26% used`) |
| `detail` | string | riga secondaria (`17.7 GiB / 31.1 GiB used`) |
| `compactValue` | string \| null | testo per la Metrics Bar / pannelli stretti (`22%`) |
| `compactLabel` | string | etichetta breve (`CPU`, `cc7d`) |
| `normalized` | number \| null | 0…1 se esiste un intervallo naturale |
| `updatedAt` | string \| null | ISO 8601 UTC dell'ultimo aggiornamento della sorgente (snapshot); `null` per le metriche di sistema, che sono sempre all'ultimo campione |
| `icon` | object \| null | `{type:"theme"\|"file", value, resolved}` oppure `{type:"symbol", value}` |
| `items` | array | voci multiple (quote Claude/Codex, snapshot M6 con più metriche) |
| `data` | object | dati grezzi della categoria (§5.3.2) |

Ogni voce di `items` ha: `id` (string|null), `title`, `formattedValue`,
`detail`, `normalized` (number|null), `compactValue` (string|null),
`compactLabel` e `warning` (bool; per esempio "Out of date").

#### 5.3.1 Id

- Built-in: `cpu`, `memory`, `swap`, `network.download`, `network.upload`,
  `storage.root`, `storage.home` (solo se Home è un filesystem distinto da
  `/`), `disk.read`, `disk.write`; da schema 2 anche `uptime`,
  `gpu:<dispositivo>` (una per GPU, slot PCI come `0000:00:02.0`, in ordine
  di id; nessuna se non c'è GPU), `fps`, `updates.packages`,
  `updates.flatpak`, `kernel`, `distribution`. Prima del primo campione sono
  `pending` con `message` "waiting for the first sample".
- La Metrics Bar accetta inoltre `gpu.utilization`, `gpu.memory`,
  `gpu.temperature` (la GPU dell'impostazione `gpuDevice`), che non sono
  oggetti dell'API: i valori stanno nelle voci della `gpu:<dispositivo>` con
  `data.selected = true`.
- Integrazioni e Custom Metrics: `custom:<percorso canonico del file>`.
  Sono gli stessi id usati dalla Metrics Bar (`metricsBarItems`), che
  accetta anche `custom-item:<id voce>:<percorso>` per una singola voce.

#### 5.3.2 `data`

- Built-in: `section {state, message}` (stato della sezione del
  campionatore) più i valori grezzi:
  - `cpu`: `logicalCpus`;
  - `memory`: `usedBytes`, `totalBytes`, `availableBytes`, `availableIsEstimate`;
  - `swap`: `present`, `usedBytes`, `totalBytes`;
  - `network.*`: `countedInterfaces` e `interfaces[{name, kind, counted, downloadBytesPerSecond, uploadBytesPerSecond, counterReset}]`;
  - `disk.*`: `countedDevices`;
  - `storage.*`: `path`, `usedBytes`, `totalBytes`, `availableBytes`;
  - schema 2 (F5): `uptime` `seconds`; `gpu:*` `device`, `card`,
    `driver`, `vendor`, `vendorId`, `deviceId`, `model`, `integrated`,
    `bootVga`, `suspended`, `selected`, `frequencyMHz` e per campo
    `utilization`/`memory`/`temperature` `{state, message, …}` (gli stessi
    tre campi sono le `items` `utilization`, `memory`, `temperature`);
    `fps` `source`, `condition`, `fps`, `frametimeMs`, `lastFps`,
    `ageSeconds`, `application`; `updates.*` `manager`, `count`, `checking`,
    `lastSuccess`, `metadataTime`, `metadataStale`, `remote`, `note`,
    `ignored` (+ `apps`, `runtimes`, `installations[]` per Flatpak);
    `kernel` `sysname`, `release`, `version`, `machine`; `distribution`
    `name`, `prettyName`, `id`, `idLike[]`, `versionId`, `version`,
    `codename`, `buildId`, `variant`, `source`. Dettagli e semantica:
    `docs/additional-built-in-metrics.md` §2.
- Integrazioni e Custom Metrics: `sourceId`, `path`, `explicitPath`,
  `explicit`, `discovered`, `configuredId`, `hasSnapshot`; da schema 5
  `groupId` (il `sourceId` del gruppo di un figlio, vuoto al primo livello;
  `configuredId` di un figlio è gerarchico, `dice-metrics/d20`) e
  `configuredEnabled` (il campo `enabled` del file stesso; `enabled` resta
  lo stato effettivo, falso anche sotto un gruppo disabilitato). Sono percorsi dei
  file di metrica di AQTRunCat, che l'utente ha registrato o che stanno nella
  cartella di scoperta. Non vengono mai esposti i file privati dei provider.

Esempio (`aqtruncatctl --json metrics cpu`):

```json
[{"category":"builtin","compactLabel":"CPU","compactValue":"22%",
  "data":{"logicalCpus":8,"section":{"message":"","state":"ok"}},
  "detail":"","formattedValue":"22.4 %","icon":null,"id":"cpu",
  "integration":null,"items":[],"kind":"system","message":"",
  "normalized":0.2243,"state":"ok","title":"CPU","unit":"percent",
  "updatedAt":null,"value":22.43}]
```

Una metrica multivalore (quota Claude Code) ha `category:"integration"`,
`integration:"claude-code"`, `kind:"command"`, e in `items` una voce per
finestra (`five_hour`, `seven_day`…) con `formattedValue` "26% used",
`detail` "Resets Oct 23, 20:00 · as of 08:45", `normalized` 0.26 e
`warning`.

### 5.4 Runner e integrazioni

`runner`: `{runnerId, speedMode:"normal"|"slow-down", fps, targetFps}`.
`fps` è la frequenza applicata (con la rampa di M1), `targetFps` quella
obiettivo calcolata dal carico CPU. Ogni frontend anima **localmente** alla
frequenza ricevuta: la CPU viene campionata una volta sola, nel backend, e
sul bus non passa nessun fotogramma.

#### 5.4.1 Catalogo dei runner (F4.2)

`GetRunners` serve ai frontend che non caricano i runner da soli (il
Plasmoid, client GTK o Waybar): `{schema, requested, selected,
defaultRunnerId, runners[]}`, dove ogni runner ha `id`, `displayName`,
`author`, `source` (`bundled`|`user`), `valid`, `error`, `monochrome`,
`frameWidth`, `frameHeight`, `frames` (percorsi assoluti dei PNG) e
`sequence` (ordine di riproduzione, indici in `frames`). `selected` è il
runner da mostrare per il `runnerId` salvato, con lo stesso ripiego del
tray. Dettagli: `docs/plasma-widget.md` §4.2.

Integrazione:
`{id, title, adapterInstalled, statusKnown, enabled, userPackage, providerFound, liveUpdates:"off"|"on"|…, lastLiveReport, liveError, metricId}`.
Questi campi descrivono solo lo stato di gestione. `metricId` è l'id della
metrica corrispondente, se l'integrazione è abilitata. Non vengono esposti
percorsi di configurazione dei provider, token o output grezzi
dell'adattatore.

## 6. Eventi e aggiornamenti

- **Stato iniziale completo** con `GetState`, poi **delta** con
  `MetricsChanged`.
- Il backend ricostruisce l'array delle metriche a ogni campione o a ogni
  esecuzione di Custom Metrics e lo confronta con l'ultimo pubblicato. Se
  nulla è cambiato non emette niente. Altrimenti incrementa `generation` di 1
  ed emette solo le metriche cambiate, gli id rimossi e l'ordine completo.
- **Coalescenza**: tutto ciò che cambia nello stesso giro dell'event loop
  (un campione di sistema e un'esecuzione di custom metrics) produce un
  solo segnale.
- **Frequenza**: il campionatore di sistema lavora a 1 Hz, quindi circa 1–2
  `MetricsChanged` al secondo. Il payload tipico è di qualche kB, mentre lo
  stato completo pesa circa 13 kB. Gli fps del runner cambiano di rado
  (`RunnerChanged`, circa 1 ogni 3 s sotto carico variabile).
- **Recupero**: un client tiene `generation`. Se arriva `generation ≤
  corrente` il segnale si ignora (vecchio). Se arriva `corrente + 1` si
  applica il delta. In tutti gli altri casi (salto, avvio, nuovo
  proprietario del nome) si richiede `GetState`. Lo stesso vale se `order`
  contiene id sconosciuti.
- Non c'è un campionatore per client: i segnali sono broadcast e il numero
  di client non cambia il lavoro del backend.

## 7. Impostazioni

Esiste **un solo** file di configurazione: `AQTRunCat.ini` (lo stesso di
M5–F3, nessuna migrazione). Il backend ne è il proprietario logico:
applica gli effetti (rete, velocità, autostart, Custom Metrics) e notifica
i client.

| Chiave | Tipo / valori | Scrivibile |
|---|---|---|
| `runnerId` | id del runner | sì |
| `speedMode` | `normal` \| `slow-down` | sì |
| `networkMode` | `automatic` \| `manual` | sì |
| `networkInterfaces` | array di nomi di interfaccia (max 64) | sì |
| `dashboardRefreshSeconds` | 1 \| 2 \| 5 | sì |
| `autostart` | bool | sì |
| `metricsBarEnabled` | bool | sì |
| `metricsBarItems` | array, max 3 id distinti e noti | sì |
| `customMetricFiles` | array di percorsi | no: usare `RegisterCustomMetric` / `UnregisterCustomMetric` |
| `gpuDevice` (schema 2) | `"auto"` \| id di GPU (`"0000:03:00.0"`, `"platform:…"`) | sì |
| `fpsSource` (schema 2) | `"mangohud"` \| `"off"` | sì |
| `updatesIntervalMinutes` (schema 2) | 0 (off) \| 15 \| 30 \| 60 \| 180 \| 360 \| 720 \| 1440 | sì |
| `updatesRemoteRefresh` (schema 2) | bool (refresh remoto dei metadati, opt-in) | sì |

Propagazione:

- **API / CLI**: `SetSetting` scrive tramite `AppSettings` e emette
  `SettingsChanged`.
- **Finestra Settings del frontend Qt**: scrive lo stesso INI. Il backend
  osserva il file e la sua cartella (le scritture sono atomiche), rilegge
  dopo 100 ms con `AppSettings::reload()` ed emette `SettingsChanged`.
- **Altri scrittori** (`aqtruncat autostart on|off`, modifica manuale)
  seguono la stessa strada.
- I client rileggono le impostazioni a ogni `SettingsChanged`; il frontend
  Qt aggiorna Metrics Bar, runner e finestre aperte.

Le integrazioni restano gestite dai loro adattatori (F2/F3), che sono i
proprietari della propria configurazione: l'API invoca solo i loro comandi
di gestione.

## 8. CLI `aqtruncatctl`

```text
aqtruncatctl [--json] [--no-start] COMMAND

  status                         backend, runner e integrazioni
  metrics [ID...] [--category builtin|integration|custom]
  watch                          variazioni in tempo reale (Ctrl+C)
  integrations
  integration enable|disable ID
  integration live-updates ID on|off
  settings
  settings set KEY VALUE
  custom add|remove PATH
  custom reload                  rilegge le Custom Metrics (2.1.6)
```

| Codice di uscita | Significato |
|---|---|
| 0 | successo |
| 1 | il backend ha riportato un errore (`Error.Failed`, altro) |
| 2 | riga di comando non valida |
| 3 | backend non disponibile (con `--no-start`, oppure attivazione fallita) |
| 4 | richiesta rifiutata (`InvalidArgument`, `NotFound`) |

`--json` stampa il JSON dell'API così com'è: un documento per i comandi
singoli e, per `watch`, una riga JSON per ogni `MetricsChanged` (l'oggetto
`changes`; lo stato iniziale si legge con `metrics --json`). L'output leggibile è pensato per le persone
e può cambiare; per gli script si usa `--json`.

Esempi (output reale, sistema di prova):

```text
$ aqtruncatctl status
Backend:       running (aqtruncatd 1.4.1, API 1, schema 1, since 2026-10-21 09:32)
Runner:        cat · normal · 13 fps
Metrics:       9 built-in · 2 integrations · 0 custom
Integrations:  Claude Code: on, live updates off · Codex: on, live updates off

$ aqtruncatctl metrics --category builtin
Built-in metrics
  cpu               CPU                   22.4 %
  memory            Memory                57.0 %  17.7 GiB / 31.1 GiB used
  swap              Swap                  11.8 %  1.9 GiB / 15.9 GiB
  network.download  Network download      8.9 KiB/s
  …

$ aqtruncatctl settings set metricsBarItems '["cpu","memory"]'
metricsBarItems set
$ aqtruncatctl settings set speedMode fast
aqtruncatctl: speedMode must be "normal" or "slow-down"      (exit 4)
$ aqtruncatctl --no-start status
aqtruncatctl: the AQTRunCat backend is not running           (exit 3)
```

Tutti gli eseguibili (`aqtruncat`, `aqtruncatd`, `aqtruncatctl`, gli
adattatori) stampano la versione con `-v` / `--version`
(`aqtruncatctl 1.4.1 (API 1)`); il menu del tray la mostra come voce
disabilitata sopra "Quit AQTRunCat".

Nell'output leggibile gli id `custom:` (che contengono percorsi) non sono
mostrati; in `--json` ci sono.

## 9. Frontend Qt/SNI

`aqtruncat run` usa `BackendClient` (in `aqtruncat_api`):

- Dashboard, Metrics Bar e Settings › Custom Metrics ricevono lo snapshot di
  sistema e le sorgenti ricostruiti dal JSON (`systemSnapshotFromMetrics`,
  `sourcesFromMetrics`), quindi la UI di M4–F3 è rimasta identica;
- l'animazione usa gli fps di `RunnerChanged`; `--fps N` resta un override
  locale per il debug;
- registrare o rimuovere un file da Settings chiama
  `RegisterCustomMetric` / `UnregisterCustomMetric`;
- il frontend **non** ha più campionatore, Custom Metrics, CpuMonitor o
  autostart: niente campionamento doppio e nessun adattatore lanciato dal
  tray;
- la finestra Settings › Integrations gestisce ancora le integrazioni con
  `ProviderIntegration`, cioè eseguendo i comandi dell'adattatore. I valori
  arrivano comunque dal backend (vedi §13).

## 10. Installazione e attivazione

| Meccanismo | Cosa installa |
|---|---|
| CMake `install` | `bin/aqtruncat`, `bin/aqtruncatd`, `bin/aqtruncatctl`, `share/dbus-1/services/io.github.psykedady.AQTRunCat.service` (generato da `data/…service.in`) |
| `install.sh` (sistema) | come sopra sotto `/usr/local`; manifest; `Exec=` del file di servizio = `bindir/aqtruncatd` |
| `install.sh --local` | `~/.local/bin` e `~/.local/share/dbus-1/services` |
| PKGBUILD | `depends` + `dbus` |
| Debian | `Depends: … default-dbus-session-bus \| dbus-session-bus` |
| RPM | `%files` con backend, CLI e file di servizio; `Requires: dbus` |

L'installer, inoltre:

- prima di sostituire i file chiude anche `aqtruncatd` di quella
  installazione (percorso esatto, SIGTERM, mai per nome, mai altri utenti);
- dopo l'installazione e la disinstallazione chiede al bus dell'utente
  `ReloadConfig` (busctl o dbus-send; senza bus non succede niente e vale il
  ripiego);
- rispetta `--dry-run` e mantiene tutte le garanzie di M9/F2/F3 (manifest,
  dati utente, autostart, `upgrade` degli adattatori).

Aggiornando da M9/F2/F3, l'autostart esistente (`Exec=…/aqtruncat`) resta
valido e non viene duplicato.

## 11. Client di terze parti

Con gli strumenti standard:

```sh
gdbus call --session -d io.github.psykedady.AQTRunCat -o /io/github/psykedady/AQTRunCat \
      -m io.github.psykedady.AQTRunCat1.GetInfo
busctl --user call io.github.psykedady.AQTRunCat /io/github/psykedady/AQTRunCat \
      io.github.psykedady.AQTRunCat1 SetSetting ss speedMode '"slow-down"'
dbus-monitor --session "type='signal',interface='io.github.psykedady.AQTRunCat1'"
```

Esempio completo senza Qt, in Python con Gio (PyGObject):
[`backend-dbus-api-cli/aqtruncat-client.py`](backend-dbus-api-cli/aqtruncat-client.py).
Il client:

1. legge `GetState` e controlla `api`;
2. si iscrive a `MetricsChanged` e applica i delta seguendo `generation`;
3. richiede lo stato completo quando perde un aggiornamento o quando il
   nome cambia proprietario (backend riavviato).

È stato provato su un bus isolato: con il backend assente esce con 3, poi
segue gli aggiornamenti e recupera dopo un riavvio del backend.

Un client **GTK**, Rust (`zbus`), Waybar (`custom` module con
`aqtruncatctl --json metrics cpu`) o TUI usa lo stesso contratto: il backend
non dipende da Qt Widgets, KDE o GTK, e AQTRunCat non ha nessuna dipendenza
GTK.

## 12. Sicurezza e privacy

- Solo il **bus di sessione** dell'utente. La policy standard di D-Bus
  consente l'accesso solo ai processi dello stesso utente; nessuna rete,
  nessuna porta TCP, nessun privilegio aggiuntivo.
- Nessun metodo esegue comandi forniti dal client. I comandi Custom Metrics
  (M6) restano quelli configurati nei file dell'utente, con la validazione e
  il modello di esecuzione esistenti. Le integrazioni accettano solo un id
  noto e argomenti fissi.
- `SetSetting` accetta solo le chiavi della tabella §7, ciascuna con la sua
  validazione. `customMetricFiles` non è scrivibile direttamente.
- Non vengono esposti credenziali Claude/Codex, token, segreti di sessione,
  contenuti di conversazioni o file privati dei provider. Gli adattatori
  mantengono i confini di privacy di F2/F3 e l'API pubblica solo il loro
  snapshot (lo stesso che la Dashboard mostrava già).
- Percorsi esposti: solo i file di metrica di AQTRunCat (id `custom:` e
  `data.path`), la cartella di scoperta e i percorsi registrati
  dall'utente. Nessuna variabile d'ambiente.

## 13. Prestazioni (Plasma 6 / Wayland, 8 CPU logiche)

Misure su `/proc/<pid>/stat` (utime+stime, inclusi i figli terminati) e
VmRSS, su 30–60 s:

| Scenario | CPU | RSS |
|---|---|---|
| backend da solo, integrazioni abilitate | 0,33 % | 17,6 MB |
| backend da solo, nessuna integrazione | 0,07–0,10 % | 17,3–17,7 MB |
| backend + frontend Qt (regime, 60 s) | backend 0,07 %, frontend 0,47 % | 17,3 + 61,2 MB |
| backend + 2 `aqtruncatctl watch` | backend 0,30 %, ogni watch ≤ 0,03 % | 17,7 + 2 × ~14,6 MB |
| riferimento: AQTRunCat F3 (un processo, integrazioni attive) | 0,47 % | 84,1 MB |

- Una CLI one-shot (`status`, `metrics`, `settings`, `integrations`) impiega
  5–6 ms. La risposta di `metrics` è di 13,6 kB.
- Traffico misurato con `dbus-monitor` in 30 s: 40 `MetricsChanged` e 10
  `RunnerChanged`, 52 messaggi in tutto. Nessun traffico per fotogramma:
  le `NewIcon` (11/s a 11 fps) sono del frontend verso il tray e non
  passano dal backend.
- Processi: un solo `aqtruncatd`, con più frontend, più CLI e dopo i
  riavvii. Gli adattatori Claude e Codex girano solo come figli del backend:
  il frontend ha 0 tick di CPU nei figli.
- Latenza di aggiornamento: un campione arriva ai client nello stesso giro
  dell'event loop (coalescenza a 0 ms) più il tempo del bus, ben sotto il
  periodo di 1 s del campionatore.

Il costo totale è in linea con F3: la memoria complessiva scende un po'
(~78 MB contro 84) e la CPU è equivalente. In più c'è un backend
utilizzabile senza tray.

## 14. Validazione runtime (Plasma 6 / Wayland)

Tutte le prove sono state fatte con XDG isolati (config, dati e cache in una
cartella temporanea poi rimossa) sul bus di sessione reale. Non sono stati
toccati l'AQTRunCat F3 dell'utente, i suoi dati, né la configurazione reale
di Claude o Codex. Esito:

1. il backend gira senza frontend e senza librerie grafiche;
2. la CLI legge le metriche dal vivo;
3. il tray si collega allo stesso backend: un solo `aqtruncatd`;
4. la Dashboard, letta via AT-SPI, mostra gli stessi valori del backend
   (CPU 19,4 % / 18,6 % / 24,1 % come `aqtruncatctl`), con cadenza di 1 s e
   di 2 s secondo l'impostazione;
5. le integrazioni Claude e Codex compaiono nei tooltip della Metrics Bar;
6. due `aqtruncatctl --json watch` ricevono le stesse 12 generazioni con
   payload identici;
7. il tray si chiude e il backend resta disponibile;
8. dopo SIGTERM al backend, il tray lo riavvia con il ripiego e torna a
   mostrare i valori; il nuovo backend eredita l'ambiente del tray;
9. gli adattatori girano solo come figli del backend;
10. il tray anima a 11 `NewIcon`/s con `targetFps` 11;
11. la Metrics Bar abilitata dalla CLI compare nel tray, con voci CPU,
    Claude e Codex;
12. le impostazioni cambiate dalla CLI arrivano al tray (Metrics Bar,
    cadenza della Dashboard);
13. alla chiusura non resta nessun processo e nessun file temporaneo;
14. i dati dell'utente non sono stati modificati.

La validazione ha trovato due difetti, corretti:

- la Dashboard restava su "Waiting…" per CPU, rete e disco, perché gli
  snapshot ricostruiti dal JSON avevano `timeSeconds` = 0 e
  `SnapshotThrottle` lasciava passare solo il primo. Ora il backend
  pubblica `sampleTime` e il client lo usa; il test `backendipc` collega la
  Dashboard attraverso il throttle come fa `trayapp`;
- `SnapshotThrottle` (presente da M5) scartava i campioni arrivati 0,9996 s
  dopo il precedente, per il jitter del timer, e così a "1 s" la Dashboard
  si aggiornava spesso ogni 2 s (lo stesso succedeva in F3). Ora c'è una
  tolleranza di 50 ms, con un test in `desktopsettings`.

## 15. Test automatici

- `tst_apischema` (7 casi, 9 con init/cleanup): metriche pending prima del primo campione,
  valori/stati/dati grezzi, round-trip dello snapshot, classificazione
  builtin/integration/custom e voci multiple, sorgenti rotte o senza
  snapshot, chiavi delle impostazioni, `reload()` con un altro scrittore.
- `tst_backendipc` (eseguito sotto `dbus-run-session`, con XDG isolati; 9
  casi, 11 con init/cleanup): registrazione, versione e istanza unica,
  coerenza dei delta tra più client, classificazione, impostazioni in
  entrambe le direzioni (API → INI e INI → segnale), frontend e Dashboard su
  un backend condiviso con uscita del frontend senza fermare il backend,
  riavvio del backend e recupero, ripiego di avvio, CLI (output leggibile,
  JSON, codici di uscita, impostazioni, custom metrics, integrazioni con un
  adattatore finto), attivazione D-Bus su un `dbus-daemon` privato.
- Test dell'installer: backend, CLI e file di servizio installati, `Exec`
  corretto, backend chiuso dall'aggiornamento, disinstallazione pulita.
- La suite completa (F1/F2/F3 inclusi) passa: vedi `requirements.md`.

Nessun test usa le credenziali reali di Claude o Codex.

## 16. Limitazioni note

- **dbus-broker** (Arch, Fedora) non rilegge da solo le cartelle dei
  servizi. L'installer chiede `ReloadConfig`; se non è possibile, il
  frontend avvia comunque il backend con il ripiego, mentre la CLI vede il
  backend solo dopo il primo avvio del frontend o dopo un nuovo login.
- Il backend resta in esecuzione fino al logout anche senza client: è una
  scelta (avvio rapido per CLI e Plasmoid) ed è leggero (~17 MB, CPU
  trascurabile).
- La finestra Settings › Integrations del frontend Qt esegue ancora gli
  adattatori direttamente (`ProviderIntegration`) per abilitare e
  disabilitare. Il proprietario della configurazione resta l'adattatore; il
  backend osserva i file dell'adattatore e vede la modifica (stato e
  metrica) entro circa 200 ms, poi emette `IntegrationsChanged`.
  L'API e la CLI usano invece i metodi del backend.
- `updatedAt` è `null` per le metriche di sistema.
- I pacchetti nativi (PKGBUILD, deb, rpm) sono stati aggiornati e
  verificati come layout (install con prefix `/usr`), ma non ricostruiti nei
  container delle distribuzioni.
- Un'API v2 incompatibile richiederà una nuova interfaccia; v1 è stabile.
