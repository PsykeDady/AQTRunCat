# AQTRunCat — M2.1 Runner Lifecycle + CLI Foundation

> Obiettivi: import sicuro dei runner ZIP, lifecycle completo dei runner
> utente (install, select/run, remove), CLI ufficiale a sottocomandi, stessa
> logica applicativa per CLI e futura GUI.
> Data: 2026-10-02. Ambiente: lo stesso di M1/M2 (Arch, Plasma 6.7.5
> Wayland, Qt 6.11.2, libarchive 3.8.9). **Con M2.1 la milestone M2 è
> completa.**

## 1. Architettura

```text
                 ┌── CLI (src/cli): parse → comando → presentazione
RunnerService ───┤
 (src/runner)    └── futura Settings UI (stessa API)
       │
       ├─ discoverRunners / chooseRunner   catalogo, precedenza, selezione
       ├─ loadRunner                        validazione runner (M2)
       ├─ extractRunnerArchive              ZIP → directory temporanea (libarchive)
       ├─ installRunner                     staging → rename atomico in XDG
       └─ removeUserRunner                  solo runner utente
```

| Componente | File | Ruolo |
|---|---|---|
| `RunnerService` | `src/runner/runnerservice.*` | facciata usata da tutti i frontend; `importArchive` orchestra estrazione → validazione → installazione e misura i tempi |
| estrazione | `src/runner/runnerarchive.*` | lettura ZIP, controlli di sicurezza, limiti, individuazione del runner candidato |
| installazione | `src/runner/runnerinstaller.*` | riceve un runner già validato, gestisce collisioni e `--force` |
| rimozione | `src/runner/runnerremover.*` | rimuove solo override utente, protegge i bundled |
| errori | `src/runner/runnererror.h` | `NotFound`, `Invalid`, `Conflict`, `Filesystem`, poi mappati dai frontend |
| grammatica CLI | `src/cli/commandline.*` | parser puro (`QStringList` → `Invocation`), help, exit code |
| comandi `runner` | `src/cli/runnercommands.*` | `list`/`import`/`remove`: chiamano il servizio e stampano |
| `run` | `src/app/trayapp.*` | l'app tray, spostata invariata da `main.cpp` |
| `main.cpp` | `src/app/main.cpp` | solo dispatch |

Nessuna logica vive nel parsing. Un pulsante GUI "Import runner" chiamerà
`RunnerService::importArchive`, come fa `aqtruncat runner import`;
"Remove runner" chiamerà `RunnerService::remove`.

Pipeline CPU invariata: `cpumonitor.*`, `core/cpustat.*`, `core/smoothing.h`
e `core/speedcurve.*` non sono stati toccati (timestamp di M1.1). Anche il
blocco tray/animazione è identico; è solo stato spostato in
`runTrayApplication`.

## 2. Dipendenza libarchive

- `find_package(LibArchive 3.0)` (modulo standard di CMake, target
  `LibArchive::LibArchive`), collegata in privato alla sola libreria
  `aqtruncat_runner`.
- Se manca, la configurazione si ferma con un messaggio che spiega a cosa
  serve e come installarla (Arch `libarchive`, Debian/Ubuntu `libarchive-dev`,
  Fedora `libarchive-devel`), oppure come indicarne il percorso.
- API usate: lettura ZIP tramite central directory
  (`archive_read_support_format_zip_seekable`), header, `pathname_utf8`,
  `filetype`/`hardlink`, `read_data`. L'estrazione su disco di libarchive
  (`archive_write_disk`) **non** viene usata: AQTRunCat scrive da sé ogni
  file, dopo averne validato il path.
- Licenza libarchive: BSD-2-Clause. È collegata dinamicamente e non è inclusa
  nel progetto.

## 3. Modello di sicurezza dell'archivio

L'archivio è input non fidato. Ogni entry viene controllata prima di scrivere
qualsiasi cosa; al primo problema l'intero archivio viene rifiutato.

| Controllo | Regola |
|---|---|
| formato | solo ZIP, letto dalla central directory |
| path | rifiutati: path assoluti (`/etc/foo`, `//x`), componenti `..` (`../x`, `a/../../b`), `.` o vuote (`./a`, `a//b`), backslash, lettere di unità (`C:`), caratteri di controllo, nomi oltre 512 caratteri o vuoti |
| tipo di entry | solo file regolari e directory. **Rifiutati** symlink, hardlink, device a caratteri e a blocchi, socket e ogni altro tipo |
| scrittura | solo `destinazione + path validato`, in una directory temporanea nuova (`QTemporaryDir`); file aperti con `NewOnly` (le entry duplicate sono errore); nessun link può esistere nella destinazione |
| junk | `__MACOSX/…` e componenti nascoste (`.DS_Store`, `._*`) vengono validati ma saltati |
| layout | frame al primo livello (id = nome dell'archivio) oppure una sola cartella al primo livello (id = nome della cartella senza `-frames`). Più cartelle con frame → rifiutato come ambiguo (lo decide il loader). |

Nota sui tipi speciali, verificata sperimentalmente con archivi costruiti a
mano (modo Unix modificato nella central directory): libarchive 3.8.9
restituisce **tali e quali** device a caratteri, a blocchi, socket e symlink
(che AQTRunCat rifiuta), mentre converte un modo FIFO in file regolare. Per
questo un FIFO non può mai essere creato e il test lo verifica.

Limiti (`ArchiveLimits`, controllati sui byte **realmente decompressi** e
non solo su quelli dichiarati):

| Limite | Valore | Riferimento reale (43 runner Gallery) |
|---|---|---|
| dimensione dello `.zip` | 64 MiB | gli archivi Gallery sono < 2 MiB (limite del loro CI) |
| entry | 1024 | massimo 25 (24 frame + 1 cartella) |
| singolo file estratto | 8 MiB | i frame sono di pochi KiB; un PNG 1024×1024 resta sotto il limite |
| totale estratto | 64 MiB | runner reali: pochi KiB–centinaia di KiB |

I limiti sono larghi rispetto ai runner reali e al limite del loader (256
frame, lato massimo 1024 px), ma impediscono di estrarre quantità illimitate
di dati: un file da 256 KiB di zeri compresso in meno di 4 KiB viene fermato
dal limite per file (test).

## 4. Estrazione → validazione → installazione

1. **Estrazione** in una `QTemporaryDir` privata: controlli del §3, poi
   individuazione della directory del runner e dell'id proposto.
2. **Validazione**:
   - id valido: 1–100 caratteri tra lettere, cifre, `.`, `_` e `-`, che inizia
     con lettera o cifra; nomi come `My Runner (1).zip` richiedono `--id`;
   - `loadRunner` (regole M2: PNG decodificabili, stessa dimensione, limiti,
     metadata opzionali).
3. **Installazione** (`installRunner`):
   - copia in `…/runners/.aqtruncat-install-XXXXXX/`: nascosta (la discovery
     la ignora) e sullo stesso filesystem;
   - seconda validazione della copia;
   - `rename` atomico in `…/runners/<id>`.

A ogni errore non resta nulla: la directory temporanea e lo staging si
cancellano da soli, e nessun runner parziale diventa visibile. Verificato nei
test (directory utente e un `TMPDIR` privato controllati dopo ogni caso) e
nell'import reale dei 43 archivi (0 residui).

## 5. Installazione XDG, collisioni, `--force`

- Destinazione: `$XDG_DATA_HOME/AQTRunCat/runners/<id>` (default
  `~/.local/share/AQTRunCat/runners/<id>`), la stessa directory di M2.
- **Collisione con un bundled**: consentita. Il runner utente fa da override
  (user > bundled) e la CLI lo dice ("takes precedence over the bundled
  runner"). Il bundled non viene modificato.
- **Collisione con un runner utente**: errore `Conflict` ("already
  installed", exit 5) con il suggerimento `--force` / `--id`. Il caso vale
  anche quando il runner esistente è stato copiato a mano come
  `<id>-frames/`.
- **`--force`**: sostituzione sicura.
  1. Nuovo runner pronto e validato nello staging.
  2. Il vecchio viene spostato in una directory nascosta di backup.
  3. Rename del nuovo al suo posto.
  4. Se il rename fallisce, il vecchio viene ripristinato. Se fallisse anche
     il ripristino, il backup viene conservato e mai cancellato.
  5. Solo a operazione riuscita il backup viene eliminato.

  Il test simula un fallimento durante la copia (file illeggibile): il vecchio
  runner resta intatto e non rimangono residui.
- Id fornito da più directory utente (es. `deer/` e `deer-frames/`): conflitto,
  nessuna scelta automatica.

## 6. Rimozione

`aqtruncat runner remove ID` → `RunnerService::remove`:

| Situazione | Esito |
|---|---|
| runner utente | rimosso: prima reso invisibile con rename in una directory nascosta, poi cancellato |
| runner utente che fa da override a un bundled | rimosso solo l'override; il bundled torna visibile ("is used again") |
| solo bundled | `Conflict`, exit 5: "is a bundled runner and cannot be removed" |
| inesistente | `NotFound`, exit 3 |
| id fornito da più directory utente | `Conflict`, exit 5, nulla viene rimosso |

Un'app in esecuzione con quel runner continua a funzionare: i frame sono già
in memoria.

## 7. CLI

Grammatica (dettagli in `requirements.md` §13a):

```text
aqtruncat                                   = aqtruncat run
aqtruncat run [--runner ID] [--fps N] [--verbose]
aqtruncat runner list
aqtruncat runner import FILE.zip [--id ID] [--force]
aqtruncat runner remove ID
aqtruncat [<command> [subcommand]] --help | -h,   aqtruncat --version | -v
```

- Opzioni nella forma `--name value` o `--name=value`; `--` chiude le opzioni
  (file che iniziano con `-`). Opzioni ripetute, sconosciute o senza valore
  sono errori d'uso.
- **Alias legacy** (M1/M2), un livello solo e senza deprecation:
  `aqtruncat --fps N`, `aqtruncat --runner ID`, `aqtruncat --verbose` sono
  equivalenti a `aqtruncat run …`; `aqtruncat --list-runners` è equivalente a
  `aqtruncat runner list`.
- **Help**: per ogni livello (`--help`, `run --help`, `runner --help`,
  `runner list|import|remove --help`), al massimo una ventina di righe, e
  spiega che cosa fa il comando. L'help principale riporta anche gli exit
  code.
- **Exit code**:

  | Codice | Significato |
  |---|---|
  | 0 | successo |
  | 1 | errore filesystem/installazione o fallimento dell'app |
  | 2 | riga di comando non valida |
  | 3 | runner o file non trovato |
  | 4 | runner, archivio o id non validi |
  | 5 | conflitto: già installato, bundled protetto, id ambiguo |

  Rispetto a M2: un `--runner` sconosciuto passa da 1 a **3**, un runner non
  valido da 1 a **4**.
- Errori sempre su stderr (mantiene il fix di M2: il logging Qt può finire su
  journald). I risultati vanno su stdout.
- Opzioni Qt generiche (`-platform`, `-style`) non sono accettate dalla
  riga di comando: si usano le variabili d'ambiente (`QT_QPA_PLATFORM`…).
- Estendibilità: un nuovo gruppo (`metrics`, `config`) è una nuova parola in
  `parseCommandLine()` con la sua tabella di opzioni e il suo help, senza
  toccare il resto.

## 8. Test automatici

11 suite, **225 casi**, tutti verdi (il conteggio Qt Test include le righe
dei test data-driven e init/cleanup):

| Suite | Casi | Nuova in |
|---|---:|---|
| cpustat | 30 | M1.1 |
| speedcurve | 21 | M1.1 |
| smoothing | 9 | M1.1 |
| frameplayback | 7 | M2 |
| framenames | 10 | M2 |
| runnerloader | 24 | M2 |
| runnercatalog | 14 | M2 (il test del formato lista è passato a `cli`) |
| runneranimator | 8 | M2 |
| **runnerarchive** | 48 | M2.1 |
| **runnerlifecycle** | 16 | M2.1 |
| **cli** | 38 | M2.1 |

Copertura M2.1:

- **Archivio valido**:
  - Gallery con DEFLATE;
  - entry STORED senza entry di directory;
  - frame al primo livello;
  - junk saltato.
- **Archivio malformato**:
  - file inesistente;
  - non ZIP;
  - dati corrotti;
  - troncato;
  - vuoto;
  - solo directory;
  - entry duplicate;
  - estratto ma non valido (frame di dimensioni diverse, nessun PNG, più runner).
- **Sicurezza**:
  - `../`, traversal annidato e profondo, path assoluto. Per ogni archivio
    ostile il test rilegge i nomi salvati, per provare che il nome malevolo è
    davvero nel file;
  - symlink e symlink a directory seguito da un file "attraverso" il link;
  - hardlink (a livello di regola; lo ZIP non può trasportarlo);
  - device a caratteri e a blocchi, socket;
  - FIFO neutralizzato;
  - limiti di entry, file singolo (bomba di zeri), totale, dimensione
    dell'archivio.

  Ogni test verifica che nulla venga scritto fuori dalla propria sandbox.
- **Lifecycle**:
  - import → catalogo → selezione → remove → scomparso;
  - bundled `cat` → user `cat` vince → remove → torna il bundled (file
    bundled intatti);
  - bundled protetto, id inesistente, id ambiguo;
  - collisione utente/utente;
  - `--force` riuscito, e fallito con il vecchio runner intatto;
  - archivio ostile o non valido → nulla installato;
  - `--id` e id non validi;
  - nessun residuo in directory utente e `TMPDIR`.
- **CLI**:
  - tutte le forme richieste e gli alias;
  - help per ogni comando;
  - 17 errori d'uso (comando sconosciuto, argomento mancante, fps non valido,
    opzione sconosciuta o ripetuta…);
  - mappatura degli exit code;
  - runner sconosciuto per `run` → 3;
  - `list`/`import`/`remove` eseguiti con output su stdout ed errori su
    stderr;
  - `--list-runners` produce lo stesso output di `runner list`.

## 9. Compatibilità con gli ZIP della Runner Gallery

Tutti i **43 archivi** reali della Runner Gallery (`b979658`) importati con il
binario reale (`aqtruncat runner import`) in un `XDG_DATA_HOME` temporaneo:

- 43 importati, 0 falliti, 0 warning;
- 43 elencati da `runner list`, 0 invalidi;
- 0 file temporanei residui (`TMPDIR` privato) e 0 directory nascoste
  residue;
- `tools/cycle-runners.sh` sui 43 + 2 bundled: 45 istanze avviate, ciascuna
  con il proprio runner caricato (log nel journal).

Gli archivi Gallery contengono solo `<name>-frames/` con i PNG e nessun
`metadata.json`, quindi nome e modalità vengono derivati (§3 di M2: tipo
dedotto corretto su 43/43).

## 10. Test reali su Plasma 6 + Wayland

Verificati via D-Bus (`IconPixmap` e `ToolTip` ricevuti da Plasma, segnali
`NewIcon` al secondo):

| Comando | Risultato |
|---|---|
| `aqtruncat` | cat 56×36, "AQTRunCat — Cat", ~11 fps (guidato dalla CPU, idle) |
| `aqtruncat run` | identico |
| `aqtruncat run --runner coffee` | 38×36, "AQTRunCat — Coffee", ~12 fps |
| `aqtruncat run --runner coffee --fps 20` | 20,5 NewIcon/s |
| `aqtruncat --runner coffee --fps 40` (legacy) | 40,5 NewIcon/s |
| `aqtruncat run --runner dragon` | messaggio su stderr, exit 3 |

Lifecycle reale nella **directory XDG di default**
(`~/.local/share/AQTRunCat`, assente prima del test e ripristinata dopo):

1. `runner import escapement-frames.zip`: installato in 24 ms; `runner list`
   lo mostra come `user`, 24 frame, 70x36.
2. `run --runner escapement`: icona 70×36 nella tray, animazione guidata
   dalla CPU. L'ordine dei 24 frame è stato verificato end-to-end
   (campionamento di `IconPixmap`, 0 passi errati).
3. `runner remove escapement`: rimosso; `run --runner escapement` → exit 3.
4. `runner remove cat` → exit 5 (bundled protetto).
5. `runner import … --id cat`: override, e `aqtruncat` mostra il 70×36.
   Reimport senza `--force` → exit 5 con il suggerimento. `runner remove cat`
   → "The bundled runner "cat" is used again", e `aqtruncat` torna a mostrare
   il 56×36.

Strumenti di sviluppo, aggiornati alla grammatica ufficiale (`run …`,
`runner list`) e verificati:

- `tools/test-cpu-fps-curve.sh`: 21 istanze, sequenza `10 10 11 … 40 … 10 10`;
- `tools/cycle-runners.sh`: 45/45;
- `tools/test-cpu-runtime.sh`: due esecuzioni.

| Fase | Esecuzione 1 | Esecuzione 2 |
|---|---|---|
| idle | 12–17 fps, 3 cambi | 11 fps, 0 cambi |
| metà carico | 27–30 fps | 28–29 fps |
| pieno | 39–40 fps | 37–40 fps |
| recovery | 12–23 fps | 11–21 fps |

La variabilità di idle e recovery viene da carico esterno, non da
AQTRunCat. Misurata senza AQTRunCat e senza carico, la CPU di sistema in 60 s
aveva comunque picchi fino al 55% (σ 9,1%). `clamd` era appena ripartito
(crash-loop ancora attivo) e c'erano altre app attive. Il codice CPU è
invariato.

## 11. Performance

| Misura | Valore |
|---|---|
| import di un archivio Gallery (43 archivi, `XDG_DATA_HOME` in `/tmp`) | totale mediana 4 ms, max 12 ms; estrazione ≤ 1 ms, validazione mediana 3 ms (max 6), installazione mediana 1 ms (max 5) |
| import reale in `~/.local/share` (escapement, 24 frame) | 24 ms: estrazione 1, validazione 5, installazione 18 (prima creazione delle directory, filesystem reale) |
| runtime | invariato rispetto a M2: stesso percorso `run`, stessi frame preparati; ~11 fps e 0 cambi in idle stabile |

## 12. Limitazioni note

1. Le opzioni generiche di Qt (`-platform`, `-style`, `-display`…) non sono
   accettate dalla CLI: si usano le variabili d'ambiente.
2. Gli archivi della Gallery non contengono `metadata.json`: dopo l'import
   nome e modalità sono derivati (es. `--id cat` su un runner della Gallery
   produce il nome visualizzato "Cat").
3. Non c'è una lista JSON né un output leggibile da macchina (non richiesti
   in M2.1). `tools/cycle-runners.sh` interpreta la tabella umana.
4. La sostituzione con `--force` è atomica per ciascun rename, ma l'intera
   operazione richiede due rename. Fra i due (finestra di microsecondi) l'id
   non è visibile: un'app avviata esattamente in quell'istante userebbe il
   bundled o fallirebbe con "not found".
5. Restano valide le limitazioni M1/M2 della tray per i runner molto larghi.

## 13. Decisioni aperte

Nessuna bloccante per M2. Da considerare più avanti:

- formato di output leggibile da macchina (`runner list --json`) se servirà
  a script o integrazioni (M8);
- persistenza della selezione del runner e anteprima utente: assegnate a M5
  insieme alla Settings UI.
