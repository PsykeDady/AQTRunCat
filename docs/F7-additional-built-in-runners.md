# F7 — Additional Built-in Animated Runners

Stato: **completata** il 2026-10-22, versione **1.7.4** (F7.1 Walking Goose
→ 1.7.1, F7.2 Party Parrot → 1.7.2, F7.3 integrazione condivisa, packaging
e validazione → 1.7.3, F7.4 Engine → 1.7.4, aggiunto su richiesta
dell'utente dopo la 1.7.3). Branch `feature/f7` (nessuna operazione Git
eseguita: commit e branch li gestisce l'utente). La 2.0.0 è un traguardo
separato, dopo l'accettazione di F7 da parte dell'utente.

## 1. Asset trovati

| Runner | id | Cartella sorgente | Frame | Dimensione | Formato |
|---|---|---|---|---|---|
| Walking Goose | `walkinggoose` | `third_party/walkinggoose/runners/goose/` | 6 (`goose-frame-0…5.png`) | 24×36 | PNG RGBA, sfondo trasparente, contorno nero nell'alfa (monocromatico) |
| Party Parrot | `partyparrot` | `third_party/partyparrot/runners/parrot/` | 10 (`parrot-frame-0…9.png`) | 50×36 | come sopra |
| Engine | `engine` | `third_party/runcatneo/runners/engine/` | 10 (`engine-frame-0…9.png`) | 83×36 | RunCat Neo, invariato, monocromatico |

Entrambi preparati e approvati il 2026-10-22 a partire da GIF di Tenor
(provenienza, SHA-256 dei GIF e parametri della lavorazione nei
`README.md` delle due cartelle e in `THIRD_PARTY_NOTICES.md`; autore
originale non verificabile, licenza sconosciuta, inclusi per decisione
esplicita dell'utente). Nessun asset modificato in F7: gli stessi byte
vengono compilati e installati (verificato dal test `bundledrunners`).
Stessa altezza di Cat e Coffee (36 px), ordine dei file naturale e
continuo, nessun avviso del loader.

## 2. Integrazione

La pipeline dei runner era già generica: `RunnerService` legge i runner
inclusi da `:/runners/<id>/` nell'eseguibile e il backend da
`share/aqtruncat/runners/<id>/`; Settings, `aqtruncat runner list`,
`GetRunners` e il Plasmoid elencano ciò che trovano. L'unico elenco è
`AQTRUNCAT_BUNDLED_RUNNERS` in `src/CMakeLists.txt`.

Modifiche:

- `AQTRUNCAT_BUNDLED_RUNNERS` = `cat;coffee;walkinggoose;partyparrot;engine`
  (Engine non ha bisogno di una cartella propria: usa la ricaduta RunCat
  Neo; `data/runners/engine/metadata.json`, autore Kyome22).
- Frame per id: `runner_frames_walkinggoose` / `runner_frames_partyparrot`
  puntano alle cartelle F7; gli altri id usano
  `third_party/runcatneo/runners/<id>` (prima c'era un'unica radice).
  L'id è il nome della cartella installata (`:/runners/<id>/`,
  `share/aqtruncat/runners/<id>/`), indipendente dal nome della cartella
  sorgente: gli asset non sono stati rinominati.
- Una risorsa Qt per runner (`qt_add_resources … PREFIX /runners/<id>`)
  invece di una sola con un'unica `BASE`.
- Cache CMake: una build configurata prima di F7 ha `cat;coffee` in cache
  e non vedrebbe i nuovi runner (la build di sviluppo e quella riusata da
  `install.sh --no-build`). Ora il valore predefinito usato è registrato in
  `AQTRUNCAT_BUNDLED_RUNNERS_DEFAULT` (INTERNAL): se il valore in cache è
  il predefinito precedente (o `cat;coffee` quando non c'era registrazione)
  passa al nuovo; un elenco personalizzato (`-D…=cat;engine`) resta.
  Provato su build esistente, nuova personalizzata e nuova con
  `cat;coffee`.
- `data/runners/walkinggoose/metadata.json` e
  `data/runners/partyparrot/metadata.json`: `displayName` "Walking Goose" /
  "Party Parrot", `author` "Unknown (…via Tenor)", `type` `monochrome`.

Nulla cambia nel backend (stessa API D-Bus, stesso `GetRunners`), nel
tray Qt, nel Plasmoid, nell'animatore o nel campionamento. I frame si
decodificano una volta per runner, le icone del tray si preparano una
volta (come per Cat), il Plasmoid anima i file che `GetRunners` elenca.
La velocità segue la CPU come per ogni runner (10–40 FPS); i tempi propri
dei GIF (0,04 s e 0,10 s per frame) non vengono riprodotti, per scelta
architetturale.

## 3. Selezione e persistenza

Impostazione condivisa `runnerId` (tray Settings, configurazione del
Plasmoid, `aqtruncatctl settings set runnerId …`, `--runner`). Cat resta
il predefinito; Restore Defaults torna a Cat. Regole esistenti, ora
verificate anche con i nuovi runner: un runner salvato che manca
dall'installazione → predefinito e impostazione riparata; presente ma
rotto → predefinito per la sessione, impostazione conservata;
`--runner` esplicito mancante o rotto → errore.

## 4. Installazione e packaging

- Installer (`install.sh`, sistema e `--local`, `--no-build`, dry run,
  disinstallazione): nessuna modifica; installa ciò che `cmake --install`
  produce e rimuove tutto `share/aqtruncat/` dal manifest.
- Arch, Fedora, Debian: i file stanno in `share/aqtruncat/`, già coperto.
  `packaging/debian/copyright`: nuova stanza per `third_party/walkinggoose/*`
  e `third_party/partyparrot/*` con "License: unknown" e la spiegazione
  (prima ricadevano in `Files: *` come Apache-2.0, il che sarebbe stato
  falso).
- Plasmoid: nessun file nel pacchetto del widget; legge i frame installati
  tramite `GetRunners`.
- Versione 1.7.4 in CMake, PKGBUILD, spec RPM (+ changelog),
  `debian/changelog`, pagina man, `metadata.json` del Plasmoid
  (`packaging-versions` verde).

## 5. Test

| Suite | Nuovo |
|---|---|
| `bundledrunners` (nuova, 6 casi, 10 righe) | per Cat, Coffee, Walking Goose, Party Parrot, Engine: caricamento dei file installati nel build tree, id, nome, monocromatico, numero di frame, dimensione, ordine naturale e sequenza, byte uguali ai sorgenti, sfondo trasparente; `aqtruncat runner list` (risorse compilate) elenca i cinque; `RunnerService` sulla cartella installata; persistenza nell'INI, Restore Defaults → Cat; selezione all'avvio; runner mancante (ripara) e rotto (non ripara), `--runner` esplicito = errore |
| `backendipc` | `GetRunners` del backend reale: `walkinggoose`, `partyparrot` ed `engine` bundled, validi, monocromatici, dimensioni, file esistenti sotto `share/aqtruncat/runners/<id>/`, sequenza |
| `plasmoidclient` | selezione via `aqtruncatctl` → il client QML del widget segue (`selected`, `runnerId`) per i tre runner |
| `plasmoidlogic` | `runnerToShow` con i due cataloghi (aspetto 24/36 e 50/36, frame, sequenza) |
| installer | frame e metadata dei tre runner installati e nel manifest (la disinstallazione toglie già tutto `share/aqtruncat`) |

Risultati: build pulita in directory nuova **0 warning**; ctest
**53/53** (anche dopo F7.4); `loadsInPlasma` 3/3 non saltato; `packaging-versions` 1.7.4;
shellcheck e `git diff --check` puliti. Nessuna regressione su Cat,
Coffee e sui tooltip F6.

## 6. Validazione runtime (isolata)

- Tray: `aqtruncat run` della build con `runnerId` salvato a
  `walkinggoose` e poi `partyparrot`, bus privato
  (`build/tests/backendipc-bus.conf`), XDG temporanei, host SNI finto,
  piattaforma Wayland reale senza finestre: runner caricato dall'impostazione
  ("6 frames, 24x36, monochrome" / "10 frames, 50x36"), ~10 fps (72–73
  `NewIcon` in 8 s a CPU bassa), tooltip F6 "AQTRunCat — Walking Goose" /
  "— Party Parrot" con il riepilogo, impostazione invariata, uscita 0.
- Plasmoid: `tools/plasmoid-preview.sh --runner walkinggoose|partyparrot`
  (offscreen, backend reale): runner nei pannelli orizzontali 22/34/48 e
  verticali 34/48, tema chiaro e scuro, proporzioni corrette, tooltip con il
  nome del runner.

## 7. Prestazioni

Come un runner esistente di pari dimensioni: nessun timer, processo o
campionamento in più. Il costo SNI per frame è proporzionale ai pixel
(M2 §10): Walking Goose (24×36) costa meno di Cat (56×36), Party Parrot
(50×36) circa come Cat.

## 8. F7.4 Engine

Aggiunto dopo la 1.7.3 su richiesta dell'utente: id nell'elenco, metadata,
note di terze parti ("usato solo dallo spike" → runner incluso), test
(`bundledrunners`, `backendipc`, `plasmoidclient`, installer). La
migrazione della cache CMake ha aggiornato da sola la build esistente
(`…;partyparrot` → `…;partyparrot;engine`). Runtime isolato: tray con
`runnerId=engine` (10 frame 83×36, ~10 fps, tooltip "AQTRunCat — Engine"),
anteprima del Plasmoid. Limite accettato: largo (≈ 2,3:1), ~22×10 px nello
slot quadrato del tray di Plasma e basso (~34×15 px) in un pannello
verticale da 34 px; nei pannelli orizzontali resta leggibile.

## 9. Verifiche manuali per l'utente

1. Tray nella sessione Plasma reale: scegliere Walking Goose, Party Parrot ed Engine
   in Settings, leggibilità alla dimensione del pannello, temi chiaro e
   scuro, velocità con carico.
2. Plasmoid dopo il riavvio di plasmashell (il QML non è cambiato, ma
   l'elenco dei runner arriva dal backend: basta che il backend installato
   sia 1.7.4): selettore nella configurazione, pannello orizzontale e
   verticale.
3. Dopo l'installazione: chiudere e riaprire, il runner scelto resta.
