# AQTRunCat — M6 Custom Metrics

M6 implementa AQTRunCat come consumer di snapshot JSON prodotti da software
esterno. Non esegue comandi, script, polling HTTP, `/proc` o `/sys`; il
producer decide quando e come aggiornare il file. La milestone non implementa
la Metrics Bar M7.

## 1. Architettura

```text
producer esterno -> file JSON -> CustomMetricsService -> CustomMetricSnapshot
                                      |                     |          |
                         registry/watcher/retry          Dashboard   futura M7
```

`CustomMetricsService` è posseduto dall'applicazione tray e resta attivo anche
quando Dashboard e Settings sono nascoste. È separato dal `MetricSampler` M3:
`SystemMetricsSnapshot` conserva invariati CPU, memoria, rete, storage e I/O
disco fortemente tipizzati; `custommetrics::Snapshot` trasporta soltanto gli
snapshot esterni.

## 2. Formato compatibile RunCat Neo

Il parser accetta lo schema documentato dall'analisi upstream M0:

```json
{
  "title": "Claude Code",
  "symbol": "staroflife",
  "metricsBarValue": "5.4%",
  "metrics": [
    { "title": "Model", "formattedValue": "Opus 4.7" },
    { "title": "Context", "formattedValue": "5.4%", "normalizedValue": 0.054 }
  ],
  "lastUpdatedDate": "2026-06-05T04:50:40Z"
}
```

Campi obbligatori:

- `title`: stringa;
- `metrics`: array, anche vuoto;
- per ogni elemento: `title` e `formattedValue`, stringhe;
- `lastUpdatedDate`: timestamp ISO 8601 con data, ora, secondi e timezone.

Campi opzionali, accettati anche come `null`:

- `symbol`;
- `metricsBarValue`;
- `normalizedValue` numerico per ciascuna entry.

Le frazioni di secondo sono accettate come estensione compatibile. I campi
sconosciuti vengono ignorati. L'ordine di `metrics[]` è preservato. Come in
upstream, `normalizedValue` viene limitato a `[0,1]` soltanto al rendering.
`formattedValue` e `metricsBarValue` restano stringhe opache del producer:
AQTRunCat non prova a ricostruire valore raw, unità o tipo.

## 3. Limiti e validazione

Ogni file è letto al massimo fino a **1 MiB + 1 byte**; oltre 1 MiB viene
rifiutato senza caricarlo integralmente. Sono ammesse al massimo **256** entry
in `metrics[]` e **4096 caratteri UTF-16** per ogni stringa conosciuta. JSON
vuoto, malformato, top-level non object, campi obbligatori mancanti, tipi
errati e timestamp invalidi producono diagnostica con campo/indice quando
possibile. Un file invalido non impedisce il caricamento degli altri.

Non esistono HTML remoto, download di immagini o esecuzione di processi. I
symlink locali normali sono supportati; quando il target esiste l'identità usa
il path canonico, altrimenti il path assoluto normalizzato.

## 4. Registrazione esplicita e discovery XDG

I file espliciti sono salvati come lista di path in
`customMetrics/files` dentro il normale `AQTRunCat.ini`; il contenuto JSON non
viene copiato nelle impostazioni. Settings permette `Add File…` e `Remove`:
l'aggiunta valida subito un file esistente, mentre la rimozione elimina solo
la registrazione e non cancella mai il file. Una registrazione già persistita
resta visibile come `Unavailable` se il producer rimuove il file e recupera
quando lo ricrea.

La discovery Linux aggiuntiva usa esattamente:

```text
$XDG_DATA_HOME/AQTRunCat/metrics/*.json
```

con fallback XDG di `QStandardPaths::GenericDataLocation`, normalmente
`~/.local/share/AQTRunCat/metrics/`. La directory viene creata all'avvio del
servizio. Un file scoperto che scompare viene rimosso dal registry/UI.

Identità e deduplicazione:

- file esistente: `canonicalFilePath`, quindi alias e symlink risolvibili non
  producono doppioni;
- file mancante: path assoluto ripulito;
- un file esplicito presente anche nella directory XDG è una sola sorgente,
  marcata `Registered + discovered`;
- ordine fra snapshot: registrazioni esplicite nel loro ordine persistito,
  poi soli file discovered ordinati stabilmente per nome/path, mai secondo
  l'ordine casuale del filesystem.

## 5. Watching, atomic replace e retry

Il servizio usa un solo `QFileSystemWatcher` per file e directory padre, oltre
alla directory discovery. Ogni riconciliazione reinstalla i watch persi dopo
`rename`/`unlink`, quindi segue il pattern comune:

```text
scrivi file temporaneo -> close/fsync -> rename sul path finale
```

Le raffiche di eventi sono accorpate con un debounce di 100 ms. Su errore di
lettura/parsing dopo un evento vengono effettuati tre retry non bloccanti a
200 ms. Durante questa finestra lo stato è `Pending` e l'ultimo snapshot
valido rimane disponibile, evitando un lampeggio transitorio. Dopo i retry lo
stato diventa `Error`, o `Unavailable` se il file esplicito manca, conservando
comunque il last-known-good con lo stato fallito, come upstream.

Un unico timer di recovery a 5 s resta attivo soltanto finché esistono sorgenti
esplicite degradate o file invalidi. Non è un refresh delle metriche: a
filesystem sano non esiste polling, e Dashboard refresh 1/2/5 s di M5 non
rilegge i JSON.

## 6. Stato e UI

Ogni sorgente ha stato `Pending`, `Available`, `Unavailable` o `Error`, path,
origine esplicita/discovered, diagnostica e snapshot last-known-good
opzionale. Uno `formattedValue` uguale a `"0"` resta un valore disponibile e
non viene confuso con gli stati di errore.

Dashboard presenta una sezione scrollabile `Custom Metrics` con titolo,
simbolo quando rappresentabile, entry nell'ordine del producer, barre per i
`normalizedValue` e timestamp `lastUpdatedDate`. Stringhe Unicode vengono
mostrate direttamente; alcuni SF Symbols usati dai sample upstream hanno un
piccolo mapping Unicode locale, mentre nomi SF sconosciuti vengono omessi.
Il valore originale `symbol` e `metricsBarValue` resta comunque nel modello
per M7.

Settings mostra il path auto-discovery e un elenco con origine e stato. La
finestra è scrollabile per restare utilizzabile su schermi piccoli; il resto
delle impostazioni M5 mantiene la stessa semantica. `Restore Defaults` non
rimuove le registrazioni Custom Metrics: l'unregister è intenzionale e
separato.

## 7. Test e compatibilità

Le suite M6 coprono:

- fixture rappresentativa Claude Code nello schema upstream, campi opzionali,
  null, unknown fields, Unicode, frazioni di secondo e ordine;
- campi mancanti/tipi errati, JSON vuoto/malformato, timestamp e limiti;
- persistenza, missing future target, discovery runtime, dedup esplicito +
  discovered, ordinamento e unregister senza delete;
- modify, atomic replace, remove/recreate, retry transitorio, errore permanente,
  last-known-good e recovery;
- Dashboard vuota/multipla/status/simbolo/barre/scroll e Settings Add/Remove.

Il contratto CLI è invariato: `aqtruncat metrics` continua a mostrare soltanto
le metriche di sistema M3.

## 8. Validazione runtime e performance

La validazione Plasma 6 Wayland usa un profilo `XDG_CONFIG_HOME` /
`XDG_DATA_HOME` temporaneo. Una fixture compatibile viene scoperta e mostrata
nella Dashboard; Dashboard e Settings rimangono utilizzabili con scaling
125%, runner e metriche built-in continuano ad aggiornarsi. Modify, replace
atomico, invalidazione/ripristino e remove/recreate sono coperti anche dal test
del watcher reale basato su inotify.

Poiché il servizio non campiona periodicamente sorgenti sane, a filesystem
fermo il costo è limitato ai watch inotify e alla memoria dei piccoli snapshot.
Con finestre chiuse, runner fisso a 10 FPS e finestre di 10 s:

| Snapshot | CPU processo (quota di un core) | RSS | watch inotify |
|---:|---:|---:|---:|
| 0 | 0,500% | 59.064 KiB | 1 |
| 5 | 0,500% | 59.188 KiB | 6 |
| 20 | 0,400% | 61.328 KiB | 21 |

Non è emerso un costo CPU incrementale né un reload loop; 20 snapshot
aggiungono circa 2,2 MiB in questa misura grossolana. Clean build con spike
opzionale: zero warning evitabili. Risultato automatico finale: **26 suite,
385 casi Qt Test, zero failure e zero skipped**.

## 9. Limitazioni

- Il file picker standard seleziona file esistenti. Il modello e il servizio
  supportano path futuri/mancanti già persistiti, ma la UI non offre un editor
  manuale del path.
- Il mapping SF Symbol → Unicode è volutamente piccolo; nessun asset remoto o
  dipendenza da icone Apple viene introdotto.
- La discovery è non ricorsiva e considera i soli `*.json` nella directory
  M6.
- Flatpak/document portal e suspend/resume approfonditi restano M8/M9.
