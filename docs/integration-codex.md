# AQTRunCat — F3 Codex Integration

F3 aggiunge Codex come seconda **integrazione ufficiale**, con la stessa
architettura di Claude Code (F2): un adattatore separato
(`aqtruncat-codex-metric`) produce uno snapshot F1; il core di AQTRunCat non
conosce alcun formato Codex.

```text
Codex (sola lettura)
    │  ~/.codex/sessions/**/rollout-*.jsonl  (eventi token_count → rate_limits)
    │  opzionale: codex app-server → account/rateLimits/read  (ogni 5 min)
    ▼
aqtruncat-codex-metric snapshot [--live]   ← adattatore (QtCore)
    │  stdout: snapshot JSON M6 · exit 0 / 69 / 1
    ▼
CustomMetricsService (F1)                  ← stesso scheduler, timeout, process group
    ├── Dashboard › Integrations   (una barra per finestra)
    ├── Metrics Bar                 (cx5h, cx7d…, a scelta)
    └── Settings › Integrations     (esegue enable/disable/live-updates/status)
```

## 1. Indagine

Ambiente: Codex CLI 0.163.0 (`/usr/bin/codex`), account ChatGPT Plus,
`~/.codex` con 80 file di sessione (versioni da 0.131 a 0.163), Arch Linux,
Plasma 6 Wayland. Ispezionati solo nomi di campo, tipi e i valori numerici
d'uso; `auth.json` non è stato letto.

| Sorgente | Contiene i limiti d'uso? | Aggiornamento | Documentata | Costo / effetti | Esito |
|---|---|---|---|---|---|
| `sessions/YYYY/MM/DD/rollout-*.jsonl` (e `archived_sessions/`): eventi `event_msg` / `token_count` | **sì**: `rate_limits` con `limit_id`, `primary`/`secondary` = `{used_percent, window_minutes, resets_at}` (epoch s; versioni vecchie `resets_in_seconds`), `plan_type`, `rate_limit_reached_type` | a ogni risposta, **solo mentre si usa Codex** (qui l'ultimo dato era del 13/10: oggi entrambe le finestre risultavano già resettate) | no (formato interno, stabile tra 0.131 e 0.163) | lettura della coda dei file più recenti | **scelta**, predefinita |
| `codex app-server` (stdio JSON-RPC) → `initialize` → `account/rateLimits/read` | **sì**: `rateLimits` / `rateLimitsByLimitId["codex"]` con `primary`/`secondary` = `{usedPercent, windowDurationMins, resetsAt}`, `planType`, `rateLimitReachedType`, `ordinaryUsageAllowed`, `accountId` | a richiesta, sempre attuale | protocollo generato da `codex app-server generate-json-schema` (sperimentale); lo schema prevede esplicitamente i "background usage polls" (`excludeResetCreditDetails`) | avvia un processo Codex (~0,8 s, ~0,34 s CPU); **Codex** contatta OpenAI con il proprio login; nessun token consumato, nessun prompt | **scelta** come opzione esplicita (“live updates”) |
| `thread_history_1.sqlite`, `state_5.sqlite` | no (storia dei thread e metadati) | — | no | contenuto delle conversazioni | scartate |
| `logs_2.sqlite` | log interni | — | no | instabile, potenzialmente sensibile | scartata |
| `/status` (TUI) | sì | a richiesta | — | interattivo, scraping | scartata |
| `codex exec` | — | — | — | consumerebbe uso | scartata |
| `auth.json` + endpoint web | — | — | — | credenziali | scartata |

Osservazioni rilevanti per la semantica:

- i limiti sono **a livello di account** (stessi valori in tutte le sessioni
  contemporanee), raggruppati per `limit_id`: `codex` è la quota d'uso di
  Codex; esistono altri bucket (qui `premium`, con finestre nulle) che non
  sono questa quota e vengono ignorati; le versioni vecchie non scrivevano
  `limit_id` (trattato come `codex`);
- la durata delle finestre va letta da `window_minutes`: di solito
  `primary` = 300 (5 h) e `secondary` = 10080 (7 giorni), ma in 1 620 eventi
  `primary` era la finestra **settimanale** senza secondaria. Etichette e id
  derivano quindi dalla durata, mai dalla posizione;
- Codex riporta la percentuale **usata**; la rimanente è `100 − usata`.

Decisione (dell'utente, tra le opzioni proposte): **ibrida** come per Claude
— sessioni locali senza rete per impostazione predefinita, lettura live
facoltativa; per ogni finestra vince la segnalazione più recente.

## 2. Valori e significato

Card “Codex” sotto **Integrations**:

| Voce (id) | Esempio | Significato |
|---|---|---|
| `5h limit` (`window_300m`) | `0% used` + barra; *100% left · Resets at 23:21* | quota d'uso di Codex della finestra di 5 ore, percentuale usata e rimanente |
| `Weekly limit` (`window_10080m`) | `1% used` + barra; *99% left · Resets Oct 27, 10:08* | quota della finestra di 7 giorni |
| altre durate | `Daily limit`, `2-day limit`, `3h limit`, `90-minute limit` | se Codex ne riporta |
| Usage data (`source`) | `live, Codex app-server` · `Codex sessions`; *ChatGPT plus plan* | sorgente usata, piano, limite raggiunto, errori |

- Le finestre mostrate sono quelle riportate (ordinate per durata); una
  finestra mai riportata non compare.
- ≥ 100%: `100% used — limit reached`; `rate_limit_reached_type` e
  `ordinaryUsageAllowed: false` compaiono nella voce *Usage data*.
- Metrics Bar: valore arrotondato (`<1%`), didascalia `cx5h` / `cx7d`
  (distinta dai `5h` / `7d` di Claude); nulla viene aggiunto alla barra
  automaticamente.
- Formattazione, reset nel fuso locale, freschezza: modulo condiviso
  `src/providers/common/quota.*` (lo stesso di Claude).

Non è: la finestra di contesto, il conteggio dei token di sessione, il
consumo API a pagamento, i crediti.

## 3. Freschezza, reset, stati

| Situazione | Presentazione |
|---|---|
| segnalazione ≤ 10 minuti (le letture live sono ogni 5) | valore, rimanente, reset |
| > 10 minuti | `· as of HH:MM` |
| `resets_at` passato | ⚠ `Out of date` — *The window reset at …; no newer report yet* (mai 0%) |
| > 24 ore | ⚠ `Out of date` — *The last report (…) is too old to show* |
| Codex assente (né `~/.codex` né `codex` nel PATH) | Unavailable (69) |
| mai usato, nessuna segnalazione | Unavailable (69) con l'indicazione di usare Codex o attivare i live updates |
| file di sessione illeggibile e nessun dato | Error (1) |
| live attivo, lettura fallita (es. non autenticato) e nessun dato | Error (1) con il messaggio di Codex |
| live fallito ma dati presenti | ultimi valori + *live update failed: …* |
| refresh in corso | resta l'ultimo stato concluso (F1) |

Live updates: una lettura riuscita vale 5 minuti; una fallita viene
ritentata dopo 60 s (non a ogni esecuzione). Lo stato è in
`$XDG_RUNTIME_DIR/aqtruncat/codex-rate-limits.json` (cartella 0700, file
0600): solo percentuali, durate, reset, ore, piano e la `CODEX_HOME` a cui
si riferisce — **mai** l'`accountId`.

## 4. Sessioni multiple e account

- Più sessioni (anche contemporanee, anche concluse): vale l'evento più
  recente per data di modifica del file e poi per `timestamp`; una sessione
  vecchia ripresa (`codex resume`) scrive nel suo file originale e viene
  comunque considerata. Nessuna sessione “attiva” da scegliere: la quota è
  dell'account.
- Al massimo gli 8 file più recenti, la coda di ciascuno (256 KiB fino a
  8 MiB), al massimo 20 000 file elencati.
- Account diversi: i dati live non vengono usati se la `CODEX_HOME` non
  coincide. Limite: i file di sessione non riportano l'account; se si cambia
  account sulla stessa `CODEX_HOME`, i dati locali di prima restano visibili
  (con *as of*) finché il nuovo account non produce una segnalazione o i
  live updates non ne leggono una più recente.
- Modelli: i limiti di Codex non sono per modello nei dati osservati; i
  bucket diversi da `codex` sono ignorati.

## 5. Architettura

- `src/providers/common/` — libreria `aqtruncat_provider_common` (QtCore):
  `quota.*` (giudizio di freschezza/reset, formato delle voci, spostati da
  F2 senza cambiarne l'output) e `package.*` (cartella del package,
  lettura/scrittura atomica, opzioni); usata da Claude e Codex.
- `src/providers/codex/codexusage.*` — libreria `aqtruncat_codex`: lettura
  locale, client app-server (`QProcess`, processo in un proprio gruppo,
  fermato con chiusura dello stdin, poi TERM/KILL del gruppo se non
  risponde entro 20 s), stato live, `collect()`.
- `src/providers/codex/main.cpp` — `aqtruncat-codex-metric`: `snapshot
  [--live] [--codex-home DIR]`, `enable [--live] [--codex-home DIR]
  [--force]`, `disable`, `upgrade`, `status [--json]`, `live-updates on|off`.
- `src/providers/codex/codexicon.h` — icona originale e neutra (parentesi di
  codice in un anello indicatore, verde), non imita loghi OpenAI.
- Package: `$XDG_DATA_HOME/AQTRunCat/metrics/codex/metric.json` con
  `"integration": "codex"` (provenienza F2 → sezione Integrations),
  `output: "snapshot"`, intervallo 15 s, timeout 30 s.
- Nessun nuovo scheduler o motore; nessun collegamento dal core.

## 6. Settings, CLI, Dashboard, Metrics Bar

- Settings › Integrations: pannello **Codex** sotto Claude Code (separatore),
  *Show Codex usage*, stato (riusa la stessa funzione di Claude), *Live
  updates from Codex every 5 minutes* con **finestra di conferma** che spiega
  cosa succede (rete tramite Codex, nessun token, nessuna modifica a Codex),
  nota con l'ultimo report o l'ultimo errore. Claude e Codex si attivano e
  disattivano in modo indipendente.
- La configurazione è il package: GUI e CLI eseguono gli stessi comandi
  dell'adattatore e restano sincronizzate (la GUI osserva il package).
  `live-updates` modifica solo il package di AQTRunCat, mai Codex.
- Dashboard: card sotto Integrations accanto a Claude, una barra per
  finestra con nome accessibile; nessun duplicato sotto Custom Metrics,
  anche se un file utente ha titolo “Codex”.
- Metrics Bar: voci `custom-item:window_300m:<package>` ecc.; etichette
  `Codex — 5h limit`.

## 7. Installazione e pacchetti

- CMake installa `aqtruncat-codex-metric` in `bin/`; quindi `install.sh`,
  `install.sh --local`, PKGBUILD e Debian (che usano `cmake --install`) lo
  includono automaticamente; RPM: `%files` aggiornato.
- `install.sh` esegue `upgrade` per **entrambe** le integrazioni dopo
  l'installazione (come utente reale; non crea nulla, non tocca package
  dell'utente, un errore è solo un avviso); tutte le garanzie F2 restano
  (chiusura dell'istanza, `--dry-run`, `--no-build`, sudo con riprova).
- Codex resta una dipendenza facoltativa a runtime.

## 8. Privacy e sicurezza

- Dalle sessioni si interpretano solo le righe che contengono
  `"token_count"` e `"rate_limits"`, e di queste solo i campi delle quote e
  il `timestamp`; prompt e risposte non vengono mai interpretati né copiati.
- `auth.json` non viene mai aperto. In modalità live le credenziali le usa
  Codex stesso; AQTRunCat invia solo `initialize`, `initialized` e
  `account/rateLimits/read` (verificato nei test con un finto app-server).
- Nessun `sudo`/`pkexec`, nessuna shell implicita.
- Verificato sul sistema reale: una lettura live non modifica
  `config.toml`, `auth.json` né gli altri file JSON/JSONL di primo livello
  di `~/.codex` (hash e date). I database SQLite interni di Codex (log) non
  sono stati controllati: l'app-server può scrivervi i propri log.

## 9. Test

40 suite CTest, build pulita senza warning, 3 esecuzioni consecutive
complete superate.

- `tst_codexusage` (17): sessione con entrambe le finestre, rimanente,
  reset nel fuso (incluso il cambio d'ora del 25/10), bucket `premium` ed
  eventi senza finestre ignorati, riga troncata, piano solo settimanale non
  scambiato per 5 h, formati vecchi (`resets_in_seconds`, senza
  `limit_id`), sessione vecchia ripresa e `archived_sessions`, freschezza /
  reset / 24 h, limite raggiunto, Codex assente / mai usato / file
  illeggibile, parsing della risposta live (bucket multipli, solo
  `premium`, nessuna finestra), stato live privato (0600, senza account) e
  legato alla `CODEX_HOME`, priorità live/locale per finestra, protocollo
  app-server (solo i tre metodi), errori live con ultimo valore conservato e
  ritentativi a 60 s, Codex che non risponde (gruppo di processi fermato,
  nessun figlio rimasto — controllo verificato togliendo la protezione),
  etichette.
- `tst_codexintegration` (6): eseguibile reale via F1 accanto a Claude →
  Dashboard › Integrations (due card, barre con nomi accessibili, lo
  snapshot utente “Codex” resta in Custom Metrics), Metrics Bar (`cx5h`
  vs `5h`, nulla aggiunto da solo, aggiornamento); Settings: attivazione da
  GUI indipendente da Claude, nessuna lettura live senza consenso, live
  updates da GUI con conferma (finto app-server), CLI → GUI, account id mai
  salvato; politica del package, `upgrade`, `status`; riga di comando.
- Claude invariato dopo lo spostamento nel modulo comune (`tst_claudeusage`,
  `tst_claudecodemetric`, `tst_claudestatusline`, `tst_claudecodeintegration`).
- `installer` (198 controlli): adattatore Codex installato e package Codex
  vecchio aggiornato mantenendo `--live`; inoltre resi robusti i controlli
  dipendenti dai tempi (attesa che l'istanza di prova esegua il binario e
  gestisca SIGTERM) e quello di `--no-build` (prima porta aggiornata la
  build), che spiegano i fallimenti intermittenti visti in precedenza.

## 10. Validazione su Plasma 6 Wayland (2026-10-20)

Istanza isolata (`XDG_CONFIG_HOME`/`XDG_DATA_HOME` temporanei,
accessibilità attiva, `run --fps 10`) accanto a quella dell'utente, che non
è stata toccata; dati reali di Codex e Claude in sola lettura.

- **Attivazione da GUI**: le caselle *Show Codex usage* e *Show Claude Code
  usage* premute via AT-SPI; *Live updates from Codex…* abilitata solo dopo
  l'attivazione; la finestra di conferma *Turn On Live Updates* (modale) è
  comparsa ed è stata confermata dall'utente sullo schermo.
- **Valori**: live 18:21 → `5h limit 0% used · 100% left · Resets at
  23:21`, `Weekly limit 1% used · 99% left · Resets Oct 27, 10:08`, piano
  plus. L'utente ha confrontato con `/status` di Codex nello stesso momento
  e li ha giudicati corrispondenti (confronto fatto da lui a vista; i
  valori di `/status` non sono stati trascritti). I dati locali da soli
  davano correttamente valori non correnti (finestre del 13/10 già resettate).
- **Dashboard**: Claude Code e Codex sotto Integrations, nessuna sezione
  Custom Metrics, nessun duplicato
  ([integration-codex/dashboard-integrations.png](integration-codex/dashboard-integrations.png)).
- **Metrics Bar**: pixmap SNI `cx5h 0%`, `cx7d 1%`, `5h —` (Claude
  sconosciuto) accanto al runner
  ([integration-codex/metrics-bar.png](integration-codex/metrics-bar.png));
  tooltip `Codex — 5h limit: 0% used — 100% left · Resets at 23:21`.
- **Nessuno sfarfallio**: 163 letture AT-SPI delle etichette della Dashboard
  in 45 s (≥ 3 refresh Codex, ≥ 4 Claude): valori delle quote sempre
  identici, mai *Waiting…*.
- **Costo** (finestre chiuse, 60 s): app 0,45% di un core, adattatori 0,22%
  in totale; snapshot locale ~13 ms di CPU; una lettura live ~0,34 s di CPU
  (0,8 s reali) ogni 5 minuti (~0,1% medio); RSS 104 MiB (Settings e
  Dashboard già create, accessibilità attiva).
- **Chiusura**: SIGTERM pulito, nessun processo dell'adattatore o
  `codex app-server` lasciato dall'istanza; file di stato live rimosso.

## 11. Avviso quando i dati non sono correnti (2026-10-20)

Su segnalazione dell'utente (“se i live updates non sono attivi sia Claude
che Codex dicono *Unknown*, che non è intuitivo”):

- voce generica facoltativa `"warning": true` negli snapshot (F1/M6,
  retrocompatibile; un valore non booleano è un errore): la Dashboard mostra
  l'icona di avviso del tema (`dialog-warning`, con quella dello stile come
  ripiego) accanto al titolo, con il dettaglio come tooltip;
- finestre non correnti: `Out of date` (non più `Unknown`) con il motivo, e
  `warning`; finestre mai riportate: `Not reported`, con `warning`;
- live updates **spenti** e almeno una finestra non corrente: la voce *Usage
  data* è un avviso che spiega quando cambiano i valori (Codex: mentre lo si
  usa; Claude Code: all'apertura di `/usage`) e dove attivare i live updates
  (*Settings › Integrations*). Con i live updates attivi, o con tutto
  corrente, nessun avviso su *Usage data*;
- Claude Code: l'adattatore sa se i suoi live updates sono attivi leggendo
  lo stato della status line (`bridgeStatus`) a ogni snapshot.
- Plasma: verificato con Codex senza live updates
  ([integration-codex/dashboard-warning.png](integration-codex/dashboard-warning.png)).
- Test: `tst_custommetric` (campo e tipo), `tst_dashboard` (icona solo sulle
  voci con avviso, tooltip), `tst_claudeusage` e `tst_codexusage` (testi,
  `warning`, avviso solo a live spenti, nessun avviso con tutto corrente).

Stabilità della suite (stessa occasione): la causa dei fallimenti
intermittenti era il test dell'installer, che compila (`--build-dir`) la
stessa cartella di build mentre ctest esegue in parallelo gli altri test;
dopo una ricompilazione incrementale ninja 1.13 può voler ricollegare
librerie ed eseguibili per più passate, sostituendo binari in uso (“Not
Run”, adattatori momentaneamente assenti). Ora il test `installer` è
`RUN_SERIAL`, porta la build a “no work to do” prima di provare
`--no-build`, e i timeout dei test di integrazione sono di 15 s: 5 cicli
“ricompila poi testa” consecutivi superati (prima fallivano).

## 12. Limiti noti

- Senza live updates i valori cambiano solo mentre si usa Codex; dopo il
  reset di una finestra non si sa nulla finché Codex non risponde di nuovo.
- I live updates usano un protocollo **sperimentale** di Codex
  (`app-server`): una versione futura può cambiarlo (la card mostrerà
  l'errore, i dati locali restano).
- Account multipli sulla stessa `CODEX_HOME`: vedi §4.
- Il formato dei file di sessione è interno.
- La finestra di contesto della sessione (metrica secondaria) non è
  implementata per Codex: non richiesta, rinviata.
- Lettura live: il processo `codex app-server` può scrivere nei propri log
  interni.
