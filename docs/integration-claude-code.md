# AQTRunCat — F2 Claude Code Metric

F2 rende Claude Code un'**integrazione ufficiale** di AQTRunCat, costruita
sopra le Custom Metrics v2 di F1. La logica specifica di Claude Code vive in
un adattatore separato (`aqtruncat-claude-metric`); il core di AQTRunCat non
conosce alcun formato Claude e continua a eseguire soltanto comandi F1.

Il documento ha due parti:

- **Parte A — Revisione del 2026-10-20 (comportamento attuale)**: integrazione
  in Settings, quote d'uso del piano (finestra di 5 ore e settimana) come
  valori principali, aggiornamenti live opzionali, chiusura dell'istanza in
  esecuzione da parte di `install.sh`.
- **Parte B — Prima implementazione (2026-10-16)**: indagine e metrica della
  finestra di contesto, oggi **metrica secondaria opzionale** (`--context`).
  È lasciata com'era, con i risultati osservati allora; dove il
  comportamento è cambiato lo dice la Parte A.

```text
Claude Code (stato locale, sola lettura)
    │  ~/.claude.json  cachedUsageUtilization  (cache di /usage)
    │  $XDG_RUNTIME_DIR/aqtruncat/claude-rate-limits.json  (solo con live updates)
    │  ~/.claude/sessions, ~/.claude/projects  (solo con --context)
    ▼
aqtruncat-claude-metric snapshot      ← adattatore (QtCore, nessuna rete)
    │  stdout: snapshot JSON M6 · exit 0 / 69 / 1
    ▼
CustomMetricsService (F1)             ← stesso scheduler, timeout, process group
    ├── Dashboard  (due barre: 5h e settimana)
    ├── Metrics Bar (una voce per quota, a scelta)
    └── Settings › Integrations  ── esegue enable/disable/status/live-updates
                                    dell'adattatore (stessa configurazione della CLI)
```

---

# Parte A — Revisione del 2026-10-20

## A.1 Richiesta

La prima implementazione mostrava come valore principale l'occupazione della
finestra di contesto (`Context window: 18.8% of 1M`). L'utente vuole invece
le due quote mostrate dalla schermata `/usage` di Claude Code:

- **Current session** — finestra d'uso mobile di 5 ore: percentuale usata,
  orario di reset;
- **Current week (all models)** — finestra di 7 giorni: percentuale usata,
  data e ora di reset;

ciascuna con la propria barra nella Dashboard, oltre a: gestione da Settings
(sezione **Integrations**, distinta dalle Custom Metrics), CLI mantenuta e
sincronizzata, nessun valore inventato, e un `install.sh` che chiude in
modo sicuro l'AQTRunCat in esecuzione prima di sostituirlo.

## A.2 Indagine sulle sorgenti delle quote

Ambiente: Claude Code 2.1.304 (nativo, `~/.local/bin/claude`), account
claude.ai, Arch Linux, Plasma 6 Wayland. Ispezionati solo nomi di campo e
valori numerici d'uso; `~/.claude/.credentials.json` **non** è stato letto.

| Sorgente | Contiene le quote di `/usage`? | Aggiornamento | Documentata | Costo / effetti | Esito |
|---|---|---|---|---|---|
| `~/.claude.json` → `cachedUsageUtilization` | **sì**: `utilization.five_hour` e `.seven_day` con `utilization` (%), `resets_at` (ISO 8601 UTC), `fetchedAtMs`, `accountUuid`; anche `limits[]` (`session`, `weekly_all`) con gli stessi valori | solo quando `/usage` (o una vista d'uso dell'IDE) interroga il servizio: nei backup automatici `~/.claude/backups/.claude.json.backup.*` il valore è rimasto quello delle 08:37 fino all'apertura di `/usage` delle 10:28 | no (interna) | lettura di un file locale (~130 KB) | **scelta** come sorgente di base, senza modificare nulla |
| JSON della status line → `rate_limits.five_hour/seven_day.{used_percentage, resets_at}` | **sì**, stessi concetti (“Percentage of the 5-hour or 7-day rate limit consumed”) | dopo ogni risposta dell'API, in ogni sessione; solo abbonati Pro/Max; ogni finestra può mancare; Claude Code la toglie quando passa `resets_at` | **sì** (code.claude.com/docs/en/statusline) | richiede `statusLine` in `settings.json` | **scelta** come opzione esplicita (“live updates”) |
| endpoint d'uso del piano (`/usage` stesso) | sì | a richiesta | no | richiede il token OAuth di `.credentials.json` e la rete | **scartata**: segreti e rete |
| `claude -p "/usage"` o simili | — | — | — | avvia una sessione, può consumare token | **scartata** |
| scraping del terminale | — | — | — | fragile | **scartata** (esiste un'interfaccia strutturata) |
| `stats-cache.json`, `history.jsonl`, trascrizioni | no (token e statistiche, non quote) | — | no | — | **scartate**: le quote non si derivano dai token |
| OpenTelemetry | metriche di consumo, non le quote del piano | push | sì | configurazione e collector dell'utente | **scartata** |

Verifica di corrispondenza: alle 10:28 l'utente ha aperto `/usage`
(“Current session 3% used, Resets 13:30”; “Current week (all models) 7%
used, Resets Oct 23, 20:00”); la cache conteneva in quell'istante
`five_hour.utilization = 3`, `resets_at = 11:30Z` (13:30 CEST),
`seven_day.utilization = 7`, `resets_at = 2026-10-23T18:00Z` (20:00 CEST).
Il confronto dal vivo è in §A.12.

Decisione (dell'utente, tra le opzioni proposte): **ibrida** — la cache di
`/usage` funziona subito e senza toccare Claude Code; gli aggiornamenti live
tramite status line sono un'opzione esplicita; per ogni finestra vince la
segnalazione più recente.

## A.3 Valori e significato esatto

Card “Claude Code” (titolo e icona del package):

| Voce (id) | Esempio | Significato |
|---|---|---|
| Current session (5h) (`five_hour`) | `31% used` + barra; *Resets at 13:30* | percentuale usata della finestra d'uso **mobile di 5 ore** del piano, come la riporta Claude Code; non è l'uso di un singolo terminale né la durata di una sessione |
| Current week (all models) (`seven_day`) | `11% used` + barra; *Resets Oct 23, 20:00* | percentuale usata della finestra di **7 giorni, tutti i modelli** |
| Usage data (`source`) | `Claude Code /usage` · `live, Claude Code status line` | da quale sorgente vengono i valori mostrati; eventuale errore di una sorgente come riga secondaria |
| Context window (`context`), solo con `--context` | `27.6% of 1M (275.8k tokens)` | metrica secondaria della Parte B; mai usata per le quote |

- La percentuale residua è implicitamente 100 − usata; non viene mostrata
  come numero separato per non duplicare il valore (la barra la rende
  visibile).
- Valori ≥ 100: `100% used — limit reached` (la barra della Dashboard è
  piena; il testo non viene limitato, es. `104% used`).
- Il reset è convertito nel fuso orario locale: oggi `Resets at HH:MM`,
  altri giorni `Resets MMM d, HH:MM` (mese in inglese come il resto della
  UI, ore nel formato 24 h). Senza reset nei dati: *Reset time not reported*.
- `normalizedValue` = percentuale / 100 (barra), `metricsBarValue` = valore
  arrotondato (`<1%` tra 0 e 1), `metricsBarLabel` = `5h` / `7d`.
- `lastUpdatedDate` della card = istante della segnalazione più recente
  mostrata (non l'ora di lettura); `metricsBarValue` della sorgente = quota
  5h (compatibilità con chi aveva scelto la sorgente intera).

## A.4 Freschezza, reset e stati

| Situazione | Presentazione |
|---|---|
| segnalazione ≤ 5 minuti fa | valore e reset |
| segnalazione > 5 minuti fa | valore, reset e `· as of HH:MM` (con data se di un altro giorno) |
| la finestra è stata resettata dopo la segnalazione (`resets_at` passato) | ⚠ `Out of date` — *The window reset at 13:30; no newer report yet*; nessuna barra, nessun valore nella Metrics Bar (prima del 2026-10-20 il testo era `Unknown`; vedi F3 doc §11) |
| segnalazione più vecchia di 24 ore (solo la settimanale può arrivarci) | ⚠ `Out of date` — *The last report (…) is too old to show* |
| live updates spenti e una finestra non corrente | ⚠ *Usage data*: *Live updates are off: values change only when you open /usage in Claude Code. Turn them on in AQTRunCat Settings › Integrations.* |
| una sola finestra presente (piano senza 5h, o dato parziale) | l'altra `Not reported` con l'indicazione di aprire `/usage` o attivare i live updates |
| nessuna segnalazione di nessuna finestra | Unavailable (exit 69): *No Claude Code usage report yet: open /usage …* |
| cache di un altro account / Claude Code non autenticato | ignorata, con il motivo nel messaggio |
| Claude Code assente (né `~/.claude` né `~/.claude.json`) | Unavailable: *Claude Code not found …* |
| cache illeggibile o formato non riconosciuto (dopo un secondo tentativo a 100 ms per le scritture concorrenti) | Error (exit 1); se l'altra sorgente ha dati, si mostrano quelli con l'errore nella voce *Usage data* |
| reset cambiato | si mostra sempre il `resets_at` della segnalazione scelta |
| refresh in corso | resta l'ultimo stato concluso (anti-flicker F1) |

Mai 0% al posto di un dato mancante; mai un valore vecchio presentato come
attuale.

## A.5 Aggiornamenti live (status line, opzionali)

Attivazione: casella *Live updates through Claude Code's status line* in
Settings (con finestra di conferma che dice quale file viene modificato) o
`aqtruncat-claude-metric live-updates on`.

- Viene modificato **solo** il membro `statusLine` del `settings.json`
  utente di Claude Code (`$CLAUDE_CONFIG_DIR` o `~/.claude`). La modifica è
  testuale: il resto del file resta identico byte per byte (formattazione,
  ordine delle chiavi). Prima di scrivere si verifica che il risultato,
  interpretato, sia l'originale con il solo `statusLine` cambiato; un file
  che non è un oggetto JSON stretto (commenti, chiavi duplicate…) viene
  rifiutato senza toccarlo. Un link simbolico viene seguito (si scrive il
  file di destinazione).
- Nuovo valore: `{"type":"command","command":"'<percorso>/aqtruncat-claude-metric' statusline"}`,
  conservando `padding` e `refreshInterval` della status line precedente.
- Lo `statusLine` precedente (valore e testo esatto) viene salvato in
  `$XDG_DATA_HOME/AQTRunCat/integrations/claude-code/statusline-backup.json`
  (0600) **prima** di modificare `settings.json`.
- `aqtruncat-claude-metric statusline` (eseguito da Claude Code): legge
  l'input, salva solo `rate_limits.five_hour/seven_day` (percentuale, reset,
  ora di cattura per finestra) in
  `$XDG_RUNTIME_DIR/aqtruncat/claude-rate-limits.json` (cartella 0700, file
  0600, insieme alla directory Claude Code per cui vale), poi, se c'era una
  status line, **si sostituisce** con `/bin/sh -c <comando precedente>`
  passandogli lo stesso identico input (tramite un memfd): la status line
  dell'utente continua a funzionare e la cancellazione di Claude Code la
  raggiunge direttamente. Senza status line precedente non stampa nulla.
  Un input senza `rate_limits` (utenti con API key, prima risposta) non
  cancella i valori salvati. Non fallisce mai.
- `live-updates off` e `disable` rimettono lo `statusLine` precedente
  esattamente com'era (o tolgono il membro; se il file era stato creato da
  AQTRunCat e resta vuoto, lo rimuovono). Se nel frattempo l'utente ha
  cambiato `statusLine`, il file non viene toccato e il backup viene
  eliminato.
- Ripristino manuale: il backup contiene `statusLine` e `statusLineText`;
  basta rimettere quel valore in `settings.json`.
- Effetto visibile in Claude Code: con una status line configurata Claude
  Code mostra meno suggerimenti da tastiera nel footer (documentato da
  Claude Code); per questo è opzionale.

## A.6 Integrazione in Settings e proprietà della configurazione

- Sezione **Integrations** (separata da *Custom Metrics*): nome, descrizione,
  *Show Claude Code usage*, stato (*Off.*, *On — available.*, *On —
  unavailable: …*, *On — error: …*, adattatore non installato, package
  dell'utente con lo stesso id), *Live updates…* con una nota sullo stato
  (ultimo report, status line concatenata, file modificato), *Also show the
  context window…* e *Only sessions in:* (abilitato solo con il contesto).
- `ProviderIntegration` (`src/app/providerintegration.*`, generico per le
  integrazioni future) esegue in modo asincrono e uno alla volta i comandi
  dell'adattatore: `status --json` (stato), `enable [--context [--cwd DIR]]`,
  `disable`, `live-updates on|off`. **La configurazione appartiene
  all'adattatore** (package + backup): Settings e CLI leggono e scrivono la
  stessa cosa. Settings osserva i file indicati da `status` (metric.json, la
  sua cartella, la cartella `metrics`, `settings.json` di Claude Code) e si
  aggiorna da solo dopo un comando dalla CLI.
- Il package dell'integrazione non compare più nell'elenco Custom Metrics;
  `enable` non sovrascrive mai un `claude-code/metric.json` scritto
  dall'utente (Settings lo segnala e disabilita la casella).
- L'adattatore viene cercato accanto all'eseguibile `aqtruncat` in
  esecuzione, poi nel `PATH`.
- Nessuna nuova dipendenza; nessun motore di metriche aggiuntivo: i valori
  passano solo da `CustomMetricsService`.

## A.7 Estensioni generiche (F1/M6)

Indipendenti da Claude, retrocompatibili, coperte da `tst_custommetric`,
`tst_metricsbarmodel`, `tst_appsettings`:

- voci dello snapshot: `id`, `detail`, `metricsBarValue`, `metricsBarLabel`
  facoltativi (RunCat Neo ignora le chiavi sconosciute). La Dashboard mostra
  `detail` sotto la barra della voce; ogni barra ha come nome accessibile il
  titolo della voce.
- Metrics Bar: id `custom-item:<id voce>:<id sorgente>`; la voce mostra la
  sua didascalia (`5h`) al posto dell'icona della sorgente, tooltip
  `Claude Code — Current session (5h): 31% used — Resets at 13:30`. Una voce
  senza valore compatto è Unavailable (`—`), una voce sparita dallo snapshot
  pure; la selezione resta salvata. Settings elenca le voci selezionabili di
  una sorgente al posto della sorgente intera. Limite di 3 voci invariato.

## A.8 Installer: chiusura dell'istanza in esecuzione

Implementato in `install.sh` (documentato in `--help` e nel README):

1. **Chi**: solo processi dell'utente reale (quello di `sudo`, se usato) il
   cui `/proc/<pid>/exe` è esattamente `BINDIR/aqtruncat` di questa
   installazione (anche “(deleted)” dopo una sostituzione). Il nome del
   processo serve solo a scartare velocemente gli altri; nessuna decisione
   per nome. Claude Code, Codex, altri programmi e un AQTRunCat avviato da
   un'altra posizione (build, altra installazione) non vengono mai toccati.
2. **Come**: SIGTERM (stessa uscita pulita di *Quit AQTRunCat*), attesa fino
   a 10 s, verifica dell'uscita confrontando lo starttime (PID riusato) e
   considerando uscito uno zombie.
3. **Se non esce**: errore esplicito, *nothing was changed*, nessun SIGKILL.
4. **Prima**: build, staging, controllo manifest e `sudo -v` avvengono prima
   della chiusura; un errore successivo (copia) stampa che AQTRunCat era
   stato chiuso e come riavviarlo.
5. **Dopo**: nessun riavvio automatico; il messaggio finale lo dice.
6. `--dry-run`: elenca il processo che verrebbe chiuso senza toccarlo.
7. `--uninstall`: stessa chiusura prima di rimuovere i file.
8. `--local` e installazione di sistema: identico; con `sudo` le istanze di
   altri utenti vengono solo segnalate (senza root non sono visibili).
9. Nessuna escalation di privilegi aggiuntiva.

## A.9 Privacy e compatibilità

- Da `~/.claude.json` si usano solo `cachedUsageUtilization` e
  `oauthAccount.accountUuid` (per scartare la cache di un altro account); il
  file viene letto in memoria ma nulla d'altro (email, progetti) finisce
  nell'output. `.credentials.json` non viene mai aperto. Nessuna rete.
- Il file di stato live contiene solo percentuali, reset, ore di cattura e
  la directory di Claude Code.
- `cachedUsageUtilization` è un formato **interno** di Claude Code (2.1.304):
  se cambia, la card mostra Error o *Not reported* finché l'adattatore non
  viene aggiornato; i live updates usano invece l'interfaccia documentata.

## A.10 Limiti noti

- Senza live updates i valori cambiano solo quando si apre `/usage` (o la
  vista d'uso dell'IDE): tra un'apertura e l'altra la card dice *as of* e
  dopo il reset della finestra non mostra più il valore.
- Con live updates il dato arriva dopo una risposta di Claude Code; l'ora di
  cattura è quella della status line, non quella esatta della risposta.
- `rate_limits` esiste solo per abbonati Pro/Max (o dietro un gateway con
  spend limit, non gestito qui): con una API key restano *Not reported*.
- Le status line di progetto (`.claude/settings.json` di un repository) o
  gestite dall'amministratore hanno la precedenza su quella utente: in quei
  progetti la cattura live non avviene (la cache resta).
- La status line concatenata viene eseguita con `/bin/sh -c`.
- Formato dell'ora fisso a 24 h; mese abbreviato in inglese.

## A.11 Test automatici

38 suite CTest (36 Qt + 2 Bash), build pulita senza warning:

- `tst_claudeusage` (19): cache come la scrive Claude Code (frazioni di
  secondo a 6 cifre), conversione nel fuso Europe/Rome, *as of*, finestra
  resettata, settimana troppo vecchia, finestra mancante/null/parziale,
  nessuna segnalazione (69), Claude Code assente, altro account / non
  autenticato, cache non supportata o non JSON (1), 100% e oltre, sorgente
  live più recente e viceversa, solo live con reset cambiato, cattura che
  salva solo le due finestre con permessi privati e non cancella i dati
  senza `rate_limits`, stato live di un'altra directory, formattazione;
  nessun dato privato (email, token, progetti, account) nell'output.
- `tst_claudestatusline` (12): modifica testuale (inserimento con
  l'indentazione del file, sostituzione, rimozione in mezzo/in fondo/unico
  membro, oggetti annidati, rifiuto di JSON non valido/chiavi duplicate),
  attivazione senza status line e ritorno ai byte originali, status line
  esistente concatenata e ripristinata byte per byte, altre impostazioni
  cambiate nel frattempo conservate, status line cambiata dall'utente
  lasciata stare, file assente creato e rimosso, file non valido mai
  toccato, link simbolico seguito, adattatore spostato.
- `tst_claudecodeintegration` (9): eseguibile reale via F1 → Dashboard (due
  barre distinte 30/70, dettagli di reset, nomi accessibili) e Metrics Bar
  (non selezionata per default; 5h e 7d selezionate separatamente con
  didascalie, aggiornate in modo indipendente dopo un nuovo `/usage`);
  Claude Code assente Unavailable e mai 0; `enable`/`disable`/`status` e
  politica di sovrascrittura; status line reale: `live-updates on`,
  `statusline` con l'input di Claude Code che esegue la status line
  dell'utente con lo stesso input, snapshot dai dati live, `disable` che
  ripristina `settings.json` byte per byte; **Settings**: attivazione da
  GUI, stato *On — available.*, package fuori dall'elenco Custom Metrics,
  Metrics Bar per voce, CLI → GUI (`enable --context` spunta la casella),
  live updates da GUI con conferma, persistenza in una nuova finestra,
  disattivazione da GUI che rimuove anche la status line; adattatore
  mancante; errori della riga di comando.
- `tst_claudecodemetric` (28): la metrica di contesto della Parte B, ora con
  `--context` (sessione assente o dati illeggibili riportati nella voce).
- Generici: `tst_custommetric` (+6), `tst_metricsbarmodel` (+1),
  `tst_appsettings` (+1).
- `installer` (Bash, 141 controlli, +22): istanza reale di questa
  installazione (`aqtruncat metrics --watch`) chiusa prima della copia e
  uscita con stato 0, nessun riavvio; `--dry-run` la elenca senza chiuderla;
  `--uninstall` la chiude; programmi esca con lo stesso nome o con
  “aqtruncat” negli argomenti intatti; istanza che ignora SIGTERM:
  installazione fallita, binario non sostituito, processo non ucciso;
  timeout non valido rifiutato.

## A.12 Validazione pratica (2026-10-20, Plasma 6 Wayland)

Istanza isolata (`XDG_CONFIG_HOME`/`XDG_DATA_HOME` temporanei, `run --fps
10`) accanto a quella dell'utente, che non è stata toccata; dati Claude Code
reali in sola lettura; integrazione attivata con l'adattatore della build e
Metrics Bar con le due quote:

- **Confronto con `/usage`**: alle 11:44 l'utente ha aperto `/usage`
  (*Current session 31% used, Resets 1:30pm (Europe/Rome)*; *Current week
  (all models) 11% used*); alle 11:45:10 l'adattatore riportava `31% used`
  *Resets at 13:30* e `11% used` *Resets Oct 23, 20:00* (cache scritta da
  quel `/usage` alle 11:44:30). Prima, alle 11:37, la Dashboard mostrava
  ancora `3%`/`7%` *as of 10:28*, cioè il `/usage` delle 10:28: esempio
  concreto del limite senza live updates.
- **Dashboard**: card Claude Code con le due barre distinte, i reset e *as
  of 10:28* (screenshot della sola finestra:
  [integration-claude-code/dashboard-usage.png](integration-claude-code/dashboard-usage.png)).
- **Metrics Bar**: due icone accanto al runner, pixmap SNI letti via D-Bus
  ([integration-claude-code/metrics-bar-usage.png](integration-claude-code/metrics-bar-usage.png)):
  `5h` sopra `3%`, `7d` sopra `7%`; tooltip `Claude Code — Current session
  (5h): 3% used — Resets at 13:30 · as of 10:28` e analogo per la settimana.
- **Settings**: sezione Integrations con *On — available.*; dopo
  `aqtruncat-claude-metric enable --context` dalla CLI la casella del
  contesto risultava spuntata nella finestra aperta (sincronizzazione CLI →
  GUI dal vivo).
- **Costo** (finestre chiuse, 30 s): app 0,50% di un core, adattatore
  ~0,1% (≈ 11 ms di CPU per esecuzione, ogni 10 s; 12 ms con `--context`);
  RSS 86 MiB dopo aver aperto Dashboard e Settings.
- **Chiusura**: SIGTERM, uscita pulita, nessun `aqtruncat-claude-metric`
  residuo.
- **Non verificato dal vivo**: i live updates sul `settings.json` reale
  dell'utente (non modificato senza consenso; percorso completo verificato
  con l'eseguibile reale in `tst_claudecodeintegration`) e il clic sulle
  caselle di Settings in Plasma (verificato offscreen nei test).

## A.13 Integrations nella Dashboard (rifinitura del 2026-10-20)

La Dashboard mostrava la card Claude Code sotto “Custom Metrics”, che è
fuorviante: è un'integrazione ufficiale, non una metrica dell'utente.

- **Provenienza esplicita**: campo facoltativo `"integration": "<id>"` di
  primo livello nella configurazione di un comando (stessa sintassi di
  `id`), scritto dall'adattatore (`"claude-code"`), letto in
  `CommandDefinition::integration` e riportato in `Source::integration`.
  Vuoto = Custom Metric dell'utente. Nessuna classificazione per titolo,
  icona o percorso; i file snapshot M6 non possono dichiararla (la chiave
  vi è ignorata). Esecuzione, scheduler, stati e Metrics Bar invariati.
- **Dashboard**: sezione **Integrations** prima di **Custom Metrics**, stesso
  renderer delle card, ogni sorgente una volta sola; una sezione vuota è
  nascosta con il suo titolo (non c'è più “No custom metrics”). Gli
  `objectName` delle card (`customSource_N`, …) proseguono tra le due
  sezioni.
- **Settings**: elenco Custom Metrics senza le sorgenti con provenienza;
  nella lista della Metrics Bar le voci dell'integrazione sono etichettate
  “Integration”.
- **Package precedenti** (scritti prima del campo): `status --json` riporta
  `integrationMarked: false`; finché non vengono riscritti compaiono sotto
  Custom Metrics nella Dashboard (in Settings sono già riconosciuti dal
  percorso). Alla prima apertura di Settings il package viene riscritto
  una volta con le sue opzioni (`enable [--context [--cwd]]`).
- **Aggiornamento all'installazione** (richiesto dopo una reinstallazione
  in cui la card restava sotto Custom Metrics): `install.sh`, dopo
  l'autostart, esegue come utente reale `aqtruncat-claude-metric upgrade`,
  che riscrive con le sue opzioni (`--context`, `--cwd`, `--config-dir`) un
  package dell'adattatore già abilitato, aggiorna il percorso
  dell'eseguibile e, se i live updates sono attivi, il comando della status
  line. Integrazione spenta → nulla; package dell'utente → intatto; nessun
  file viene cancellato (l'installer continua a non toccare
  `~/.local/share/AQTRunCat`); un errore è solo un avviso. Scartata
  l'alternativa di cancellare la cartella: spegnerebbe l'integrazione in
  silenzio e, con i live updates attivi, lascerebbe `settings.json` di
  Claude Code senza ripristino. Test: `tst_claudecodeintegration`
  (`upgrade` spento/vecchio package/package dell'utente) e `installer`
  (+5 controlli: provenienza, eseguibile installato, opzioni conservate,
  altri file e Custom Metrics intatti).
- Test: `tst_commandmetric` (campo assente/valido/non valido, definizione
  altrimenti identica), `tst_dashboard` (sezioni miste, ordine, nessun
  duplicato, titolo uguale, sezioni vuote nascoste, Dashboard nascosta),
  `tst_claudecodeintegration` (+2: provider, snapshot M6 legacy con lo
  stesso titolo e perfino la chiave `integration`, comando dell'utente con
  lo stesso titolo → solo il provider sotto Integrations; migrazione di un
  package senza campo da parte di Settings, opzioni conservate).
- Plasma 6 Wayland (istanza isolata): Integrations con la sola card Claude
  Code (36% / 12% *as of 12:34*), Custom Metrics con lo snapshot M6
  “Claude Code” di esempio e un comando “Temperature”
  ([integration-claude-code/dashboard-sections.png](integration-claude-code/dashboard-sections.png));
  dopo `disable` dalla CLI il titolo Integrations scompare. Suite completa
  38/38, build pulita senza warning.

## A.14 Modulo condiviso con F3 (2026-10-20)

Con l'integrazione Codex (F3, `docs/integration-codex.md`) il giudizio di
freschezza/reset e il formato delle voci quota sono stati spostati in
`src/providers/common/quota.*` e gli helper del package in
`src/providers/common/package.*` (libreria `aqtruncat_provider_common`).
L'output di Claude Code è invariato: tutti i test F2 passano senza
modifiche.

---

# Parte B — Prima implementazione (2026-10-16)

> Nota (2026-10-20): quanto segue descrive la prima versione, in cui la
> finestra di contesto era il valore principale. Oggi è la sezione
> secondaria attivata da `--context` / *Also show the context window*;
> senza sessione o con dati illeggibili lo dice nella sua voce invece di
> rendere l'intera metrica Unavailable/Error, e il suo valore compatto è
> sulla voce `context` (didascalia `ctx`). Le frasi su comandi, stati e
> attivazione sono superate dalla Parte A; i risultati osservati restano
> quelli di allora.

### B.1 Indagine

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

### B.2 Sorgente scelta

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

### B.3 Valori esposti e significato esatto

Una sola card “Claude Code”:

| Voce | Esempio | Significato |
|---|---|---|
| Context window | `21.7% of 1M (217.4k tokens)` + barra | **occupazione della finestra di contesto** della sessione selezionata dopo la sua ultima risposta: `input_tokens + cache_creation_input_tokens + cache_read_input_tokens` dell'ultima risposta della conversazione principale, diviso la finestra (200 000, o 1 000 000 se il modello richiesto termina con `[1m]`). Stessa formula di `context_window.used_percentage`. Non conta `output_tokens`. |
| Model | `claude-opus-5-5 · 1M context` | modello che ha prodotto l'ultima risposta; “· 1M context” quando la finestra è quella estesa |
| Session | `AQTRunCat · busy` | nome della directory di lavoro della sessione (non il percorso completo) e stato riportato da Claude Code (`busy`/`idle`) |
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

### B.4 Selezione della sessione

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

### B.5 Lettura della trascrizione

Non si scorre mai la storia completa. Si leggono gli ultimi 256 KiB; se non
contengono una risposta valida la finestra cresce ×4 fino a 8 MiB, poi
l'adattatore si ferma con Error. Si considerano solo le righe che contengono
letteralmente `"type":"assistant"` o `compact_boundary`; delle risposte si
leggono solo `isSidechain`, `message.model`, `message.usage`,
`requestedModel` e `timestamp`. Le risposte dei sub-agent (`isSidechain`) e
quelle `<synthetic>` (messaggi locali, non risposte dell'API) sono saltate.
La prima riga parziale di ogni finestra viene scartata.

### B.6 Architettura e integrazione F1

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

#### B.6.1 Estensioni generiche di F1

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

### B.7 Installazione e attivazione

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
`enable`. (Dalla 2.1.3 è un errore, “Executable not found”: vedi
`docs/custom-metrics-commands.md` §9.)

### B.8 Dashboard, Metrics Bar, Settings

- Dashboard: card “Claude Code” con icona, le quattro voci di §3 e la barra
  per il contesto. Durante il refresh resta visibile l'ultimo stato
  concluso (correzione anti-flicker di F1), “Waiting…” solo alla prima
  esecuzione.
- Metrics Bar: valore compatto `22%` sotto l'icona; il tooltip riporta
  `Claude Code: 22%` e l'ora dell'ultima risposta. Unavailable/Error usano
  la resa standard della barra.
- Settings: la sorgente appare come “Command” nell'elenco Custom Metrics,
  come ogni comando F1.

### B.9 Stati e guasti

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

### B.10 Privacy e sicurezza

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

### B.11 Compatibilità tra versioni di Claude Code

Registro e trascrizione sono formati **interni** di Claude Code (verificati
su 2.1.301). L'adattatore è difensivo: campi mancanti o di tipo inatteso
producono Error con un messaggio esplicito, mai valori inventati; `procStart`
e `requestedModel` assenti hanno un ripiego documentato (riga di comando;
solo token). Se una versione futura cambia questi formati, la card mostrerà
Error finché l'adattatore non viene aggiornato. La formula del contesto e la
dimensione della finestra seguono invece la documentazione ufficiale della
status line.

### B.12 Limiti noti e lavoro rinviato

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

### B.13 Test e validazione

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
