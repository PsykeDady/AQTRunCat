# AQTRunCat — M1.1 CPU-driven runner

> Obiettivo: il gatto nella tray accelera e rallenta seguendo il carico CPU
> reale. **Raggiunto** su KDE Plasma 6.7.5 + Wayland (Qt 6.11.2), vedi §6.
> Data: 2026-10-02.

## 1. Architettura

```text
/proc/stat ──► CpuMonitor ─────────────► cpuToFps ──► FpsStabilizer ──► RunnerTray
              (timer 1 s,              (curva          (anti-flicker)    (timer animazione,
               parse + delta,           quadratica)                       QSystemTrayIcon)
               EMA)
```

| File | Ruolo | Dipendenze |
|---|---|---|
| `src/core/cpustat.{h,cpp}` | parsing della riga `cpu` di `/proc/stat`, delta tra campioni (`cpuBusyBetween`, `CpuSampler`) | solo C++20 |
| `src/core/smoothing.h` | `Ema`, parametro `CpuSmoothingAlpha` | solo C++20 |
| `src/core/speedcurve.{h,cpp}` | `cpuToFps`, `FpsStabilizer`, `MinFps`/`MaxFps` | solo C++20 |
| `src/app/cpumonitor.{h,cpp}` | timer di campionamento, lettura file, logging errori | QtCore |
| `src/app/runnertray.{h,cpp}` | tray, frame, tinta, timer animazione | QtWidgets |
| `src/app/main.cpp` | collegamento della pipeline, CLI minima | — |
| `tests/tst_*.cpp` | test unitari del core | QtTest |

La logica (`src/core`) non conosce Qt né la tray; `RunnerTray` non conosce la
CPU (riceve solo `setFps`). Il runner è il gatto validato in M1.0
(`third_party/runcatneo/runners/cat`, Apache-2.0).

## 2. CPU calculation

Sorgente: la riga aggregata `cpu` di `/proc/stat` (prima riga del file), letta
direttamente con `QFile`, senza processi esterni.

Campi (jiffies cumulativi): `user nice system idle iowait irq softirq steal guest guest_nice`.

```text
active = user + nice + system + irq + softirq + steal
idle   = idle + iowait
total  = active + idle
cpu%   = 100 × Δactive / Δtotal          (Δ tra due campioni successivi)
```

- **nice → active.** Un processo a bassa priorità sta comunque usando la CPU.
  Upstream (SystemInfoKit) lo mette nel denominatore ma non nel numeratore:
  differenza voluta, come da requisiti §8.
- **iowait → idle.** iowait è tempo in cui la CPU è *inattiva* mentre un task
  attende I/O: la CPU è disponibile per altro lavoro. È anche la scelta di
  htop/procps. La documentazione del kernel (`Documentation/filesystems/proc.rst`)
  definisce iowait come inaffidabile: può persino diminuire sulla riga
  aggregata. Conseguenza visibile: un carico puramente di I/O (es. copia su
  disco lento) **non** accelera il gatto. È un'ambiguità di semantica reale:
  vedi §9.
- **steal → active.** Tempo in cui l'hypervisor ha tolto la vCPU alla VM: il
  sistema voleva girare e non ha potuto. È la scelta di htop.
- **irq, softirq → active.** Lavoro reale del kernel.
- **guest, guest_nice → ignorati.** Il kernel li somma già dentro `user` e
  `nice`; contarli di nuovo li conterebbe due volte.

Robustezza:

| Caso | Comportamento |
|---|---|
| Primo campione | Nessun valore: serve un delta. Il runner parte a 10 fps. |
| Δtotal = 0 | Nessun valore (nessuna informazione); la baseline resta valida. |
| Un contatore diminuisce (es. regressione nota di iowait, hotplug) | Il delta di quel campo vale 0, flag `counterRegressed`, log a livello debug. |
| Tutti i contatori diminuiscono (reset) | Nessun valore; il campione diventa la nuova baseline. |
| Kernel vecchio con < 10 campi | Campi mancanti = 0; servono almeno 4 campi. |
| Campi extra futuri | Ignorati. |
| Riga mancante, campo non numerico/negativo/overflow, meno di 4 campi | Errore esplicito. `CpuMonitor` lo logga come warning **una volta per episodio** (non ogni secondo) e lo segnala di nuovo al ripristino; il runner mantiene l'ultima velocità. |
| Range | `0 ≤ cpu ≤ 100` per costruzione (+ clamp difensivo). Nessun tetto artificiale al 99,9%. |

Il risultato è un rapporto di jiffies, quindi l'irregolarità del timer non lo
distorce: conta solo il tempo CPU accumulato tra le due letture.

## 3. Sampling

**Intervallo: 1 s**, con un timer separato da quello dell'animazione.

Motivi:

- Reattività: un indicatore "a colpo d'occhio" deve reagire in pochi secondi.
  Il default di RunCat Neo è 5 s (3/5/10 configurabili, M0 §5): la velocità
  cambia a scatti ogni 5 s. runcat-tray usa 1 s.
- Stabilità: con 1 s il rumore del campione è maggiore, ma lo assorbe lo
  smoothing (§4). Così si ottiene reattività e stabilità insieme, mentre un
  campione lungo darebbe stabilità a prezzo di ritardo.
- Costo: una lettura di `/proc/stat` costa ~19 µs. Il costo misurato del
  campionamento è sotto la risoluzione: AQTRunCat 0,40% di core contro 0,38%
  con `--fps 11` (stessa animazione, nessuna lettura), su 60 s.

Il campionamento non dipende dagli FPS: a 40 fps si legge comunque
`/proc/stat` una volta al secondo.

## 4. Smoothing

Due stadi minimi, entrambi in `src/core`:

1. **EMA sulla CPU**: `s ← s + α (x − s)` con **α = 0,4** a 1 campione/s.
   La costante di tempo è τ = −1 s / ln(0,6) ≈ 2 s: dopo un gradino la media
   copre il 40% del salto in 1 s, il 64% in 2 s e il 92% in 5 s. Il rumore
   alternato viene ridotto del fattore α/(2−α) = 0,25. Il primo valore
   inizializza la media, così all'avvio il gatto non deve risalire da zero.
2. **FpsStabilizer sugli FPS**: un target che differisce di **≥ 3 fps** viene
   applicato subito (cambiamento reale di carico). Una differenza di 1–2 fps
   viene applicata solo se lo stesso target si ripete **3 campioni di fila**
   (≈ 3 s). Questo elimina l'oscillazione 18↔19 e, sulla parte ripida della
   curva, 26↔28.

Parametri (modificabili in un solo punto ciascuno):

| Parametro | Valore | Dove |
|---|---|---|
| intervallo di campionamento | 1000 ms | `CpuMonitor::DefaultInterval` |
| α EMA | 0,4 | `aqtruncat::CpuSmoothingAlpha` |
| salto immediato | 3 fps | `FpsStabilizer` (default del costruttore) |
| conferme per salti piccoli | 3 campioni | `FpsStabilizer` (default del costruttore) |

**Come è stato scelto il salto immediato.** Nella prima esecuzione reale la
soglia era 2 fps: con carico parziale stabile si sono visti 7 cambi in 24 s
(26↔28), perché il rumore grezzo era più ampio (σ 4,4%) e lì la curva è
ripida. Ho ripassato offline i campioni grezzi registrati nella stessa
pipeline con parametri diversi:

| α | salto immediato | cambi a regime (idle/metà/pieno/recovery) | metà ≥20 fps | pieno ≥35 fps | recovery ≤15 fps | recovery ≤12 fps |
|---:|---:|---|---:|---:|---:|---:|
| 0,4 | 2 | 0 / 7 / 1 / 1 | 2,9 s | 1,9 s | 2,9 s | 4,9 s |
| **0,4** | **3** | **0 / 1 / 1 / 1** | **2,9 s** | **1,9 s** | **2,9 s** | **6,9 s** |
| 0,4 | 4 | 0 / 1 / 1 / 1 | 3,9 s | 1,9 s | 2,9 s | 6,9 s |
| 0,3 | 2 | 0 / 5 / 1 / 2 | 3,9 s | 2,9 s | 3,9 s | 7,9 s |
| 0,3 | 3 | 0 / 3 / 1 / 2 | 3,9 s | 3,9 s | 4,9 s | 7,9 s |
| 0,25 | 2 | 0 / 2 / 2 / 2 | 4,9 s | 4,9 s | 5,9 s | 9,9 s |

Con la soglia 3 il jitter sparisce e la reattività ai cambi di carico resta
invariata. Peggiora solo l'ultimo assestamento da 15 a 12 fps. Ridurre α non
porta benefici e rallenta tutto. La prima riga riproduce esattamente il
comportamento osservato dal vivo, a conferma che il replay è fedele.

## 5. Mapping

```text
fps = round(10 + 30 * (cpu / 100)^2)      cpu clamped to [0, 100]
```

`aqtruncat::cpuToFps(double)` in `src/core/speedcurve.cpp`. È una funzione
pura e testata: monotona su 0..100 a passi di 0,01, minimo 10, massimo 40.
Gli input fuori range vengono limitati; NaN e ±∞ (non dovrebbero mai
arrivare) danno il minimo.

| CPU | 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 | 100 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| FPS | 10 | 10 | 11 | 13 | 15 | 18 | 21 | 25 | 29 | 34 | 40 |

Motivazione (decisione approvata dopo M1.0):

- sotto ~10 FPS il runner non è visivamente soddisfacente (test manuale);
- 10–40 FPS validati manualmente, 40 resta utile come massimo;
- il costo degli aggiornamenti tray cresce linearmente con gli FPS
  (M1.0: ≈ 0,42% di core per fps tra AQTRunCat, plasmashell e kwin);
- la curva quadratica riserva gli FPS più costosi ai carichi alti: a 50% di
  CPU il gatto va a 18 fps, invece dei 20 della curva lineare di RunCat Neo.

Animazione (`RunnerTray::setFps`): cambia solo l'intervallo del timer
(`round(1000/fps)` ms). L'indice del frame corrente resta invariato, quindi il
ciclo continua dal frame mostrato, senza tornare a 0 e senza icona vuota. Le
`QIcon` vengono costruite una sola volta (e ricostruite solo al cambio di
palette); a ogni tick si scambia soltanto l'icona.

## 6. Runtime test

Ambiente: lo stesso di M1.0 (Arch, kernel 7.2.4-zen2, Plasma 6.7.5 Wayland,
Qt 6.11.2, i7-11390H 4 core / 8 thread). Rumore di fondo reale:
`clamd` occupa ~100% di un core (12,5% della macchina) e c'è un desktop in uso.
Per questo l'"idle" del sistema è ~19%, non ~2%.

Procedura (`spike/m1-tray/tools/cpu_runtime_test.sh`): AQTRunCat con
`--verbose`. Il carico viene generato con:

```bash
for _ in $(seq N); do timeout <secondi> yes > /dev/null & done
```

Le fasi sono: idle 30 s → metà carico (4 × `yes`) 30 s → carico pieno
(8 × `yes`) 40 s → recovery 45 s. Ogni `yes` è limitato da `timeout`. A fine
test: 0 processi `yes` residui, AQTRunCat uscito con codice 0 su SIGTERM. In
parallelo, `dbus-monitor` conta i segnali `NewIcon` dell'item SNI: è una
misura indipendente della velocità reale del runner.

Risultato con i parametri finali (esecuzione 3; statistiche a regime, esclusi
i 6 s dopo ogni transizione):

| Fase | CPU grezza media (min–max) | CPU smoothed | FPS applicati | Cambi FPS | `NewIcon`/s misurati |
|---|---|---|---|---:|---:|
| idle | 19,2% (17,3–28,9) | 18,0–22,5% | 11 | 0 | 10,96 |
| metà carico | 74,3% (70,8–83,3) | 71,9–76,4% | 26, 27 | 1 | 26,25 |
| carico pieno | 99,9% (99,7–100) | 99,1–100% | 39, 40 | 1 | 39,81 |
| recovery | 21,9% (17,5–52,3) | 18,6–34,3% | 11, 12, 14 | 3 | 11,49 |

Tempi di reazione: metà carico ≥ 20 fps in 3,8 s. Carico pieno ≥ 35 fps in
3,8 s, 40 fps in 9,8 s. Fine carico ≤ 15 fps in 2,8 s, ≤ 12 fps in 4,8 s. Fra
le esecuzioni varia di ~1 s, a seconda di come cade l'inizio del carico
rispetto al campione.

- **Idle**: il gatto si stabilizza a 11 fps (19% di CPU reale per via di
  clamd), nella parte bassa e piatta della curva, senza nessun cambio.
- **Carico**: la CPU grezza sale subito, quella smoothed la segue in pochi
  secondi, gli FPS salgono a 26 e poi a 40. I `NewIcon`/s misurati su D-Bus
  confermano che il gatto accelera davvero, non solo nel log.
- **Recovery**: la CPU scende in 1 s, la media converge, il gatto torna a
  11–12 fps. Nella recovery dell'esecuzione 3 un picco reale di fondo
  (52% grezzo) ha portato brevemente a 14 fps: comportamento corretto.
- Nella "metà carico" la CPU è ~74%, non 50%: 4 `yes` su 8 thread (4 core con
  SMT) più il ~19% di fondo.
- Prima della taratura (esecuzione 2, salto immediato = 2) la metà carico
  oscillava 26↔28 per 7 volte in 24 s; vedi §4.

Costo di AQTRunCat a riposo (11 fps): 0,40% di un core, 50 MB RSS (il
platform theme KDE carica librerie KF6 a runtime, come in M1.0).

## 7. Diagnostica

- `aqtruncat --verbose` logga a ogni campione (categoria `aqtruncat.speed`):
  `cpu raw= 74.4% smoothed= 73.5% target=26 fps applied=26 fps`. Si può fare
  lo stesso senza flag con `QT_LOGGING_RULES="aqtruncat.*.debug=true"`.
- Senza `--verbose` non c'è output periodico. L'unico messaggio possibile è il
  warning, una volta per episodio, se `/proc/stat` non è leggibile.
- **Dove finisce il log**: questo Qt invia i messaggi a **journald** quando
  stderr non è un terminale (es. avvio dal desktop o da script). Si leggono con
  `journalctl --user _COMM=aqtruncat`; per averli su stderr si usa
  `QT_FORCE_STDERR_LOGGING=1`. Al primo test il log era vuoto proprio per
  questo motivo.
- `--fps N` (1–120) è una modalità **diagnostica**: anima a frame rate fisso e
  non legge la CPU.

## 8. Known limitations (osservate)

1. Un carico che è quasi solo iowait non accelera il gatto (iowait è idle per
   scelta, §2). Non l'ho riprodotto con un test di I/O dedicato: deriva dalla
   semantica e la documento come conseguenza attesa.
2. La CPU di fondo della macchina di test (~19%, clamd in crash-loop) tiene il
   gatto a 11 fps invece di 10 in "idle". È un comportamento corretto, non un
   difetto, ma significa che il minimo assoluto non è stato osservato dal vivo
   su questa macchina.
3. Fra 1 e 2 fps di differenza il cambio arriva con circa 3 s di ritardo, per
   via del FpsStabilizer. Esempio: l'ultimo assestamento da 12 a 11 fps dopo
   un carico.
4. Il log di `--verbose` va su journald se non c'è un terminale (§7). Non è un
   difetto, ma all'inizio sorprende.
5. Restano validi i limiti tray di M1.0 (runner largo rimpicciolito, costo per
   frame in plasmashell/kwin): a 40 fps il desktop spende ≈ 17% di un core,
   per scelta solo sotto carico pieno.

## 9. Decisioni aperte / note per milestone successive

- **iowait**: confermare che va trattato come idle (scelta attuale, allineata a
  htop) e non come attivo.
- **Slow-down mode** (requisiti §7.3, M5): la formula upstream
  `0.5 × (21 − v)` è legata alla vecchia curva lineare. Con la nuova curva
  bisogna decidere che cosa significa "preservare il comportamento upstream"
  (ad esempio la curva specchiata 40→10 fps).
- I parametri di smoothing sono tarati su questa macchina e su un test di
  pochi minuti. Un test umano prolungato potrebbe suggerire di ritoccarli; si
  cambiano in un solo punto ciascuno (§4).
- Annotato per M3: `CpuTimes` contiene già tutti i campi, quindi aggiungere
  le percentuali user/system/iowait per la dashboard è una piccola estensione
  di `cpuBusyBetween` (oggi espone solo il totale).
