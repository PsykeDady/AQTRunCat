# AQTRunCat — F2 Claude Code Metric

F2 aggiunge una metrica Claude Code ufficialmente supportata, costruita sopra
le Custom Metrics v2 di F1. La logica specifica di Claude Code vive in un
adattatore separato (`aqtruncat-claude-metric`); il core di AQTRunCat non
conosce alcun formato Claude e continua a eseguire soltanto comandi F1.

```text
Claude Code (stato locale, sola lettura)
    │  ~/.claude/sessions/<pid>.json
    │  ~/.claude/projects/*/<sessionId>.jsonl (solo la coda)
    ▼
aqtruncat-claude-metric snapshot      ← adattatore F2 (QtCore, nessuna rete)
    │  stdout: snapshot JSON M6 · exit 0 / 69 / 1
    ▼
CustomMetricsService (F1)             ← stesso scheduler, timeout, process group
    ├── Dashboard
    ├── Metrics Bar
    └── Settings
```

Il JSON “Claude Code” mostrato nella documentazione M6 resta soltanto un
esempio del formato snapshot prodotto da un programma esterno: non è mai stato
un monitor funzionante e non è il modo in cui funziona F2.

## 1. Indagine

Ambiente esaminato: Claude Code 2.1.301 (installazione nativa in
`~/.local/bin/claude`), Arch Linux, Plasma 6 Wayland. Tutte le letture sono
state fatte senza inviare prompt, senza modificare la configurazione di
Claude Code e senza aprire il contenuto delle conversazioni (sono stati
ispezionati solo nomi di campo e tipi).

| Sorgente | Informazione | Livello | Aggiornamento | Documentata | Effetti collaterali / rete / token | Sessione attiva | Multi-sessione |
|---|---|---|---|---|---|---|---|
| `~/.claude/sessions/<pid>.json` | pid, `procStart`, `sessionId`, cwd, `status` (busy/idle), versione, `updatedAt` | sessione | a ogni cambio di stato del processo | no (interna, presente in 2.1.x) | nessuno / no / no | sì: un file per processo vivo; i file dei processi terminati male restano | sì, uno per processo |
| `~/.claude/projects/<cwd-codificato>/<sessionId>.jsonl` | trascrizione; ogni risposta `assistant` ha `message.usage` (token input/cache/output), `message.model`, `requestedModel`, `isSidechain`, `timestamp`; `system`/`compact_boundary` dopo `/compact` | sessione | a ogni messaggio | formato interno; il significato dei campi `usage` è quello documentato della Messages API | nessuno / no / no | no (resta su disco) | un file per sessione |
| JSON su stdin della status line | `context_window.{context_window_size, used_percentage, current_usage}`, `rate_limits.{five_hour, seven_day}`, `model`, `cost` | sessione + account (`rate_limits`) | a ogni evento della sessione (debounce 300 ms) | **sì** (code.claude.com/docs/en/statusline) | richiede di configurare `statusLine` nelle impostazioni dell'utente | sì, solo dentro la sessione | uno script per sessione |
| `~/.claude/stats-cache.json` | statistiche aggregate giornaliere | account | ricalcolato pigramente (qui: 11 giorni prima) | no | nessuno / no / no | no | — |
| `~/.claude/history.jsonl` | storia dei prompt | account | — | no | contiene testo privato: **scartata** | — | — |
| `~/.claude/.credentials.json` + endpoint di utilizzo | limiti account via rete | account | — | no | credenziali + rete: **scartata** | — | — |
| CLI `claude` | nessun comando di stato in sola lettura; `claude -p` avvia una richiesta | — | — | sì | consuma token: **scartata** | — | — |
| OpenTelemetry | metriche di utilizzo | sessione/account | push | sì | richiede configurazione e collector dell'utente: **scartata** | sì | sì |

Verifiche puntuali:

- `procStart` nel registro coincide con il campo 22 di `/proc/<pid>/stat`
  (starttime in clock tick): permette di distinguere un processo vivo da un
  PID riutilizzato dopo un crash.
- La documentazione della status line definisce
  `used_percentage = (input_tokens + cache_creation_input_tokens + cache_read_input_tokens) / context_window_size`
  (senza `output_tokens`) e
  `context_window_size = 200000 per default, 1000000 per i modelli a contesto esteso`.
  Nella trascrizione il modello richiesto con contesto esteso compare con
  suffisso `[1m]` (es. `claude-opus-5-5[1m]`), quello effettivo senza
  (`claude-opus-5-5`).
- `rate_limits` (finestre 5 ore/7 giorni) esiste **solo** nel JSON passato
  alla status line; non viene salvato su disco.

## 2. Sorgente scelta

Registro sessioni + coda della trascrizione, entrambi locali e in sola
lettura. Motivi:

- nessuna modifica alla configurazione dell'utente (la status line
  richiederebbe di impostare `statusLine` e di sostituire o incatenare uno
  script esistente: è esattamente ciò che F2 non deve fare per ottenere dati);
- nessuna rete, nessun token, nessuna credenziale;
- il registro dice quali sessioni sono **in esecuzione** in modo verificabile
  (`procStart`), invece di indovinare dalla data dei file;
- costo minimo: pochi file piccoli e la coda di un solo file.

La status line è l'unica interfaccia documentata e l'unica con i limiti
account; resta un'opzione futura esplicitamente opt-in (§12).

## 3. Valori esposti e significato esatto

Una sola card “Claude Code”:

| Voce | Esempio | Significato |
|---|---|---|
| Context window | `21.7% of 1M (217.4k tokens)` + barra | **occupazione della finestra di contesto** della sessione selezionata dopo la sua ultima risposta: `input_tokens + cache_creation_input_tokens + cache_read_input_tokens` dell'ultima risposta della conversazione principale, diviso la finestra (200 000, o 1 000 000 se il modello richiesto termina con `[1m]`). Stessa formula di `context_window.used_percentage`. Non conta `output_tokens`. |
| Model | `claude-opus-5-5 · 1M context` | modello che ha prodotto l'ultima risposta; “· 1M context” quando la finestra è quella estesa |
| Session | `AQTCatRun · busy` | nome della directory di lavoro della sessione (non il percorso completo) e stato riportato da Claude Code (`busy`/`idle`) |
| Running sessions | `2 (showing the most recently active)` | numero di sessioni Claude Code in esecuzione (dopo l'eventuale filtro `--cwd`) |

`metricsBarValue` è la percentuale arrotondata (`22%`; `<1%` sotto l'1%).
`Last updated` della card è l'ora dell'ultima risposta di Claude, non l'ora
della lettura.

Cosa **non** è: non è il consumo di token della sessione (somma dei token
spesi), non è l'uso dei limiti account (5 ore/7 giorni), non è l'allowance
residua. Questi concetti non sono mostrati perché la sorgente locale non li
fornisce in modo affidabile (i limiti esistono solo nel JSON della status
line). La percentuale può superare il 100% se Claude Code lo riportasse: non
viene clampata nel testo (la barra della Dashboard sì, come per ogni M6).

Casi particolari:

- nessuna risposta ancora (sessione appena avviata, trascrizione assente):
  `No response yet`, barra `—`; mai 0%;
- dopo `/compact` e prima della risposta successiva: `Unknown until the next
  response after /compact`, barra `—` (Claude Code stesso azzera
  `current_usage` in quel momento);
- `requestedModel` assente (versioni che non lo scrivono): solo i token
  (`152.3k tokens (window size unknown)`), nessuna percentuale inventata;
- contatori cache assenti: valgono 0 come nella Messages API (campi
  opzionali); presenti ma non interi non negativi: formato non supportato.

## 4. Selezione della sessione

1. Si leggono solo i file `<pid>.json` di `<config>/sessions/` (al massimo
   256, ciascuno ≤ 64 KiB). Un file illeggibile o senza `pid`/`sessionId`
   viene ignorato; se lo sono **tutti**, il formato è considerato non
   supportato (Error).
2. Una voce è viva solo se `/proc/<pid>/stat` esiste e il suo starttime è
   uguale a `procStart`. Le voci rimaste da processi terminati o PID
   riutilizzati sono scartate. Senza `procStart` (versioni più vecchie) si
   ripiega su una riga di comando che contiene `claude`.
3. Con `--cwd DIR` restano solo le sessioni avviate in `DIR` o sotto
   (confronto per componenti: `/work/alp` non include `/work/alpha`).
4. Tra le candidate si sceglie la **più recentemente attiva**: trascrizione
   modificata per ultima, poi `updatedAt` del registro. Il registro non dice
   quale terminale ha il focus: la card lo dichiara con “showing the most
   recently active” quando le sessioni sono più di una.

| Situazione | Risultato |
|---|---|
| Claude Code non installato / directory dati assente | Unavailable “Claude Code data directory not found” |
| Installato ma mai usato, nessuna sessione in esecuzione | Unavailable “No running Claude Code session.” |
| Vecchie sessioni su disco, file di registro orfani | ignorati |
| Una sessione | quella |
| Più sessioni | la più recentemente attiva, o quella del progetto con `--cwd` |
| Sessione che smette di scrivere (idle) | resta selezionata finché il processo vive; `Last updated` mostra da quanto |
| Sessione interrotta (crash) | la voce del registro resta ma il processo no: ignorata |
| Ultima riga della trascrizione in scrittura/troncata | saltata, si usa la risposta precedente |

La directory di Claude Code è `$CLAUDE_CONFIG_DIR` se impostata, altrimenti
`~/.claude` (o `--config-dir`).

## 5. Lettura della trascrizione

Non si scorre mai la storia completa. Si leggono gli ultimi 256 KiB; se non
contengono una risposta valida la finestra cresce ×4 fino a 8 MiB, poi
l'adattatore si ferma con Error. Si considerano solo le righe che contengono
letteralmente `"type":"assistant"` o `compact_boundary`; delle risposte si
leggono solo `isSidechain`, `message.model`, `message.usage`,
`requestedModel` e `timestamp`. Le risposte dei sub-agent (`isSidechain`) e
quelle `<synthetic>` (messaggi locali, non risposte dell'API) sono saltate.
La prima riga parziale di ogni finestra viene scartata.

## 6. Architettura e integrazione F1

- `src/providers/claude/claudecode.{h,cpp}` — libreria `aqtruncat_claude`
  (solo QtCore): registro, verifica `/proc`, ricerca trascrizione,
  `readLatestUsage`, formattazione, `collect()`.
- `src/providers/claude/main.cpp` — eseguibile `aqtruncat-claude-metric`
  (`snapshot`, `enable`, `disable`, `--help`, `--version`).
- `src/providers/claude/claudeicon.h` — icona SVG originale e neutra (anello
  indicatore con prompt di terminale; non imita loghi Anthropic).
- `aqtruncat_desktop` e l'eseguibile `aqtruncat` **non** linkano
  `aqtruncat_claude`.

Contratto con F1 (`aqtruncat-claude-metric snapshot`):

| Exit | stdout | Stato F1 |
|---|---|---|
| 0 | uno snapshot JSON M6 | Available |
| 69 (`EX_UNAVAILABLE`) | vuoto; motivo su stderr | Unavailable |
| 1 | vuoto; motivo su stderr | Error |
| timeout / crash | — | Error (gestito da F1) |

### Estensioni generiche di F1

F2 ha richiesto due piccole estensioni di F1, indipendenti da Claude e
retrocompatibili, coperte da test in `tst_commandmetric` e
`tst_commandmetricsservice`:

1. **`source.output`**: `"scalar"` (default, comportamento F1 invariato) o
   `"snapshot"`. Con `"snapshot"` lo stdout (sempre ≤ 16 KiB) è un oggetto
   snapshot M6 validato dal parser M6 esistente: più voci, `normalizedValue`
   e `metricsBarValue` propri. Titolo e simbolo restano quelli della
   configurazione (l'identità della sorgente non cambia tra un run e
   l'altro); `display` è rifiutato in questa modalità. F1 aveva rinviato
   l'output strutturato “fino a un caso d'uso concreto”: F2 è quel caso, e
   riusa il formato snapshot invece di inventarne uno nuovo.
2. **Exit status 69** → Unavailable (messaggio = stderr, max 512 caratteri),
   conservando l'ultimo snapshot valido come per gli altri errori. Ogni altro
   exit non zero resta Error. Un comando esistente che usava proprio 69 ora
   appare Unavailable invece di Error: entrambi non-Available, nessun valore
   inventato.

Nessun nuovo scheduler, watcher o presentazione: l'adattatore è un normale
comando F1 con intervallo 10 s e timeout 5 s, quindi eredita non-overlap,
process group, kill al timeout e chiusura pulita.

## 7. Installazione e attivazione

`aqtruncat-claude-metric` viene installato accanto ad `aqtruncat` da tutti i
flussi: `install.sh` (in `PREFIX/bin`), `install.sh --local`
(`~/.local/bin`, registrato nel manifest e rimosso da `--uninstall`),
`cmake --install`, PKGBUILD e pacchetto Debian (tutto ciò che installa
CMake) e RPM (`%files` aggiornato). Claude Code **non** è una dipendenza:
senza Claude Code AQTRunCat si installa e funziona come prima.

La metrica è **disattivata per default**. L'utente la attiva con:

```bash
aqtruncat-claude-metric enable              # sessione più recentemente attiva
aqtruncat-claude-metric enable --cwd DIR    # solo sessioni in DIR o sotto
aqtruncat-claude-metric disable
```

`enable` scrive in modo atomico il package
`$XDG_DATA_HOME/AQTRunCat/metrics/claude-code/` (`metric.json` +
`claude-code.svg`), con il percorso assoluto dell'eseguibile che lo ha
scritto. AQTRunCat lo scopre subito tramite la discovery dei package di F1.
Non tocca le impostazioni: la metrica compare nella Dashboard ma **non**
viene aggiunta alla Metrics Bar (limite di tre voci invariato); l'utente la
sceglie in Settings se la vuole.

Politica di sovrascrittura: `enable`/`disable` agiscono solo su un package
che riconoscono come proprio (`id` = `claude-code` ed eseguibile chiamato
`aqtruncat-claude-metric`). Un `claude-code/metric.json` scritto dall'utente
non viene mai sovrascritto né rimosso; `enable --force` lo sostituisce solo
su richiesta esplicita. Le altre Custom Metrics e le impostazioni non vengono
toccate. Se AQTRunCat viene reinstallato in un altro prefisso, la card
diventa Unavailable (“Executable is not available”): basta ripetere
`enable`.

## 8. Dashboard, Metrics Bar, Settings

- Dashboard: card “Claude Code” con icona, le quattro voci di §3 e la barra
  per il contesto. Durante il refresh resta visibile l'ultimo stato
  concluso (correzione anti-flicker di F1), “Waiting…” solo alla prima
  esecuzione.
- Metrics Bar: valore compatto `22%` sotto l'icona; il tooltip riporta
  `Claude Code: 22%` e l'ora dell'ultima risposta. Unavailable/Error usano
  la resa standard della barra.
- Settings: la sorgente appare come “Command” nell'elenco Custom Metrics,
  come ogni comando F1.

## 9. Stati e guasti

| Caso | Stato | Messaggio |
|---|---|---|
| Directory dati assente | Unavailable | `Claude Code data directory not found: …` |
| Nessuna sessione in esecuzione (anche con `--cwd`) | Unavailable | `No running Claude Code session[ in DIR].` |
| Registro tutto illeggibile/senza campi | Error | `unsupported Claude Code session registry format` |
| Trascrizione illeggibile (permessi, I/O) | Error | `cannot read the session transcript: …` |
| `usage` senza contatori o con valori non interi/negativi | Error | `unsupported transcript format: …` |
| Nessuna risposta negli ultimi 8 MiB | Error | `no response usage within the last … bytes` |
| Eseguibile mancante/non eseguibile | Unavailable | messaggio F1 |
| Avvio fallito, crash, timeout | Error | messaggio F1 |

I messaggi non contengono mai testo della trascrizione. Nessun dato mancante
diventa 0. Un guasto della metrica Claude non tocca runner, Dashboard, altre
metriche o la chiusura (garanzie F1, verificate anche a runtime).

## 10. Privacy e sicurezza

- Solo lettura: nessuna scrittura in `~/.claude`, nessuna modifica di
  impostazioni o sessioni, nessun prompt, nessun token consumato, nessuna
  rete.
- Lette solo le voci necessarie: dal registro pid/procStart/sessionId/cwd/
  status/updatedAt; dalla trascrizione solo le righe candidate, e di queste
  solo i campi di §5. Il contenuto dei messaggi viene analizzato come JSON
  in memoria ma mai copiato nell'output o nei messaggi d'errore.
- Nell'output: nome della directory di progetto (non il percorso), stato,
  modello, conteggi di token. Mai prompt, risposte, titoli AI della
  sessione, credenziali o percorsi completi.
- Nessun `sudo`/`pkexec`, nessuna shell implicita: F1 esegue l'adattatore
  con `QProcess` e un array di argomenti.
- I test usano solo fixture sintetiche; contengono marcatori fittizi
  (`PRIVATE-PROMPT-…`, `sk-ant-FAKE-…`) proprio per verificare che non
  finiscano nell'output.

## 11. Compatibilità tra versioni di Claude Code

Registro e trascrizione sono formati **interni** di Claude Code (verificati
su 2.1.301). L'adattatore è difensivo: campi mancanti o di tipo inatteso
producono Error con un messaggio esplicito, mai valori inventati; `procStart`
e `requestedModel` assenti hanno un ripiego documentato (riga di comando;
solo token). Se una versione futura cambia questi formati, la card mostrerà
Error finché l'adattatore non viene aggiornato. La formula del contesto e la
dimensione della finestra seguono invece la documentazione ufficiale della
status line.

## 12. Limiti noti e lavoro rinviato

- I limiti account (5 ore/7 giorni) e l'allowance residua non sono mostrati:
  esistono solo nel JSON della status line. Un ponte opt-in (uno script
  status line fornito da AQTRunCat che l'utente configura esplicitamente e
  che salva solo i numeri in `$XDG_RUNTIME_DIR`) è possibile ma rinviato:
  richiede di modificare la configurazione di Claude Code dell'utente e di
  gestire una status line già esistente.
- Con più sessioni la scelta è “la più recentemente attiva”, non “quella in
  primo piano”; `--cwd` permette di fissare un progetto.
- La finestra è dedotta da `requestedModel`: un modello con finestra diversa
  da 200k/1M senza suffisso `[1m]` darebbe una percentuale sbagliata. Oggi la
  documentazione prevede solo questi due valori.
- Il valore si aggiorna al massimo ogni 10 s e solo quando Claude risponde.
- Formati interni: vedi §11.

## 13. Test e validazione

Automatici (deterministici, senza `~/.claude` reale né rete):

- `tst_claudecodemetric` (28 casi, incluse righe dati): installazione
  assente, mai usato, voci orfane e PID riutilizzati, registro non
  supportato, sessione valida, 1M, finestra sconosciuta, limiti percentuali
  (0, <0,1, 0,5, 1, 100, 105%), sidechain/synthetic/riga troncata,
  `/compact`, nessuna risposta, campi mancanti/stringa/negativi, contatori
  cache assenti, finestra di coda crescente e limitata, multi-sessione e
  `--cwd`, `comm` con spazi e parentesi, ripiego senza `procStart`,
  `/proc/self` reale, trascrizione non leggibile, formattazione,
  `CLAUDE_CONFIG_DIR`, percorsi con spazi, assenza di dati privati
  nell'output.
- `tst_claudecodeintegration` (6): l'eseguibile reale eseguito da
  `CustomMetricsService` → Dashboard (titolo, icona, valori, barra) e
  Metrics Bar (non selezionata per default; selezionata mostra `12%`),
  refresh che riflette una nuova risposta, Unavailable senza Claude Code,
  nessun processo figlio residuo, `enable`/`disable` con politica di
  sovrascrittura, errori della riga di comando.
- Estensioni F1: `tst_commandmetric` (+2) e `tst_commandmetricsservice`
  (+2).
- Suite completa: 36/36 test CTest superati.

Validazione pratica (2026-10-16, Plasma 6 Wayland, Claude Code 2.1.301 reale
in sola lettura, istanza AQTRunCat isolata con XDG separati accanto a quella
in uso, `run --fps 10`, Dashboard chiusa durante le misure, 30 s per
scenario):

| Scenario | CPU app | CPU adattatore (figli) | Totale | RSS |
|---|---:|---:|---:|---:|
| metrica disattivata (baseline) | 0,40% | 0,00% | 0,40% | 68,7 MiB |
| metrica attiva, 10 s, nella barra | 0,40% | 0,03% | 0,43% | 68,6 MiB |
| stress: intervallo 1 s | 0,57% | 0,53% | 1,10% | 68,7 MiB |

(% di un core.) Un'esecuzione dell'adattatore richiede circa 7 ms.

Verificato: lettura dei dati reali (due sessioni in esecuzione, finestra
1M), discovery del package scritto da `enable`, esecuzione tramite F1,
refresh (valore passato da 27% a 28% tra due risposte), card nella
Dashboard con icona, contesto, barra e modello (screenshot della sola
finestra), valore compatto e icona nella Metrics Bar (pixmap SNI letto via
D-Bus: icona sopra `27%`, tooltip `Claude Code: 27%`), Unavailable con
directory assente ed Error con registro non supportato visibili nel tooltip
e ripristino immediato, runner invariato (100 `NewIcon` in 10 s con e senza
metrica), `disable` che rimuove la card, SIGTERM con uscita pulita e nessun
processo `aqtruncat-claude-metric` residuo; l'istanza AQTRunCat dell'utente
non è stata toccata.

Non verificato a runtime: un timeout reale dell'adattatore (non c'è modo di
farlo bloccare senza strumenti invasivi; il percorso di timeout è quello
generico di F1, già validato in F1 e coperto dai test); le ultime righe della
card (Session, Running sessions, footer) erano fuori dallo schermo nello
screenshot e sono verificate dai test automatici.
