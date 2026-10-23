# AQTRunCat — M2 Runner System

> **Aggiornamento post-M2.** Le decisioni aperte del §12 sono state risolte
> in M2.1 (`docs/runner-import-and-cli.md`): import ZIP con libarchive,
> rimozione dei runner e CLI a sottocomandi. Selezione persistente e
> anteprima sono passate a M5. Le forme CLI `--runner` e `--list-runners`
> descritte qui restano valide come alias. Il resto del documento descrive lo
> stato di M2 così come è stato osservato.

> Obiettivo: il motore non sa più che cosa sia "il gatto". Conosce solo il
> concetto generico di Runner. Il gatto è il runner predefinito distribuito
> con AQTRunCat, trattato come qualsiasi altro.
> Data: 2026-10-02. Ambiente: lo stesso di M1 (Arch, Plasma 6.7.5 Wayland,
> Qt 6.11.2).
>
> Stato: completo, tranne l'**import ZIP**, fermato in attesa di una decisione
> (§12.1), e di tre voci della lista M2 dei requisiti che dipendono da
> decisioni (§12.2).

## 1. Architettura

```text
CpuMonitor ─► cpuToFps ─► FpsStabilizer ─► RunnerAnimator ─► TrayPresenter
 (M1, invariato)                            (frame timing)     (icone, tinta)
                                                  ▲                  ▲
                                         sequence │                  │ frames, monochrome
                                                  └──── Runner ──────┘
                                                          ▲
                              discoverRunners ─► chooseRunner ─► loadRunner
                              (bundled + user)   (--runner / default) (validazione)
```

| Componente | File | Conosce | Non conosce |
|---|---|---|---|
| `Runner` (modello) | `src/runner/runner.h` | frame decodificati, ordine, dimensioni, monocromia | CPU, tray, timing |
| `loadRunner` | `src/runner/runnerloader.*` | layout su disco, PNG, metadata | tray, CPU |
| `discoverRunners` / `chooseRunner` | `src/runner/runnercatalog.*` | radici bundled/utente, precedenza, scelta | tray, CPU |
| ordinamento nomi | `src/runner/framenames.*` | ordinamento naturale, filtro dei file | il resto |
| `FramePlayback` | `src/core/frameplayback.h` | posizione nella sequenza (puro C++) | tempo, FPS, Qt |
| `RunnerAnimator` | `src/runner/runneranimator.*` | timer, FPS, sequenza | CPU, tray, immagini |
| `TrayPresenter` | `src/app/traypresenter.*` | `QSystemTrayIcon`, tinta, tooltip | path, CPU, sorgenti |
| `main.cpp` | `src/app/main.cpp` | collegamento e CLI | — |

Pipeline CPU (sampling, EMA, curva, stabilizzatore) **invariata** rispetto a
M1: `cpumonitor.*`, `core/cpustat.*`, `core/smoothing.h` e
`core/speedcurve.*` non sono stati toccati. Unica differenza: la CPU ora
imposta gli FPS di `RunnerAnimator` invece che del vecchio `RunnerTray`.
`src/app/runnertray.*` (con `loadFrames(":/runners/" + nome)`) è stato
eliminato.

Nel codice non resta nessuna assunzione sul gatto: niente nomi, numero di
frame, dimensioni o path. Il runner predefinito è solo la variabile CMake
`AQTRUNCAT_DEFAULT_RUNNER` (default `cat`). I runner inclusi sono
`AQTRUNCAT_BUNDLED_RUNNERS` (default `cat;coffee`); CMake fallisce se il
default non è tra i runner inclusi. Aggiungere un runner non richiede
modifiche al motore.

## 2. Modello

`Runner` è una struct semplice, senza gerarchie:
`id`, `displayName`, `author`, `source` (`Bundled` | `User`), `path`,
`frames` (`QList<QImage>`, decodificati una volta), `sequence` (indici dei
frame in ordine di riproduzione), `frameSize`, `monochrome`, `warnings`.

- `sequence` copre sia l'ordine standard 0..n-1 sia sequenze con ripetizioni
  come quelle dei built-in di RunCat Neo (swing, pendulum), tramite il campo
  `frameOrder`.
- `monochrome` è utile alla compatibilità: un runner monocromatico viene
  disegnato nel colore del testo del pannello (semantica "template" upstream),
  uno a colori così com'è.
- Le dimensioni sono libere (nessuna assunzione `width == height`). Il loader
  non ridimensiona, non deforma e non aggiunge bordi.

## 3. Formato e layout

Il formato è stato riverificato su RunCat Neo `b3b1543` e Runner Gallery
`b979658` (43 runner): 36 px di altezza, 25–100 px di larghezza, da 5 a 24
frame, nomi `<runner>-frame-<N>.png`, 37 monocromatici e 6 a colori.

Layout accettati per un runner (directory `<id>/`):

```text
<id>/<qualsiasi>.png …                      frame direttamente nella directory
<id>/<id>-frames/<id>-frame-N.png …         una sola sotto-directory di frame
                                            (cartella Gallery con lo ZIP estratto)
<id>/metadata.json                          opzionale (anche dentro la sotto-directory)
```

- `metadata.json` usa le chiavi della Runner Gallery: `displayName`,
  `author`, `type` (`monochrome` | `color`); `tags` e le altre chiavi vengono
  ignorate. In più c'è la chiave opzionale `frameOrder` (indici), stesso nome
  usato da RunCat Neo nei runner personalizzati.
- Senza metadata, il nome viene derivato dall'id (`border-collie` →
  "Border Collie"). Il tipo viene dedotto dai frame: un runner è monocromatico
  se meno del 3% dei pixel visibili è colorato. Ho tarato la soglia su tutti i
  43 runner della Gallery: i monocromatici arrivano al massimo allo 0,67%, quelli
  a colori partono dall'8,84%. Il risultato coincide con il `type` dichiarato
  in 43 casi su 43. Una prima versione ("anche un solo pixel colorato") sbagliava
  su `icosahedron`, che ha bordi antialiasati blu scuro.
- La directory `<nome>-frames` ottiene l'id `<nome>`: uno ZIP della Gallery
  estratto così com'è nella directory utente funziona subito.

## 4. Ordinamento dei frame

Ordinamento naturale deterministico (`naturalLess`): i blocchi di cifre si
confrontano per valore numerico, senza overflow anche oltre i 64 bit, e il
testo senza distinguere maiuscole. Le parità ("1" e "01", "A" e "a") si
risolvono con un confronto semplice, così l'ordine è totale e **non dipende
dal locale**. Non uso `QCollator`, che con ICU cambia comportamento a seconda
del locale. È lo stesso criterio di RunCat Neo, che ordina i frame importati
con `localizedStandardCompare`.

Risultato: `1, 2, 3, …, 10, 11` e non `1, 10, 11, 2, 3` (il bug di
runcat-tray, che ordinava con `strcmp`). Con 24 frame
(`escapement-frame-0..23`) l'ordine è stato verificato end-to-end (§9).

## 5. Validazione

| Caso | Esito |
|---|---|
| directory inesistente | **fatale** |
| nessun PNG (directory vuota o solo altri file) | **fatale** |
| più sotto-directory con frame | **fatale** (ambiguo) |
| file `.png` non PNG (es. JPEG rinominato) o PNG troncato/corrotto | **fatale**, con il nome del file |
| frame di dimensioni diverse | **fatale**, con le dimensioni attese e trovate |
| lato > 1024 px (letto dall'header, prima di decodificare) o > 256 frame | **fatale** |
| buchi nella numerazione (`f-0, f-1, f-3`) o numeri duplicati (`f-1`, `f-01`) | warning |
| dimensioni fuori da 10–100 × 36 (formato upstream) | warning |
| un solo frame | warning |
| `metadata.json` illeggibile, JSON non valido, tipi sbagliati, `type` sconosciuto, `frameOrder` fuori range | warning, si usano i default |
| `.txt`, `.DS_Store`, `._*` (AppleDouble), `Thumbs.db`, `preview.png`, `__MACOSX/`, `.zip` | ignorati |

Principio: i problemi dei **frame** sono fatali, perché impediscono di animare
correttamente. I problemi dei **metadata** e i file estranei non invalidano
mai un runner. Un runner non valido non manda in crash AQTRunCat: viene
mostrato come `INVALID` in `--list-runners` e, se richiesto esplicitamente,
produce un errore leggibile.

## 6. Discovery, XDG e precedenza

| Sorgente | Posizione |
|---|---|
| bundled | risorse Qt compilate nell'eseguibile, `:/runners/<id>/` (frame da `third_party/`, metadata da `data/runners/`) |
| user | `QStandardPaths::GenericDataLocation` + `/AQTRunCat/runners`, cioè `$XDG_DATA_HOME/AQTRunCat/runners/`, con fallback `~/.local/share/AQTRunCat/runners/` |

- Solo dati runner. Config e cache non sono usate da M2 e restano separate,
  come previsto dagli standard.
- Discovery: un solo livello, directory nascoste saltate, radici mancanti
  accettate senza errori.
- **Precedenza: user > bundled.** Un runner utente con lo stesso id sostituisce
  quello incluso. È il modo previsto per "personalizzare" un runner di
  sistema, e la sostituzione viene loggata.
- Collisione dentro la stessa radice (`beagle/` e `beagle-frames/`): vince la
  prima in ordine naturale (`beagle`), l'altra viene ignorata con una nota.
- Nessuna risoluzione di versioni.
- Nei test il path XDG è simulato con `XDG_DATA_HOME` e `HOME` temporanei; la
  discovery riceve le radici come parametri e non tocca mai la home reale.

## 7. Selezione e CLI diagnostica

```text
aqtruncat                      runner predefinito, guidato dalla CPU (come M1)
aqtruncat --runner <id>        un runner specifico
aqtruncat --list-runners       ID, nome, sorgente, frame, dimensioni, modalità, warning; poi esce
aqtruncat --fps N              (M1) frame rate fisso, ignora la CPU; combinabile con --runner
```

- `--runner` con un id sconosciuto o non valido: **errore** (exit 1) che
  elenca i runner trovati. Nessun fallback: chi chiede un runner esplicito
  deve sapere che non c'è.
- Senza `--runner`: si usa il predefinito. Se manca o non è valido, si usa il
  primo runner valido (prima i bundled, poi in ordine di id) con un warning
  esplicito, perché l'app deve poter partire. Se non ce n'è nessuno: errore.
- `--list-runners` usa una `QCoreApplication`, quindi non serve un display.
- Gli errori di riga di comando vanno sempre su stderr. Il logging Qt finisce
  su journald quando stderr non è un terminale, e prima `--fps 0` usciva con
  codice 2 senza mostrare alcun messaggio: corretto durante M2.

## 8. Licenze e provenienza

- Inclusi: `cat` (default) e `coffee`, frame RunCat Neo `b3b1543` Apache-2.0,
  non modificati (`third_party/runcatneo/`, attribuzione in
  `THIRD_PARTY_NOTICES.md`). `coffee` serve a verificare con un asset reale
  che il motore è generico (10 frame invece di 5, proporzioni diverse).
- `data/runners/{cat,coffee}/metadata.json` sono file di AQTRunCat (nome,
  autore, monochrome).
- Nessun nuovo asset è entrato nel progetto. I runner della Gallery usati per
  i test (Apache-2.0, autori in `metadata.json`) sono rimasti in una directory
  temporanea fuori dal progetto. Nessun asset di runcat-tray è stato usato.

## 9. Test reali su Plasma 6 + Wayland

Fixture utente in un `XDG_DATA_HOME` temporaneo, con runner presi dalla
Gallery:

- `escapement`: layout cartella Gallery completo, 24 frame;
- `dojo-panda-frames`: ZIP estratto così com'è, a colori, senza metadata;
- `cat-tail`: 100 px;
- `engine`: 83 px;
- `broken`: directory vuota.

**Compatibilità Gallery**: tutti i 43 runner, estratti dai loro ZIP, vengono
caricati con 0 errori e 0 warning; tipo monocromatico/colore corretto in 43
casi su 43.

**Ordine dei frame end-to-end.** Ho letto l'`IconPixmap` realmente ricevuto
da Plasma via D-Bus ogni ~0,2 s, a 2 fps. Ogni campione è stato ricondotto al
file sorgente tramite il canale alpha, tenendo conto dei frame duplicati negli
asset. Risultato: 0 passi errati su 7 runner. Plasma riceve sempre la
dimensione nativa (56×36, 38×36, 70×36, 48×36, 100×36, 83×36): nessun
ridimensionamento o padding da parte nostra.

| Runner | Frame | Frame distinti visti | Passi errati |
|---|---:|---:|---:|
| cat | 5 | 5/5 | 0 |
| coffee | 10 | 10/10 | 0 |
| escapement | 24 | 24/24 | 0 |
| engine | 10 | 10/10 | 0 |
| dojo-panda (colore) | 12 | 11/11 unici (frame 11 = frame 0 nell'asset) | 0 |
| cat-tail | 8 | 5/5 unici (ping-pong già nei file: 5 = 3, 6 = 2, 7 = 1) | 0 |

Due "anomalie" iniziali erano dovute al mio verificatore, che non gestiva i
frame identici; non al loader.

**Velocità consegnata.** Segnali `NewIcon` al secondo, con `--fps` 10/20/40
su `cat`, `dojo-panda`, `escapement` e `cat-tail`: 10,4 / 20,4 / 40,4
ovunque. Lo +0,4 è il margine della finestra di misura.

**Verifica visiva.** Ho ritagliato soltanto l'area dell'icona, individuata
come pixel che cambiano tra screenshot successivi; i ritagli sono rimasti
fuori dal progetto.

| Runner | Resa nella tray |
|---|---|
| cat | riconoscibile, proporzioni corrette |
| coffee | leggibile |
| escapement (70 px) | ruota rotonda, aspect ratio preservato; piccolo ma leggibile |
| engine (83 px) | al limite |
| cat-tail (100 px) | minuscolo, poco leggibile |
| dojo-panda (colore) | colori preservati (non tinto); proporzioni corrette |

Nessuna deformazione e nessun clipping.

**Runner predefinito e CPU (come M1).**

`tools/test-cpu-runtime.sh` (due esecuzioni):

| Fase | Esecuzione 1 | Esecuzione 2 |
|---|---|---|
| idle | 11 fps, 0 cambi | 11 fps, 0 cambi |
| metà carico | 23–32 fps | 25–27 fps |
| pieno | 38–40 fps | 38–40 fps |
| recovery | 11–21 fps | 11–13 fps |

Nella prima esecuzione il carico di fondo era più rumoroso: CPU grezza in metà
carico con σ 7,3% (massimo 97%) invece del 3,3% della seconda e del 4,4% di
M1.1. Il codice CPU è invariato: la seconda esecuzione riproduce M1.1.

`tools/test-cpu-fps-curve.sh`: 21 istanze, FPS lanciati
`10 10 11 13 15 18 21 25 29 34 40 34 … 10`, tutte con runner `cat (bundled)`.

## 10. Performance

| Misura | Valore |
|---|---|
| caricamento runner (decodifica + validazione) | 2–3 ms (5–12 frame), 7 ms (`escapement`, 24 frame) |
| memoria frame decodificati | cat 39 KiB, coffee 53 KiB, cat-tail 113 KiB, escapement 236 KiB; altrettanto per le icone della tray |
| RSS processo | ~50 MB per tutti i runner, come M1: dominata dalle librerie del tema KDE |
| CPU a riposo, 60 s, CPU reale ~20% (11 fps) | cat 0,48% di un core (M1.1: 0,40%), escapement 0,67% |
| per tick | solo `setIcon` di un'icona preparata; nessuna lettura da disco |

La differenza di CPU fra runner viene dalla serializzazione dell'immagine a
ogni frame lato KStatusNotifierItem (M1.0 §5.1), proporzionale ai pixel:
70×36 contro 56×36. Non è un costo del loader.

## 11. Limitazioni note (osservate)

1. Runner molto larghi illeggibili nello slot quadrato di Plasma: `cat-tail`
   a 100 px è poco leggibile, `engine` a 83 px è al limite. È la limitazione
   M1.0, non affrontata in M2. Il motore conserva dimensioni e aspect ratio
   reali, quindi una soluzione futura non è ostacolata.
2. Il cambio di runner a runtime è supportato dall'architettura
   (`TrayPresenter::setRunner` sostituisce frame e icone,
   `RunnerAnimator::setSequence` riparte dal primo frame mantenendo timer e
   FPS). È verificato dai test automatici dell'animator, ma senza UI non è
   stato esercitato nell'app reale.
3. Il tipo dedotto senza metadata è un'euristica: corretta su 43/43 runner
   della Gallery, ma un runner "a un solo colore non grigio" (es. tutto rosso)
   risulta "colore". `metadata.json` ha sempre la precedenza.
4. Dei runner personalizzati di RunCat Neo (`<UUID>/frame-N.png`) si possono
   caricare i frame, con id = UUID. Nome e `isTemplate`, che stanno in
   `CUSTOM_RUNNERS.json`, non vengono importati.

## 12. Decisioni aperte

### 12.1 Import ZIP (fermato come richiesto)

Gli ZIP della Gallery usano sia STORED sia DEFLATE (283 e 139 voci sui 43
archivi), quindi serve una vera decompressione. Qt non ha un'API pubblica per
gli ZIP. Opzioni verificate su questa macchina:

| Opzione | Nuova dipendenza | Pro | Contro |
|---|---|---|---|
| A. `QZipReader` privato di QtCore (header presente: `QtCore/6.11.2/QtCore/private/qzipreader_p.h`) | no (`Qt6::CorePrivate`) | ~20 righe | API privata, nessuna garanzia di compatibilità tra versioni di Qt; alcune distro separano gli header privati |
| B. libarchive (installata, 3.8.9) | sì, piccola e diffusissima | API pubblica, matura, STORED/DEFLATE e oltre | dipendenza esplicita nuova |
| C. zlib (già caricata da QtCore) + lettore ZIP minimo scritto da noi | link esplicito a zlib | nessuna libreria nuova di fatto | ~200 righe di parsing sensibile alla sicurezza da mantenere |
| D. KArchive | KF | — | escluso dai requisiti (niente KDE Frameworks) |
| E. `unzip`/`bsdtar` esterni | dipendenza runtime | niente codice di parsing | processo esterno e programma esterno richiesto |

Raccomandazione: **B (libarchive)**. A va bene se si vuole zero dipendenze
accettando il rischio dell'API privata.

Con qualunque opzione il design resta quello richiesto, in tre fasi separate:

1. **estrazione** in memoria, con limiti come quelli del validatore della
   Gallery (≤ 2 MB per archivio, ≤ 64 voci, ≤ 1 MB per voce) e rifiuto di path
   assoluti, `..`, symlink e voci fuori dalla directory `<nome>-frames/`;
2. **validazione** con `loadRunner` su una directory temporanea dentro la
   directory utente;
3. **installazione** con rename atomico in `runners/<id>`, senza sovrascrivere
   un runner esistente se non richiesto.

Il loader accetta già oggi il risultato: uno ZIP estratto a mano nella
directory utente funziona (§3, §9).

### 12.2 Altre voci M2 dei requisiti

- **Selezione persistente**: oggi la selezione esiste solo via `--runner`.
  Senza UI, persisterla significa decidere chi la scrive: una chiave
  `QSettings` in `$XDG_CONFIG_HOME`, oppure attendere M5 (Settings). Propongo
  M5.
- **Rimozione runner**: oggi basta cancellare la directory utente. Un comando
  dedicato ha senso insieme all'import (§12.1).
- **Anteprima**: `tools/cycle-runners.sh` copre l'anteprima di sviluppo; per
  l'anteprima utente serve una UI, quindi M5.
- **Runner inclusi**: oggi `cat` e `coffee`. Aggiungere altri built-in di
  RunCat Neo (dog, slime, drop, newton-cradle, mochi; licenza verificata in
  M0) significa solo copiarli in `third_party/` e aggiungere i metadata.
  Quelli con ordine swing/pendulum useranno `frameOrder`. Da decidere quali.

### 12.3 Nota

`.gitignore` ora contiene soltanto `docs/tray-spike/*.png`: `build/` non è
più escluso. Non l'ho toccato; quando reinizializzerai Git conviene
aggiungerlo di nuovo.

## 13. Strumenti

- Nuovo: `tools/cycle-runners.sh`. Mostra in sequenza tutti i runner validi,
  o gli id passati, a FPS fisso. Supporta `--fps`, `--duration` e `--binary`;
  i runner utente seguono `XDG_DATA_HOME`. Come gli strumenti M1: PID esatto,
  cleanup su Ctrl+C reale (verificato via pseudo-terminale, exit 130), nessun
  `pkill`, nessun path locale.
- `tools/test-cpu-fps-curve.sh` e `tools/test-cpu-runtime.sh` funzionano
  invariati con l'app M2.
