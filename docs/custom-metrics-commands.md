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
