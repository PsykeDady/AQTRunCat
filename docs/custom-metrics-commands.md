# AQTRunCat — F1 Custom Metrics v2

F1 estende, senza sostituirla, l'architettura Custom Metrics realizzata in
M6. M6 resta storicamente il supporto passivo agli snapshot JSON prodotti da
software esterno; F1 aggiunge una seconda sorgente che AQTRunCat aggiorna
eseguendo periodicamente un programma configurato.

## 1. Modello comune

Le sorgenti sono ora esplicitamente distinte:

```text
Custom Metric
├── Snapshot: JSON RunCat Neo aggiornato da un producer esterno
└── Command:  configurazione JSON + processo avviato da AQTRunCat
```

Entrambe confluiscono nello stesso `custommetrics::Snapshot` e nello stesso
`custommetrics::Source` con stati Pending, Available, Unavailable ed Error.
Dashboard e Metrics Bar non possiedono sampler o percorsi provider-specifici.
Una sorgente guasta non influenza le altre e un errore non diventa mai zero.

Tutto il contratto M6 rimane valido: registrazione esplicita persistente,
discovery degli snapshot `*.json`, file watching, atomic replace, retry e
ultimo valore valido. Il JSON Claude mostrato nella documentazione M6 è solo
un esempio di snapshot esterno: F1 non implementa monitor Claude o Codex.

## 2. Configurazione Command

Schema implementato:

```json
{
  "id": "example-temperature",
  "title": "Temperature",
  "symbol": {
    "type": "file",
    "value": "./icons/temperature.svg"
  },
  "source": {
    "type": "command",
    "executable": "./read-temperature",
    "arguments": ["--sensor", "cpu"],
    "intervalSeconds": 5,
    "timeoutSeconds": 3
  },
  "display": {
    "prefix": "CPU ",
    "suffix": " °C",
    "normalizedRange": {
      "minimum": 0,
      "maximum": 100
    }
  }
}
```

Campi:

- `id`: obbligatorio, stabile, 1–100 caratteri; primo carattere
  alfanumerico, poi alfanumerici, `.`, `_` o `-`;
- `title`: obbligatorio e usato sia per la card sia per la singola voce;
- `symbol`: opzionale; può restare una stringa legacy oppure essere
  `{ "type": "theme"|"file", "value": "..." }`;
- `source.type`: deve essere esattamente `command`; è il confine esplicito che
  impedisce di interpretare uno snapshot M6 come programma;
- `source.executable`: obbligatorio, percorso assoluto o relativo;
- `source.arguments`: opzionale, array di stringhe, default vuoto, massimo
  256 elementi;
- `source.intervalSeconds`: opzionale, default e minimo 1 secondo, massimo
  86400 secondi;
- `source.timeoutSeconds`: opzionale, default 10 secondi, intervallo 1–300;
- `display.prefix` e `display.suffix`: opzionali, concatenati al valore;
- `display.normalizedRange`: opzionale, con `minimum < maximum`; se presente
  l'output deve essere numerico e viene normalizzato con
  `(value-minimum)/(maximum-minimum)` per la progress bar. Il valore non viene
  clampato, coerentemente con il modello snapshot.

I campi stringa noti mantengono il limite M6 di 4096 caratteri; il file JSON
mantiene il limite di 1 MiB. Una configurazione riconosciuta come Command ma
non valida entra in Error e non viene mai eseguita né reinterpretata come
snapshot.

## 3. Output e presentazione

Il processo deve scrivere su stdout un solo valore scalare UTF-8. Spazi e
newline esterni vengono rimossi; dopo il trim il valore deve essere non vuoto
e non contenere altri ritorni a capo. Esempio valido:

```text
23.4
```

Il valore finale è `prefix + scalar + suffix` e alimenta sia
`formattedValue` sia `metricsBarValue`. La forma strutturata non è stata
aggiunta: per F1 un solo valore copre il caso comune senza duplicare il
contratto snapshot M6, che resta disponibile per producer con più valori.
Output vuoto, multilinea, non UTF-8, troppo lungo o non numerico quando è
richiesto `normalizedRange` produce Error conservando l'ultimo snapshot
valido.

Stdout e stderr sono raccolti separatamente e limitati a 16 KiB ciascuno.
Stderr viene usato, fino a 512 caratteri, come diagnostica per exit status non
zero; non diventa un valore della metrica.

## 4. Percorsi, package e icone

Un eseguibile relativo viene risolto rispetto alla directory del file di
configurazione. Il processo usa la stessa directory come working directory.
Anche le icone `file` relative seguono questa regola, mai il working directory
dal quale è stato avviato AQTRunCat.

La discovery M6 dei file standalone resta non ricorsiva. F1 aggiunge soltanto
un livello package: per ogni directory immediatamente contenuta in
`$XDG_DATA_HOME/AQTRunCat/metrics/` viene cercato `metric.json`. È quindi
supportata questa struttura autosufficiente:

```text
~/.local/share/AQTRunCat/metrics/my-metric/
├── metric.json
├── metric-executable
└── icons/
    └── metric.svg
```

I simboli `theme` usano `QIcon::fromTheme`; i simboli `file` usano i formati
leggibili dai plugin immagine Qt installati (incluso SVG sull'ambiente
supportato). File assente, illeggibile, non supportato o invalido produce
l'icona informativa di fallback, senza rendere indisponibile la metrica. La
vecchia stringa `symbol` M6 continua a essere renderizzata come prima.

## 5. Scheduler, stati e ciclo di vita

`CustomMetricsService` possiede un solo timer monotonic shared per tutte le
sorgenti Command. Ogni job conserva scadenza e processo propri, ma non ha un
timer periodico dedicato. Il primo run avviene subito; il successivo è
programmato `intervalSeconds` dopo il completamento. Non esistono catch-up o
busy polling e lo stesso job non può sovrapporsi a sé stesso. Questo evita
process storm quando un'esecuzione dura più dell'intervallo.

Transizioni:

- configurazione valida/primo processo in corso: Pending;
- refresh in corso dopo un successo: Pending con ultimo snapshot visibile;
- successo e output valido: Available;
- file eseguibile mancante o non eseguibile: Unavailable;
- avvio fallito, crash, exit non zero, timeout o output invalido: Error.

Presentazione del refresh: lo stato interno resta Pending per tutta la
durata del run, ma se esiste già un valore valido la Dashboard continua a
mostrare l'ultimo stato concluso (Available con "Last updated", oppure
Error/Unavailable con il relativo messaggio) senza passare a "Waiting…" a
ogni refresh. "Waiting…" compare solo alla prima esecuzione, quando non c'è
ancora un valore valido. Un refresh riuscito sostituisce il valore; uno
fallito espone Error/Unavailable conservando l'ultimo valore valido. È una
regola di sola presentazione: scheduling ed esecuzione non cambiano.

Ogni comando viene avviato come leader di un proprio process group
(`setpgid(0, 0)` nel figlio, prima di `exec`). Timeout e output oltre il limite
inviano `SIGKILL` all'intero gruppo, quindi anche agli helper lanciati da uno
script (per esempio `sleep 30 &`). Dopo il kill per timeout lo scheduler
ricontrolla il processo non ancora raccolto ogni secondo, senza girare a vuoto
su una scadenza già passata. Su arresto del servizio o dell'app tutti i gruppi
ricevono prima `SIGTERM`; l'attesa complessiva è limitata a 250 ms, poi i
gruppi ancora vivi ricevono `SIGKILL` con un'ulteriore attesa limitata. Un
`SIGKILL` finale al gruppo raggiunge anche i membri che hanno ignorato
`SIGTERM` sopravvivendo al leader. I job vengono distrutti e non restano
processi figli. Dopo un run completato con successo AQTRunCat non uccide
processi che il comando ha deliberatamente lasciato in background. Le sorgenti
vengono riprovate al proprio intervallo, senza backoff aggiuntivo.

Le sorgenti Command pubblicano un aggiornamento all'avvio e alla fine di ogni
run. La Dashboard nascosta memorizza soltanto l'ultimo elenco e ricostruisce
le card quando viene mostrata; Settings conserva la riga selezionata
nell'elenco Custom Metrics durante questi aggiornamenti.

## 6. Confine di sicurezza

AQTRunCat chiama `QProcess::start(executable, arguments)` direttamente:

- nessuna shell implicita, espansione, pipe, redirect o interpretazione di
  metacaratteri;
- nessun `sudo`, `pkexec` o altro tentativo di elevazione;
- il programma gira con gli stessi privilegi e lo stesso ambiente utente di
  AQTRunCat;
- soltanto una configurazione esplicita e valida con `source.type=command`
  può avviare un processo;
- chi necessita sintassi shell deve indicare esplicitamente un proprio script
  eseguibile e ne assume la responsabilità.

Una configurazione Command è quindi codice locale autorizzato dall'utente,
non un formato dati non fidato. Registrare o collocare un simile package nella
directory di discovery autorizza AQTRunCat a eseguirlo.

## 7. Integrazione e validazione automatica

Il modello comune conserva titolo, valore formattato, valore normalizzato,
`metricsBarValue`, stato e icona. Settings identifica le sorgenti come
“Command” o “Snapshot”; non introduce un editor F1. Dashboard e Metrics Bar
consumano lo stesso elenco già usato da M6/M7. Una Command Pending prima del
primo risultato è già eleggibile per la barra e mostra lo stato semantico;
dopo il primo successo conserva il valore durante il refresh. La Dashboard
applica la stessa regola alla card: durante il refresh mantiene l'ultimo
stato concluso, senza alternare Available e "Waiting…".

Le suite dedicate coprono parser/schema, argomenti, percorsi assoluti e
relativi, package discovery/CWD, scheduling, non-overlap, timeout, avvio ed
exit falliti, executable mancante/non eseguibile, output malformato o
eccessivo su entrambi i canali, cleanup, transizioni di stato, simboli e
fallback, configurazioni invalide non eseguite e integrazione Dashboard/
Metrics Bar. Le suite M6 restano la prova di compatibilità snapshot.

Validazione finale (2026-10-16, Plasma 6 Wayland, profilo XDG isolato,
`run --fps 10`, Dashboard chiusa, misura su 30 s dopo 3 s di avvio):

| Scenario | CPU app (% di un core) | RSS | Esito |
|---|---:|---:|---|
| nessun comando (solo snapshot M6) | 0,40 | 60,9 MiB | invariato rispetto a M8 |
| 1 comando `sh`, intervallo 1 s, in barra | 0,63 | 62,2 MiB | valore live nel tooltip SNI |
| 5 comandi, intervalli 1–5 s, 3 in barra | 0,87 | 62,8 MiB | run 23/12/8/6/5 in ~23 s |
| timeout 1 s di uno script con `sleep 30 &` | 0,50 | 62,1 MiB | 17 run, Error a ogni ciclo, nessun `sleep` residuo |
| run 2,5 s con intervallo 1 s | 0,47 | 62,1 MiB | avvii ogni 3,51 s, nessuna sovrapposizione |

Prima di rendere differita la ricostruzione della Dashboard nascosta, lo
scenario a 5 comandi costava 2,20% e quello a 1 comando 0,85%. In tutti gli
scenari `SIGTERM` all'app ha dato exit 0 e nessun processo residuo. A vista
sono stati verificati: snapshot M6 legacy accanto ai comandi, icone `file`
relative SVG, icona `theme`, fallback per icona mancante, `normalizedRange`
nella progress bar ed eseguibile relativo `../m1/run`. Il pixmap SNI della
barra, letto via D-Bus, mostra l'icona file sopra il valore.

Decisioni rinviate: output Command strutturato/multi-valore, editor grafico,
backoff configurabile e policy di trust più granulare. Non servono al
contratto F1 e potranno essere valutati soltanto con un caso d'uso concreto.

Nota successiva (F2): il caso d'uso concreto per l'output multi-valore è
arrivato con la metrica Claude Code. F2 ha aggiunto `source.output:
"snapshot"` e l'exit status 69 → Unavailable come estensioni generiche e
retrocompatibili; il resto di questo documento descrive F1 così come è stato
chiuso. Vedi `docs/integration-claude-code.md` §6.

## 8. v2F1.2 — `enabled` facoltativo (2.1.2)

Stato: **completata** il 2026-10-23, versione **2.1.2** (Custom Metrics v3,
primo passo funzionale). Branch `feature/v2/f2`.

### Schema

Campo facoltativo di primo livello, in entrambi i tipi di sorgente (file
snapshot e configurazione Command / `metric.json`):

```json
{ "id": "example-metric", "title": "Example Metric", "enabled": false, "source": { "type": "command", "...": "..." } }
```

- Assente = `true`: i file esistenti restano validi e non vengono
  riscritti.
- `true` / `false`: abilitata / disabilitata.
- Qualsiasi altro tipo (`"false"`, `0`, `null`) è un errore di validazione
  come gli altri campi: `field "enabled" must be a boolean`; per una
  configurazione Command resta un errore terminale (mai interpretata come
  snapshot).

### Backend

`CustomMetricsService` resta l'unica fonte di verità.

- Una sorgente disabilitata resta nell'elenco (`Source::enabled = false`),
  con stato Unavailable, messaggio "Disabled." e uno snapshot ridotto a
  titolo e icona (come un comando in attesa del primo risultato), così
  Settings e la configurazione del Plasmoid mostrano il suo nome ("D20") e
  non quello del file ("metric"); nessun valore, niente valori vecchi né
  zero inventati.
- Command disabilitato: nessun job, quindi nessuna pianificazione, nessuna
  prima esecuzione, nessun processo. Se viene disabilitato mentre il comando
  gira, `syncCommandJobs` lo ferma con le regole esistenti (SIGTERM al
  gruppo, poi SIGKILL, segnali scollegati): un risultato tardivo non può
  rientrare.
- Snapshot disabilitato: il file viene letto solo per conoscere il flag;
  lo snapshot non viene tenuto. Una sorgente disabilitata non attiva il
  timer di recupero dei file guasti.
- Riattivato: nuovo job eseguito subito (stato Pending "Waiting for the
  first command result."), poi la cadenza normale; lo snapshot torna
  Available alla rilettura. Errori, timeout, exit 69 → Unavailable invariati.

### Scrittura e applicazione

- `custommetrics::writeEnabled()` modifica **solo** il flag: sostituisce il
  valore di primo livello esistente oppure inserisce `"enabled": …` come
  prima chiave con l'indentazione della prima chiave esistente. Ordine delle
  chiavi, formattazione e campi sconosciuti restano byte per byte; il testo
  modificato viene verificato (deve dare lo stesso oggetto più il flag) e poi
  sostituisce il file con `QSaveFile` (file temporaneo nella stessa
  cartella, permessi del file mantenuti, attraverso un symlink verso il suo
  target). File illeggibile, non oggetto JSON o cartella non scrivibile: errore
  leggibile, file intatto. Nessun privilegio.
- `CustomMetricsService::setEnabled()` scrive e ricarica subito (lo stesso
  `reload` di registrazione/rimozione: i job con definizione invariata non
  ripartono), senza aspettare il file watcher; un file modificato a mano viene
  comunque applicato dal watcher come prima. Non è il rescan manuale della
  2.1.6.
- D-Bus (schema 3): `SetCustomMetricEnabled(s path, b enabled)` e il campo
  `enabled` nelle metriche `custom`/`integration`
  (`docs/backend-dbus-api-cli.md` §5). Le sorgenti delle integrazioni si
  accendono e spengono in Integrations: il metodo le rifiuta.

### Frontend

- Settings (tray): nella lista Custom Metrics la prima colonna è il nome
  della metrica (il titolo del file; il nome del file solo se manca, ad
  esempio per un file non valido) con la casella (selezionata = abilitata,
  anche quando il campo manca); poi file, origine e stato ("Disabled").
  La modifica passa dal backend; se il file non si può scrivere compare un
  avviso con il motivo e la riga torna allo stato reale.
- Configurazione del Plasmoid: la stessa casella accanto a ogni file (nascosta
  con un backend senza il campo).
- Dashboard, Metrics Bar e popup del Plasmoid non mostrano le sorgenti
  disabilitate. Una selezione della Metrics Bar resta salvata (in Settings
  "Custom — disabled") e la voce torna al suo posto quando la sorgente viene
  riabilitata; una sorgente disabilitata non selezionata non viene offerta.

### Correzioni successive (stessa 2.1.2)

- Popup del Plasmoid: quando una metrica compariva o spariva (abilitata,
  disabilitata, file aggiunto o tolto) `Logic.syncModel` svuotava e
  ricostruiva il modello e la lista tornava in cima. Ora rimuove, sposta e
  inserisce le singole righe: la posizione di scorrimento resta (provato con
  backend reale e lista scorsa in fondo: prima 396 → −4, ora resta in fondo).
  Difetto presente dalla F4.2.
- Popup del Plasmoid, secondo difetto (segnalato dopo la prima correzione):
  a ogni esecuzione di un comando la metrica tornava "pending" e la riga
  nascondeva valori, barre e voci ("Command is running."), poi li rimostrava:
  la lista cambiava altezza (misurato 1212 → 1066 → 1212 px con una metrica a
  tre voci) e scorreva. Ora `Logic.settleRefreshes` fa come la Dashboard Qt:
  una sorgente che ha già dei valori continua a mostrare l'ultimo stato
  stabile (con il suo messaggio) durante il refresh; solo una sorgente senza
  ancora valori mostra "pending". Altezza costante (1212 px) nella stessa
  prova.
- Dashboard Qt: a ogni esecuzione di un comando tutte le card Custom Metrics
  venivano distrutte e ricreate (sfarfallio). Ora, se l'elenco delle sorgenti
  è lo stesso, viene sostituita solo la card il cui contenuto mostrato è
  cambiato, al suo posto e con il ridisegno sospeso; un refresh che mostra lo
  stesso valore non tocca nulla.

### Limite

Un producer esterno che riscrive un file snapshot senza `enabled` lo
riabilita (campo assente = `true`): il flag vive nel file. Per i file
prodotti da altri programmi conviene che sia il producer a scriverlo.

### Test

`commandmetric` (`enabledFlag`: assente, true, false, stringa, numero,
null), `custommetric` (flag degli snapshot, `writeEnabled` che conserva il
resto del file, i permessi e i symlink, rifiuti senza danni),
`commandmetricsservice` (disabilitato mai eseguito; disattivazione con
processo in corso → processo terminato, nessun risultato tardivo;
riattivazione; rifiuti: sorgente sconosciuta, cartella non scrivibile,
integrazione), `custommetricsservice` (snapshot disabilitato e producer che
lo riscrive), `settingswindow` (casella, file aggiornato, Metrics Bar,
errore di scrittura), `dashboard`, `metricsbarmodel`, `apischema` (schema
3, andata e ritorno, payload senza campo), `backendipc` (metodo D-Bus,
`GetMetrics` e `MetricsChanged`, errori), `plasmoidlogic`, `plasmoidpackage`
(metodo ammesso).

## 9. v2F1.3 — Avvisi di esecuzione (2.1.3)

Stato: **completata** il 2026-10-23, versione **2.1.3**. Branch
`feature/v2/f3`.

### Modello di stato

Il modello esistente bastava: quattro stati (Pending, Available,
Unavailable, Error) più un messaggio, già trasportati dal payload D-Bus
(`state`, `message`) e già distinti dai frontend. Nessun nuovo stato, nessun
campo nuovo, schema API invariato (3). La 2.1.3 rende coerente la
classificazione e la presentazione.

| Esito | Stato | Messaggio |
|---|---|---|
| exit 0 e output valido | Available | vuoto (un successo cancella l'avviso precedente) |
| exit 69 | **Unavailable** (intenzionale, invariato) | stderr fino a 512 caratteri, altrimenti "Source is not available." |
| altro exit ≠ 0 | Error | "Command exited with status N." + la prima riga non vuota di stderr, al massimo 200 caratteri |
| eseguibile mancante | Error (prima Unavailable) | "Executable not found: <percorso>" |
| file non eseguibile | Error (prima Unavailable) | "Permission denied: <percorso> is not executable." |
| avvio fallito (interprete inesistente…) | Error | "Could not start executable: …" |
| timeout | Error | "Command timed out after N seconds."; il gruppo di processi viene ucciso, un output tardivo non sostituisce l'esito |
| terminato da un segnale / output oltre 16 KiB | Error | "Command crashed." / "Command output exceeds the … limit." |
| exit 0 ma output non valido | Error | "Invalid output: " + l'errore del validatore (scalare, numero con `normalizedRange`, snapshot) |

Una metrica disabilitata non viene eseguita e non genera avvisi. Gli errori
non disattivano nulla: il comando riparte alla cadenza configurata, e ogni
esito sostituisce il precedente (Error → Available, Error ↔ Unavailable,
Unavailable → Available). Durante un refresh Dashboard e Plasmoid mostrano
l'ultimo stato stabile, quindi un errore resta visibile finché il risultato
successivo non lo sostituisce.

Modifica deliberata di una regola di F1 (`docs/feature-reference.md`):
programma mancante o non eseguibile era Unavailable; ora è un errore di
esecuzione, perché Unavailable resta riservato alla scelta esplicita del
comando (exit 69). Per un'integrazione reinstallata in un altro prefisso
la card mostra quindi l'errore "Executable not found" invece di
"Unavailable" (basta ripetere `enable`).

### Diagnostica e log

- Nei frontend arriva solo il messaggio breve: codice di uscita, timeout,
  motivo dell'avvio fallito, errore di validazione, prima riga di stderr
  (limitata). Mai l'intero output, mai l'ambiente o la riga di comando
  completa; stdout e stderr restano limitati a 16 KiB come prima.
- Log del backend, categoria `aqtruncat.custommetrics`: una riga di avviso
  quando una sorgente inizia a fallire o fallisce in modo diverso
  (`custom metric "<id>" failed: <messaggio>`) e una informativa quando si
  esce dall'errore (`… recovered`, anche verso Unavailable); mai a ogni
  esecuzione con lo stesso errore. Senza terminale il backend scrive nel
  journal (comportamento di Qt); con `QT_FORCE_STDERR_LOGGING=1` su stderr.
- Payload D-Bus: in Error resta l'ultimo snapshot valido (come prima, è lo
  stato a dire che non è attuale): tutti i client AQTRunCat lo nascondono.
  `aqtruncatctl metrics` (testo) stampa solo `(error: <motivo>)`, senza il
  valore né le voci; `--json` resta il payload completo.

### Presentazione

- Dashboard Qt: in Error un'icona di errore accanto al nome (tooltip con il
  motivo), "—" al posto dei valori, le barre nascoste ma con il loro spazio
  (il layout non cambia), riga di stato "Error: <motivo>". Unavailable resta
  come prima: valori conservati, "Unavailable", nessuna icona.
- Metrics Bar: "!" per Error (già così), "—" per Unavailable, "…" per
  Pending; in Error il tooltip non riporta più l'ultimo valore ma
  "<nome>: Error" e "Error: <motivo>".
- Plasmoid: già distinto (icona `dialog-error` per Error, `dialog-warning`
  per Unavailable, "—" e il motivo nella riga di dettaglio, nessuna voce né
  barra); verificato da un test, nessuna logica di stato in QML.
- Settings: colonna Stato "Error" con un'icona di errore e il motivo nel
  tooltip; "Unavailable" senza icona.

### Test

`commandmetricsservice`: programma mancante e non eseguibile → Error;
exit 1 con stderr lungo e su più righe (prima riga, limitata), exit 2, 70,
127, 255; output scalare non numerico con `normalizedRange` e snapshot non
valido → "Invalid output"; timeout senza output tardivo; transizioni
Error → Available → Unavailable (69, valore conservato) → Available →
Error con i messaggi cancellati e le righe di log attese (una per cambio).
`dashboard` (icona, "—", motivo; Unavailable senza icona e con il valore),
`metricsbarmodel` ("!", tooltip senza valore vecchio, ritorno al valore),
`backendipc` (error → unavailable → ok via `GetMetrics`, di nuovo error:
`aqtruncatctl metrics` senza il valore vecchio), `plasmoidlogic` (riga di
errore senza valore).

Runtime isolato (backend reale, bus privato, script che legge la modalità
da un file): fail → `error | Command exited with status 3. sensor offline`;
69 → `unavailable | service not running`; ok → `ok | 42`; fail di nuovo →
error. Log (`QT_FORCE_STDERR_LOGGING=1`): `failed`, `recovered`, `failed`;
la seconda riga di stderr (`token=SECRET…`) non compare né nel messaggio né
nel log.

## 10. v2F1.4 — `display.showBar` facoltativo (2.1.4)

Stato: **completata** il 2026-10-23, versione **2.1.4**. Branch
`feature/v2/f3`.

### Comportamento prima

Una barra compariva nella Dashboard Qt e nel popup del Plasmoid per ogni
voce con un valore normalizzato (`display.normalizedRange` per l'output
scalare, `normalizedValue` delle voci di uno snapshot); nessuna barra per i
valori testuali. La Metrics Bar non disegna barre (icone con testo).

### Schema

```json
{ "display": { "showBar": false } }
```

- Configurazioni Command: `display.showBar` booleano, facoltativo. Con
  `"output": "snapshot"` `display` era vietato; ora può contenere **solo**
  `showBar` (prefissi, suffissi e `normalizedRange` restano dell'output
  scalare).
- File snapshot: `display.showBar` letto allo stesso modo; le altre chiavi di
  `display` sono ignorate come ogni chiave sconosciuta.
- Assente = comportamento 2.1.3 identico; `true` = lo stesso (una barra solo
  dove c'è un valore normalizzato, mai inventata); `false` = nessuna barra.
  Un tipo non booleano (`"false"`, `0`, `null`) o un `display` che non è un
  oggetto è un errore di validazione come gli altri.

### Architettura

- `custommetrics::parseShowBar()` condiviso dai due parser;
  `CommandDefinition::showBar` e `Snapshot::showBar` (anche negli scheletri
  e nello snapshot dell'output snapshot, dove decide la configurazione).
- Solo presentazione: esecuzione, cadenza, timeout, valori e `normalized`
  invariati (i test lo verificano).
- Payload (schema 4, additivo): `showBar` effettivo sulle metriche
  `custom`/`integration`; assente = `true`.
- Dashboard: con `false` la barra non viene creata (la card è più bassa,
  nessuno spazio vuoto); in errore restano icona, "—" e motivo (2.1.3).
- Plasmoid: `Logic.itemRows`/`metricRow` azzerano la frazione con
  `showBar === false`; nessun parsing di configurazione in QML.
- Metrics Bar: nessuna modifica (non ha barre); Settings: nessun editor delle
  proprietà di presentazione, quindi nessun controllo nuovo: si scrive nel
  file, e il file watcher lo applica senza riavvio.

### Test

`commandmetric` (`showBarFlag`: assente, true, false, stringa, numero,
null; valore e `normalized` invariati; output snapshot con solo `showBar`
accettato, con `suffix` rifiutato, `display` non oggetto), `custommetric`
(file snapshot), `apischema` (schema 4, andata e ritorno, payload senza
campo), `dashboard` (nessuna barra, card più bassa, valore/titolo/stato,
errore e Unavailable senza barra, `true` senza valore normalizzato),
`plasmoidlogic`, `metricsbarmodel` (nessuna differenza), `backendipc`
(`showBar` false via `GetMetrics`, `normalized` presente).

Runtime isolato: backend reale su bus privato e il vero
`FullRepresentation.qml` offscreen con due metriche uguali ("12 / 20",
`normalizedRange` 1–20): quella con `showBar: false` occupa una sola riga
(titolo e valore), quella predefinita ha la barra sotto.

## 11. v2F1.5 — Gruppi ed ereditarietà JSON (2.1.5)

Stato: **completata** il 2026-10-23, versione **2.1.5**. Branch
`feature/v2/f4`.

### Esempio completo

```text
~/.local/share/AQTRunCat/metrics/
└── dice-metrics/
    ├── metric.json          il gruppo
    ├── dice.svg
    ├── random-metric.sh
    ├── d6/metric.json       i figli
    ├── d20/metric.json
    └── d100/metric.json
```

`random-metric.sh` (eseguibile):

```sh
#!/bin/sh
# un lancio di un dado con $1 facce
shuf -i "1-$1" -n 1
```

`dice-metrics/metric.json`:

```json
{
  "id": "dice-metrics",
  "title": "Dice Metrics",
  "symbol": { "type": "file", "value": "./dice.svg" },
  "source": {
    "type": "group",
    "executable": "./random-metric.sh",
    "arguments": [],
    "intervalSeconds": 5,
    "timeoutSeconds": 3
  },
  "display": { "showBar": false }
}
```

`dice-metrics/d20/metric.json` (d6 e d100 uguali con 6 e 100):

```json
{
  "title": "D20",
  "source": { "type": "command", "arguments": [20] },
  "display": {
    "normalizedRange": { "minimum": 1, "maximum": 20 },
    "showBar": true
  }
}
```

Risultato: Dashboard e Plasmoid mostrano una card "Dice Metrics" con
`D6 4  D20 17  D100 73` (la barra solo per D20); Settings un albero con il
gruppo e i tre figli.

### Regole

- **Gruppo**: un `metric.json` di package (scoperto, o registrato
  esplicitamente) con `"source": {"type": "group"}`. Richiede `id` e
  `title`; `symbol` ed `enabled` facoltativi; il resto di `source`, `display`
  ecc. vale solo come configurazione ereditata. Non viene mai eseguito. Un
  gruppo in un file che non si chiama `metric.json` è rifiutato (i suoi figli
  sono le sottocartelle del package).
- **Figli**: le sottocartelle del gruppo che contengono un `metric.json`, in
  ordine naturale (d6, d20, d100), al massimo 256 per gruppo; le altre
  sottocartelle sono ignorate. Un figlio il cui tipo effettivo resta `group`
  è un gruppo annidato (al massimo 4 livelli; oltre: errore "Groups are
  nested too deeply."); un collegamento simbolico verso un antenato non viene
  seguito.
- **Ereditarietà**: configurazione effettiva = merge ricorsivo
  (`metricgroup.cpp`, `deepMerge`): oggetti fusi chiave per chiave, array e
  scalari sostituiti interi (gli `arguments` non si concatenano). `id` ed
  `enabled` non si ereditano. Il risultato passa dal validatore Command
  esistente: un figlio senza programma (né suo né ereditato) è un errore come
  una metrica singola. Un figlio di tipo diverso da `command`/`group` è un
  errore.
- **Percorsi**: `source.executable` e il `value` di un `symbol` di tipo
  `file` si risolvono rispetto alla cartella del file che li ha dichiarati:
  ereditati → cartella del gruppo, ridefiniti → cartella del figlio (anche
  `../`); mai la cartella di lavoro del processo; i file non vengono
  riscritti. La cartella di lavoro di un figlio è la sua cartella. Le regole
  di esecuzione restano quelle di F1 (eseguibile diretto, nessuna shell,
  nessuna elevazione).
- **Argomenti numerici**: da 2.1.5 `arguments` accetta numeri (`[20]` →
  `"20"`), per ogni metrica Command.
- **Identità**: id del figlio = il suo `id` esplicito o il nome della
  cartella (stesse regole di formato); `configuredId` gerarchico
  `dice-metrics/d20`; due figli con lo stesso id nello stesso gruppo: il
  secondo è un errore "Duplicate id". L'identità della sorgente (e quindi
  la selezione nella Metrics Bar) resta il percorso canonico del
  `metric.json` del figlio: stabile ai riavvii, distinta dalle metriche
  singole (i cui id non possono contenere `/`).
- **enabled**: stato effettivo = gruppi antenati abilitati **e** il proprio
  flag. Un gruppo disabilitato disabilita tutti i discendenti (anche con
  `"enabled": true`, "Disabled by its group."); riabilitarlo non tocca i file
  dei figli, quindi un figlio spento resta spento.
- **Esecuzione**: ogni figlio è un job Command come gli altri (proprio
  intervallo, timeout, processo, stato, avvisi 2.1.3, exit 69); nessun
  timer in più.

### API (schema 5)

`kind: "group"` per il contenitore; `data.groupId` sui figli;
`data.configuredEnabled` (flag del file) accanto a `enabled` (effettivo);
`data.configuredId` gerarchico. Vedi `docs/backend-dbus-api-cli.md` §5.3.
Nessun frontend legge i `metric.json`.

### Presentazione

- Dashboard: una card per gruppo (icona e titolo), i figli abilitati come
  celle compatte "titolo valore" disposte da un `FlowLayout` che va a capo
  secondo la larghezza; barra piccola solo per i figli con `showBar` e un
  valore normalizzato; in errore icona e "—" con il motivo nel tooltip; un
  gruppo senza figli abilitati non compare.
- Plasmoid: lo stesso con un `Flow` (`Logic.groupCells`), barra larga quanto
  il testo della cella.
- Metrics Bar: i figli sono voci normali, selezionabili come prima; etichetta
  "Gruppo — Figlio"; con l'icona del gruppo (ereditata) la didascalia è il
  titolo del figlio, con un'icona propria (`symbol` nel suo `metric.json`)
  resta quella; i gruppi non sono voci.
- Icona propria di un figlio: nella Dashboard e nel Plasmoid compare nella
  sua cella, prima del titolo, solo se diversa da quella del gruppo (quella
  ereditata non viene ripetuta in ogni cella).
- Settings (tray): la lista Custom Metrics è un albero (gruppo, figli sotto);
  ogni casella è il flag del proprio file; sotto un gruppo spento un figlio
  acceso appare in grigio con "Disabled by group". Configurazione del
  Plasmoid: stesse caselle, figli rientrati.
- Applicazione senza riavvio: le modifiche ai file (anche nuove
  sottocartelle) passano dal file watcher; la rilettura manuale completa è
  la 2.1.6.

### Test

`metricgroups` (nuova): merge ricorsivo e array, id dei figli, origine dei
percorsi (ereditati, ridefiniti con `../`, icona, gruppi annidati), gruppo di
dadi via servizio (contenitore mai eseguito, argomenti numerici, valori
distinti, icona e `showBar` ereditati o ridefiniti), `enabled` ereditato e
riabilitazione senza toccare i figli, figli con errore/timeout/69
indipendenti e figlio senza programma, JSON non validi (gruppo, figlio),
duplicati, gruppo vuoto, profondità e ciclo di symlink, regole di
registrazione. Inoltre `dashboard` (card compatta, `FlowLayout` che va a
capo, errore di un figlio, gruppo vuoto nascosto), `metricsbarmodel`,
`apischema` (schema 5), `plasmoidlogic` (riga di gruppo), `settingswindow`
(albero, "Disabled by group", file dei figli intatti), `backendipc` (gruppo
e figli via `GetMetrics`).

Runtime isolato: backend reale e il vero popup del Plasmoid offscreen con
l'esempio sopra: "Dice Metrics" con `D6 3  D20 9  D100 56`, barra solo sotto
D20.

Correzione collaterale: `tst_claudecodeintegration` assumeva che "fra due
ore" fosse lo stesso giorno (falliva tra le 22 e mezzanotte); ora si aspetta
il formato con la data quando il reset cade il giorno dopo.

## 12. v2F1.6 — Ricarica delle Custom Metrics (2.1.6)

Stato: **completata** il 2026-10-24, versione **2.1.6**. Branch
`feature/v2/f1.6`.

Rilettura esplicita, a richiesta, di tutta la configurazione Custom Metrics
(file registrati, cartella di scoperta, package, gruppi e figli a ogni
livello) e applicazione di ciò che è cambiato, senza riavviare backend,
tray o widget. Il file watcher di M6 resta com'era (non è un nuovo
meccanismo automatico, né una scansione periodica): la ricarica è la
rilettura completa e garantita, utile per esempio dopo aver copiato un
package di gruppo (una cartella figlio nuova non ancora osservata dal
watcher), dopo modifiche a più file, o per sapere con certezza cosa è stato
applicato.

### Come si usa

- **Settings** (tray), gruppo *Custom Metrics*: pulsante **Reload Custom
  Metrics**. Spento finché la richiesta è in corso; l'esito compare sotto la
  lista ("Custom Metrics reloaded: 1 added, 2 changed, 0 removed, 3
  unchanged." più gli eventuali file non validi, oppure "Custom Metrics not
  reloaded: … The active configuration is unchanged."). La lista si
  aggiorna e la riga selezionata resta selezionata.
- **Widget Plasma**, pagina di configurazione *AQTRunCat*: stesso pulsante,
  esito nel messaggio in alto (`docs/plasma-widget.md` §15b).
- **CLI**: `aqtruncatctl custom reload` (riepilogo e una riga per ogni
  sorgente aggiunta, cambiata, rimossa o con errore; `--json` = il report
  dell'API; uscita 1 se rifiutata).
- **D-Bus**: `ReloadCustomMetrics() → s` (schema 6, capability
  `custom-metrics-reload`; `docs/backend-dbus-api-cli.md` §5.1).

I frontend non leggono mai i file: chiedono al backend, che è l'unico a
rileggere.

### Riconciliazione

`CustomMetricsService::reload` costruisce la configurazione candidata
**a parte** (stesse funzioni di sempre: `readConfigObject`,
`parseCommandFile`/`parseFile`, `appendGroup`/`appendChildren` con
`effectiveChild`; nessun secondo parser) e solo alla fine la confronta con
quella attiva, per id stabile (percorso canonico del file):

- **Aggiunta / rimossa / cambiata / invariata**: ogni `Source` porta la sua
  `configuration` (JSON effettivo compatto — per un figlio quello già fuso
  con gli antenati — oppure il motivo per cui non si legge). Cambiata =
  `configuration`, tipo o gruppo di appartenenza diversi. Così un cambio del
  gruppo che un figlio eredita rende cambiato anche quel figlio, mentre un
  figlio che ridefinisce il valore resta invariato. Spazi e ordine delle
  chiavi non contano.
- **Job dei comandi** (`syncCommandJobs`, `definitionChange`):
  - nessuna differenza → il job continua, valore e stato restano;
  - solo **presentazione** (`title`, `symbol`, `display.showBar`, `id`,
    `integration`) → nessun riavvio: il processo in corso continua, l'ultimo
    valore resta e prende titolo/icona/showBar nuovi (`applyPresentation`);
  - solo **pianificazione** (`intervalSeconds`, `timeoutSeconds`) → nessun
    riavvio: la scadenza della corsa in corso diventa inizio + nuovo
    timeout, la prossima corsa fine dell'ultima + nuovo intervallo;
  - **esecuzione** (programma, argomenti, cartella, `output`, prefisso,
    suffisso, `normalizedRange`) → il job viene sostituito: il gruppo di
    processi della corsa in corso viene terminato (stesso `stopCommand` di
    sempre), il nuovo parte subito e il valore vecchio non viene più
    mostrato ("Waiting for the first command result.");
  - `enabled` false → nessun job (fermato); true → job nuovo, parte subito.
- Le metriche built-in, le impostazioni e la selezione della Metrics Bar
  (che usa l'id stabile) non vengono toccate; una metrica rimossa e poi
  ripristinata torna nel suo posto della Metrics Bar.
- Nessun file viene riscritto dalla ricarica.

### Fallimenti e atomicità

Due livelli, scelti per non cambiare la politica già stabilita da M6 e 2.1.5
(ogni file fallisce da solo):

1. **Rifiuto completo** — se la cartella di scoperta esiste ma non è una
   cartella leggibile ed attraversabile, oppure una sua sottocartella
   (package, cartella di un gruppo o di un figlio) non si può leggere o
   attraversare, la ricarica **non viene applicata**: sorgenti, job,
   processi in corso, valori e watch restano esattamente quelli di prima,
   nessun `sourcesChanged`, e l'errore ("Cannot read the directory …") torna
   a chi l'ha chiesta (`ok=false`; D-Bus `Error.Failed`). Senza questo
   controllo tutte le metriche sotto quella cartella sembrerebbero rimosse.
   Vale anche per le riletture del watcher (riprova come prima, poi lascia
   lo stato invariato, una riga di log per motivo). Una cartella di scoperta
   **assente** non è un errore: è vuota (viene ricreata all'avvio).
2. **Errori per file** (politica esistente) — JSON non valido, gruppo non
   valido, id duplicato in un gruppo, comando non valido: quella sorgente
   resta nella lista in stato Error con il motivo (mai rimossa né
   disabilitata in silenzio), le altre vengono applicate; il report le elenca
   in `errors`, e Settings/widget/CLI le mostrano. Un eseguibile mancante
   resta un errore di esecuzione 2.1.3 ("Executable not found"), non di
   configurazione.

### Avvisi (la metrica continua a funzionare)

Alcuni problemi non fermano una metrica ma vanno segnalati. Sono di due
tipi:

- **avvisi della sorgente** (`Source::warnings`, campo `warnings[]`
  dell'API): oggi l'**icona mancante**. Un `symbol` di tipo `file` che non
  esiste o non è leggibile ("icon file … does not exist" / "cannot be
  read"), oppure un `symbol` di tipo `theme` che non si trova in nessun tema
  installato ("icon "…" is not in any installed icon theme"). Il backend non
  ha una sessione grafica: il nome viene cercato come prevede la specifica
  freedesktop (`~/.icons`, `$XDG_DATA_HOME/icons`, `$XDG_DATA_DIRS/*/icons`,
  le cartelle elencate da ogni `index.theme`, `/usr/share/pixmaps`, estensioni
  png/svg/svgz/xpm) ma in **tutti** i temi, perché ogni frontend può usarne
  uno diverso; i ripieghi di Qt/KDE sul nome (es. senza l'ultimo `-…`) non
  contano. Il controllo si rifà a ogni ricarica: un'icona aggiunta dopo si
  trova alla ricarica successiva (il file dell'icona non è osservato). I
  frontend usano la loro icona di ripiego, come prima.
- **avvisi di valore** (sulla voce): un comando scalare con
  `display.normalizedRange` che stampa un valore fuori dall'intervallo, o un
  `normalizedValue` fuori da 0–1 in uno snapshot. Il valore resta mostrato,
  la barra viene limitata (piena o vuota); la voce ha `warning` e nel
  dettaglio "outside the range 0–1" (per uno snapshot: "normalizedValue 1.5
  outside the range 0–1", dopo l'eventuale dettaglio del produttore).
  Ricalcolato a ogni corsa.

Dove si vedono (sempre sotto la metrica, con l'icona di avviso, come il
dettaglio di una voce):

- **Dashboard Qt**: avvisi della sorgente sotto i valori della card (e sotto
  il titolo di un gruppo); nelle celle dei gruppi, sotto la cella, gli avvisi
  del figlio e quelli dei suoi valori.
- **Widget, popup**: stesse posizioni (riga della metrica, celle dei gruppi).
- **Widget, configurazione** e **Settings del tray**: tutti gli avvisi della
  metrica, inclusi quelli di valore ("Warning: value 10 (outside the range
  0–1)"); Settings con l'icona nella colonna di stato e il testo nel tooltip.
- **CLI**: `aqtruncatctl metrics` stampa "! …" sotto la metrica (e le voci
  con "!" e il dettaglio); `custom reload` righe "warning: nome: messaggio".
- **Report di ricarica**: voce `warnings` (avvisi della sorgente e di valore),
  riepilogo "N with warnings: …".
- **Log**: una riga quando l'avviso compare (un valore che resta fuori range
  non viene riscritto a ogni corsa).

Gli avvisi non contano come modifica della configurazione.

### Processi e concorrenza

- Ogni corsa ha un id (`CommandJob::run`); tutti i callback di un processo
  (output, fine, `FailedToStart` differito) controllano id e job: il
  risultato di una corsa sostituita o rimossa non può più comparire, anche
  se arriva dopo la ricarica.
- Nessun processo o timer orfano: un job rimosso o sostituito passa da
  `stopCommand` (SIGTERM al gruppo, ≤250 ms, SIGKILL); limiti di output,
  timeout e un solo processo per job restano quelli di F1.
- La ricarica è sincrona nel thread del backend (nessun thread nuovo): le
  richieste D-Bus vengono servite una dopo l'altra; una richiesta rientrante
  (da un gestore di `sourcesChanged`) viene rifiutata ("already running") e
  raccolta in una sola rilettura subito dopo. Il client Qt e il widget
  inviano una sola richiesta alla volta (le altre condividono la risposta).
- Il backend pubblica il nuovo insieme (`MetricsChanged`) prima della
  risposta, così chi aggiorna la vista alla risposta lo ha già.

### Frontend

Dashboard, Metrics Bar, Settings e popup del widget seguono come per ogni
altra variazione (`sourcesChanged` nel tray, delta `MetricsChanged` nel
widget): card e celle di gruppo ricostruite, metriche rimosse sparite,
titoli/icone/`showBar` aggiornati, regole di visibilità, Error e
Unavailable invariati. Nessuna finestra va chiusa e riaperta.

### Test

`custommetricsreload` (nuova, 19 casi, incluso `warningsDoNotStopAMetric`: icona file e tema mancanti, valore fuori range, avvisi che spariscono dopo la correzione): nessuna modifica (nessuna nuova
corsa, valori e id invariati), metrica singola aggiunta/cambiata/rimossa
(il job rimosso non gira più), snapshot RunCat Neo, gruppo e figli
aggiunti/rimossi, cambio ereditato dal gruppo con figlio che ridefinisce
l'intervallo (non toccato), `enabled` su figlio e gruppo, modifica di sola
presentazione senza riavvio (valore e normalizzazione conservati), cambio
di programma durante una corsa (processo terminato, risultato vecchio mai
visto), cambio di titolo durante una corsa (stessa corsa, titolo nuovo),
JSON non valido e id duplicato (errori per file nel report), cartella di
scoperta / package / figlio non leggibili (nulla applicato, nessuna
pubblicazione), cartella assente, richiesta rientrante, avvisi 2.1.3
conservati, Dashboard e Metrics Bar che seguono la ricarica, servizio
fermo. Inoltre: `backendipc` (report, delta prima della risposta, client Qt
asincrono, 4 richieste simultanee senza nuove corse, `aqtruncatctl custom
reload` anche `--json`, rifiuto `Failed` con metriche invariate),
`settingswindow` (pulsante, esito, rifiuto, risposta differita con pulsante
spento), `apischema` (schema 6, report in entrambi i sensi),
`plasmoidclient` (widget reale: report, gruppo e figlio, rimozione,
rifiuto), `plasmoidlogic` (`reloadSummary`), `plasmoidpackage` (metodo
consentito).

Build pulita in una cartella nuova: 0 warning; ctest **55/55**;
`packaging-versions` 2.1.6.

Runtime isolato (bus privato, HOME temporanea, `aqtruncatd` e `aqtruncatctl`
reali, gruppo di dadi con script `roll`): ricarica senza modifiche "0 added,
0 changed, 0 removed, 3 unchanged."; nuova cartella figlio `d100` creata e
riempita in un colpo, `d6` rinominato e intervallo del gruppo cambiato, in
una sola ricarica: "1 added, 3 changed" (`dice`, `dice/d6`, `dice/d20`) e
ogni figlio eseguito **una sola volta** (nessun riavvio per titolo o
intervallo); JSON rotto in `d20`: errore elencato, uscita 0, `d20` resta in
lista in errore; `chmod 000` sulla cartella del gruppo: uscita 1 "Cannot
read the directory …", metriche invariate; backend chiuso con uscita 0,
nessun processo rimasto.

Da verificare nella sessione reale (non eseguito qui): il pulsante nella
finestra Settings del tray installato e quello della configurazione del
widget dopo il riavvio di plasmashell.

## 13. v2F1.7 — Aggiornamento manuale dall'icona (2.1.7)

Stato: **completata** il 2026-10-24, versione **2.1.7**. Branch
`feature/v2/f1.7`. Ultima sotto-funzione di v2F1: con questa **v2F1 —
Custom Metrics v3 è completa**.

Un clic sull'icona di una Custom Metric chiede subito un nuovo campione,
senza aspettare l'intervallo. È un'occasione in più di esecuzione, non una
ricarica della configurazione (§12): non rilegge i file e non cambia
l'intervallo configurato.

### Dove si clicca

- **Dashboard Qt**: l'icona della card di una metrica, quella di un
  gruppo e l'icona propria di un figlio nella cella del gruppo (un figlio
  che usa l'icona del gruppo non ne ha una sua: si aggiorna con quella del
  gruppo). Cursore a mano e suggerimento "Refresh now" / "Refresh the group
  now"; solo il tasto sinistro. L'esito compare per 2,5 s come suggerimento
  sotto l'icona ("Refreshing…", "Already running or just refreshed",
  "Nothing to refresh: no enabled metric" o il motivo del rifiuto). Nessuna
  finestra di dialogo; layout e aspetto dell'icona invariati.
- **Metrics Bar**: ogni voce è un'icona del vassoio il cui clic sinistro
  apre/chiude la Dashboard (invariato). Per non cambiarlo, l'aggiornamento
  è sul **clic centrale** della voce e sulla prima voce del suo menu,
  **Refresh Now**. Solo le voci Custom Metrics; i gruppi non sono voci della
  barra (§11), quindi il bersaglio è sempre la singola metrica. Nessun
  riscontro visivo oltre al nuovo valore.
- **Widget Plasma**: icona della riga nel popup, del gruppo e propria del
  figlio, come nella Dashboard; esito per 3 s nell'intestazione del popup
  (`docs/plasma-widget.md` §15c). Pannello invariato.
- **CLI**: `aqtruncatctl custom refresh ID|PATH` (id `custom:<percorso>`
  oppure il percorso del file; `--json` = la risposta dell'API; uscita 4 se
  rifiutata).
- **D-Bus**: `RefreshCustomMetric(s id) → s` (schema 7, capability
  `custom-metrics-refresh`; `docs/backend-dbus-api-cli.md` §5.1).

Tutti i frontend usano lo stesso metodo del backend; nessuno esegue script.
Built-in e file snapshot (RunCat Neo, riletti quando cambiano) non hanno
nulla da aggiornare: le loro icone non sono cliccabili. Una metrica senza
icona (o con il solo simbolo testuale legacy) non ha un'icona da cliccare
nella Dashboard e nel widget: resta la Metrics Bar o la CLI.

### Backend

`CustomMetricsService::refreshNow(sourceId)` (e `refreshMetric`, che emette
`refreshFinished`; `BackendService::RefreshCustomMetric` lo espone):

1. Risolve l'id nel registro attivo (`m_sources`).
2. **Metrica**: deve essere un comando abilitato con un job (configurazione
   valida). **Gruppo**: i discendenti eseguibili abilitati, gruppi annidati
   compresi, nell'ordine del servizio (i figli seguono il loro gruppo); un
   gruppo annidato disabilitato esclude i suoi figli (che sono comunque
   disabilitati per ereditarietà). Il gruppo non esegue nulla di suo; ogni
   figlio gira con il **proprio** job: comando, argomenti, timeout e stato
   suoi, anche se ereditati dallo stesso eseguibile.
3. Per ogni metrica chiama `startCommand`, lo stesso avvio dello scheduler
   periodico: processo nel suo gruppo, limiti di output, timeout, uscita 69
   → Unavailable, errori → Error, risultato che cancella l'errore
   precedente, log solo sulle transizioni. Non c'è un secondo percorso di
   esecuzione.

Risposta immediata (D-Bus non attende lo script): `started` (avviate ora) e
`coalesced` (vedi sotto). Lo stato passa a Pending ("Command is running.")
e il valore arriva con le notifiche di sempre (`sourcesChanged` →
`MetricsChanged`) a tutti i frontend collegati. La Dashboard e il widget,
come per ogni corsa periodica, continuano a mostrare l'ultimo stato stabile
durante la corsa (nessun valore inventato, nessun salto della card).

Rifiuti (`RefreshStatus` → errore D-Bus): id vuoto, id non `custom:` o file
snapshot → Invalid (`InvalidArgument`); id sconosciuto (anche una metrica
rimossa) → NotFound; metrica, figlio o gruppo disabilitato → Disabled
(`InvalidArgument`, "… is disabled."); comando o gruppo con configurazione
non valida, servizio fermo → NotRunnable (`Failed`). Gruppo abilitato senza
figli abilitati: accettato, liste vuote, nessuna esecuzione.

### Concorrenza: politica scelta

- **Mai due processi della stessa metrica.** Se la metrica è in esecuzione
  (`job.process`), la richiesta è **accorpata** (`coalesced`) a quella
  corsa: nessuna coda.
- **Spaziatura di 1 s** (`ManualRefreshSpacingMilliseconds`): una richiesta
  per una metrica terminata da meno di un secondo è accorpata a quel
  risultato, appena prodotto. Clic ripetuti quindi non possono produrre più
  di una corsa per (durata + 1 s), né una raffica di processi uno dopo
  l'altro. Conseguenza: una metrica con intervallo di 1 s risulta quasi
  sempre "già aggiornata".
- **Nessuna coda illimitata**: niente viene messo in attesa; ogni richiesta
  ha subito la sua risposta. Il client Qt e il widget inviano una sola
  richiesta per id alla volta; le altre vengono scartate finché arriva la
  risposta.
- **Limite globale**: AQTRunCat non ha un tetto globale di processi oltre a
  "un processo per metrica" (già così dalla F1); il refresh di un gruppo
  avvia al massimo un processo per figlio abilitato, e clic ripetuti sul
  gruppo non ne aggiungono altri.
- Timeout, SIGTERM/SIGKILL al gruppo di processi e pulizia allo stop
  restano quelli di F1/§12.

### Rapporto con lo scheduler periodico

Scelta: il refresh **sposta la prossima scadenza** della metrica
aggiornata, che diventa un intervallo dopo la fine di questa corsa
(`finishCommand`, come dopo ogni corsa). È il cambiamento minimo: nessun
timer nuovo o permanente (il risveglio unico dello scheduler viene solo
ricalcolato per la scadenza del timeout), nessuna corsa periodica subito
dopo quella manuale, nessun altro job toccato. Una corsa periodica e un
clic quasi simultanei non si sovrappongono: chi arriva secondo trova il
processo (o il risultato di meno di 1 s) e non avvia nulla.

### Ricarica e rimozione durante un refresh

Restano le garanzie di §12: una modifica di sola presentazione o di
pianificazione non tocca la corsa in atto (il suo risultato arriva); una
modifica di esecuzione la termina e riparte, e il risultato della corsa
sostituita non compare mai (id della corsa); una metrica rimossa ferma il
processo, i suoi callback vengono ignorati e un nuovo refresh risponde
NotFound.

### Test

`custommetricsrefresh` (nuova, 11 casi): metrica singola (solo lei, valore
nuovo, nessuna corsa extra subito dopo, l'altra metrica non toccata),
figlio di un gruppo (solo quel figlio), gruppo con discendenti annidati
(figlio disabilitato e figli di un gruppo annidato disabilitato mai
eseguiti, metrica fuori dal gruppo non toccata, gruppo annidato da solo),
bersagli disabilitati (metrica, gruppo, figlio di gruppo disabilitato,
figlio disabilitato; gruppo senza figli abilitati: nessuna esecuzione), id
sconosciuto, vuoto, snapshot, configurazione non valida e servizio fermo,
nessuna sovrapposizione (clic ogni 20 ms per 3 s contro un intervallo di
1 s, con l'helper che registra ogni sovrapposizione), gruppo con 4 figli e
10 clic (al massimo 4 processi), timeout, uscita 69, errore e ritorno a
Available, ricarica durante il refresh (presentazione: stessa corsa;
esecuzione: valore vecchio mai visto), rimozione durante il refresh,
icone della Dashboard (metrica, gruppo, icona propria del figlio, tasto
destro ignorato, card snapshot senza nulla di cliccabile, esiti). Inoltre:
`backendipc` (`refreshThroughTheApi`), `apischema` (`refreshResult`,
schema 7), `metricsbarpresenter` (clic centrale e "Refresh Now" solo sulle
voci Custom, clic sinistro invariato), `plasmoidlogic` (`refreshable`,
`refreshSummary`, `metricId` delle celle), `plasmoiddelegate` (nuovo:
`MetricDelegate` reale con qmltestrunner; registrato solo se i moduli QML
di Plasma sono installati), `plasmoidclient` (`refreshCustomMetric` contro
il backend reale), `plasmoidpackage` (metodo consentito, `RefreshArea`).
Nessun test esegue gli script reali dell'utente: solo
`custommetric_test_helper` in cartelle temporanee.

Build pulita in una cartella nuova: 0 warning; ctest **57/57** (nuove
`custommetricsrefresh` e `plasmoiddelegate`); `packaging-versions` 2.1.7;
`git diff --check` pulito; nessuno script shell modificato.

Da verificare nella sessione reale (non eseguito qui): clic sulle icone
nella Dashboard del tray installato, clic centrale e "Refresh Now" sulle
voci della Metrics Bar (dipende dall'host SNI: Plasma invia il clic centrale
come SecondaryActivate), clic sulle icone nel popup del widget dopo il
riavvio di plasmashell.
