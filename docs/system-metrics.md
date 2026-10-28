# AQTRunCat — M3 Core Metrics + CLI

> Obiettivo: un metrics layer Linux-native per CPU, memoria, rete, storage e
> attività disco, riusabile da runner, CLI e dalla futura Dashboard/Metrics
> Bar. **Raggiunto.**
> Data: 2026-10-04. Ambiente: Arch Linux, kernel 7.2.4-zen2, Plasma 6.7.5
> Wayland, Qt 6.11.2, i7-11390H (8 thread), due NVMe, swap zram, root ext4,
> home ecryptfs su ext4, Wi-Fi.

## 1. Architettura

```text
/proc/stat  /proc/meminfo  /proc/net/dev  /proc/diskstats   statvfs   /sys/class/net  /sys/block
     │            │              │               │              │             │             │
     └────────────┴──────────────┴───────────────┴──────────────┴─────────────┴─────────────┘
                                 SystemMetricsSampler::sample(t)   (src/metrics, C++ puro)
                                              │
                                    SystemMetricsSnapshot
                    ┌──────────────────────────┼──────────────────────────┐
              aqtruncat metrics          futura Dashboard          futura Metrics Bar
              (src/cli/metricscommand)
```

| File | Ruolo |
|---|---|
| `src/metrics/metricstatus.h` | modello di disponibilità (`Ok`, `Pending`, `Unavailable`, `Error`) |
| `src/metrics/counterrates.h` | da contatori cumulativi a rate (rete e disco), Δbyte / tempo reale |
| `src/metrics/memory.*` | parsing di `/proc/meminfo`, semantica RAM e swap |
| `src/metrics/network.*` | parsing di `/proc/net/dev`, classificazione delle interfacce, policy di aggregazione |
| `src/metrics/diskio.*` | parsing di `/proc/diskstats`, classificazione dei block device, policy |
| `src/metrics/storage.*` | capacità del filesystem (`statvfs`), semantica di `df` |
| `src/metrics/systemmetrics.*` | snapshot tipizzato e sampler unico |
| `src/metrics/metricsformat.*` | formattazione (unità binarie, indipendente dal locale) |
| `src/metrics/textparse.*` | helper di parsing e lettura file (interni) |
| `src/cli/metricscommand.*` | `aqtruncat metrics [--watch]`: solo presentazione |

Il metrics layer è C++20 puro: non dipende da Qt, KDE o Wayland. Usa solo
`/proc`, `/sys` e POSIX `statvfs`. Il modello conserva valori raw (byte,
byte/s, percentuali); la CLI è soltanto un consumer.

## 2. Snapshot

Uno snapshot tipizzato per area, senza un tipo "metrica generica":

| Sezione | Contenuto (se `status` è Ok) |
|---|---|
| `CpuMetrics` | `usagePercent`, `logicalCpus` |
| `MemoryMetrics` | `MemoryInfo`: total, available, used, usage %, swap separato |
| `NetworkMetrics` | download/upload byte/s aggregati + statistiche per interfaccia (tipo, se contata, rate, reset) |
| `StorageMetrics` (una per path) | total, used, available, usage % |
| `DiskIoMetrics` | read/write byte/s aggregati + statistiche per device |

Ogni sezione ha il suo `MetricStatus`: una sorgente rotta o assente non
nasconde le altre (testato).

## 3. CPU

La temperatura CPU nativa (`cpu.temperature`, introdotta in 2.3.2) è
documentata separatamente in [cpu-temperature.md](cpu-temperature.md); è una
lettura hwmon distinta dalla percentuale di utilizzo descritta qui.

- **Una sola implementazione.** Il metrics layer usa `parseProcStat`,
  `cpuBusyBetween` e `CpuSampler` di `core/cpustat`, gli stessi del runner.
  Semantica invariata: attivi user, nice, system, irq, softirq, steal; idle
  e iowait inattivi; guest non contati due volte. Un test confronta il valore
  dello snapshot con `cpuBusyBetween` sugli stessi campioni.
- Il metrics layer espone `usagePercent` **grezzo**, non smussato. Lo
  smoothing (EMA) e la stabilizzazione restano nella pipeline del runner.
- `logicalCpus`: numero di righe `cpuN` di `/proc/stat`
  (`countLogicalCpus`, unica aggiunta a `core/cpustat`, che non tocca la
  semantica esistente). L'uso per singolo core non è stato implementato
  (non richiesto).
- **Runner invariato.** `aqtruncat run` usa ancora `CpuMonitor` (M1), che
  legge la sola prima riga di `/proc/stat`. In M3 l'app tray non avvia il
  metrics sampler, quindi nello stesso processo non ci sono letture doppie.
  Piano per M4 (Dashboard): un solo sampler nell'app, con `CpuMonitor` che
  prende `cpu.usagePercent` dallo snapshot invece di leggere `/proc/stat`
  da sé (calcolo identico, cambia solo chi legge).

## 4. Memoria (`/proc/meminfo`)

```text
total     = MemTotal
available = MemAvailable
used      = total - available
usage %   = used / total
```

- La page cache che il kernel può liberare **non** conta come usata: è la
  semantica di `MemAvailable` (kernel ≥ 3.14) e della colonna "used" di
  `free` recente.
- **Fallback senza MemAvailable**: available = MemFree + Buffers + Cached +
  SReclaimable − Shmem, limitato a [MemFree, MemTotal]. In questo caso
  `availableIsEstimate` è impostato e la CLI lo segnala ("estimated").
- **Swap separata**: `SwapTotal`, used = SwapTotal − SwapFree, percentuale
  propria; `swapPresent` è falso se SwapTotal è 0 o assente. Non viene mai
  sommata alla RAM.
- Errori: MemTotal mancante o zero, MemFree mancante, valore non numerico in
  un campo usato → `Error`. I campi non usati non vengono validati.

## 5. Rete (`/proc/net/dev` + `/sys/class/net`)

```text
rate = (bytes_now - bytes_prev) / (t_now - t_prev)      t da clock monotono
```

Il tempo è quello **realmente trascorso**, misurato con `steady_clock`, mai
l'intervallo nominale (differenza voluta rispetto a RunCat Neo).

### Policy delle interfacce

| Tipo | Regola | Esempi | Nell'aggregato |
|---|---|---|---|
| Loopback | nome `lo` | `lo` | no |
| Physical | esiste `/sys/class/net/<if>/device` | `enp3s0`, `wlp2s0`, `wlan0`, tethering USB, WWAN | **sì** |
| Virtual | tutto il resto | `tun*`, `wg*`, `ppp*`, `docker*`, `veth*`, `br-*`, `virbr*`, bond/VLAN | no |

Nessun nome viene assunto: la classificazione dipende solo dalla presenza
del device in sysfs.

Motivazione: il traffico di una VPN o di un container attraversa comunque
un'interfaccia fisica. Contare anche il tunnel, il bridge o il veth
conterebbe gli stessi dati due volte. Contare solo l'hardware dà "quanto
traffico entra ed esce da questa macchina".

Rischi e limiti documentati:

- con una VPN il valore misurato è il traffico **cifrato** sull'interfaccia
  fisica (overhead incluso), non il payload del tunnel;
- connessioni il cui unico link è virtuale (`ppp0` di un vecchio modem,
  bond/VLAN sopra NIC fisiche) contano tramite le NIC fisiche sottostanti;
  dove non esistono, l'aggregato risulta `Unavailable` ("no physical network
  interface"), mai 0;
- il traffico tra container e host che resta sui bridge locali non viene
  contato (non esce dalla macchina).

Ogni interfaccia, contata o no, mantiene le proprie statistiche nello
snapshot (`interfaces`): una UI futura potrà offrire una selezione diversa.

### Anomalie

| Caso | Comportamento |
|---|---|
| primo campione | `Pending`, nessun rate (non 0) |
| delta zero | rate 0 (`Ok`): zero vero |
| tempo trascorso zero, negativo o NaN | `Pending`, la baseline precedente resta valida |
| contatore che diminuisce (reset, driver ricaricato, wrap a 32 bit) | nessun rate per quell'interfaccia in quell'intervallo (`counterReset`), mai valori enormi; dall'intervallo dopo si riparte dalla nuova baseline |
| interfaccia che compare | nessun rate fino al campione successivo |
| interfaccia che scompare | semplicemente assente |
| solo reset o comparse | `Pending` |
| nessuna interfaccia fisica | `Unavailable` |

## 6. Storage (capacità del filesystem)

`statvfs` sul path (per M3, `/`). Semantica di `df`:

```text
total     = f_blocks × f_frsize
used      = (f_blocks - f_bfree) × f_frsize
available = f_bavail × f_frsize                 (spazio scrivibile da un utente normale)
usage %   = used / (used + available)           (come la colonna Use% di df)
```

- I blocchi riservati a root (`f_bfree − f_bavail`, tipicamente il 5% su
  ext4) non sono né usati né disponibili, quindi used + available < total.
  La percentuale si riferisce allo spazio che l'utente può davvero avere.
  Esempio verificato: root ext4 a 61,6% (df lo arrotonda a 62%).
- Statistiche incoerenti (frsize o blocks a 0, free > total, avail > free,
  overflow) → `Error`; path inesistente → `Unavailable`.
- Il sampler accetta una lista di path (`MetricsSources::storagePaths`):
  aggiungere `/home` o altri mount point non richiede modifiche al modello.
  `statvfs` è iniettabile per i test.
- Caveat noto: su btrfs (e in generale su filesystem copy-on-write o
  compressi) `statvfs` è un'approssimazione. AQTRunCat mostra ciò che
  mostrerebbe `df`.

## 7. Attività disco (`/proc/diskstats` + `/sys/block`)

Rate di lettura e scrittura in byte/s, con la stessa logica di rete: Δ e
tempo reale, gestione di reset, comparse e scomparse. I settori di
`/proc/diskstats` sono sempre da 512 byte, qualunque sia il block size del
device (documentazione del kernel `iostats`).

### Policy dei device

| Tipo | Regola | Esempi | Nell'aggregato |
|---|---|---|---|
| PhysicalDisk | `/sys/block/<dev>/device` esiste e `hidden` ≠ 1 | `nvme0n1`, `sda`, `mmcblk0`, `vda` (virtio), dischi USB | **sì** |
| Other | partizioni (non sono voci di `/sys/block`), dispositivi virtuali | `nvme0n1p7`, `dm-*` (LVM/LUKS), `md*`, `loop*`, `zram*`, path NVMe multipath nascosti | no |

Motivazione: ogni byte che arriva all'hardware viene contato una sola volta,
sul disco. Le partizioni e i device impilati (dm-crypt su partizione, LVM,
RAID md) riporterebbero lo stesso I/O una seconda o terza volta, mentre
zram e loop non sono dischi (zram è memoria compressa: sulla macchina di
test la swap è zram). È la definizione di "disco" di `lsblk TYPE=disk` per
l'hardware ed è stabile su tutte le distribuzioni.

Limiti:

- I/O su storage di rete (NFS, SMB) o su FUSE non è I/O su disco locale e
  non appare;
- con RAID md si contano i dischi membri (scrittura RAID1 = 2× i byte
  logici), coerente con "byte verso l'hardware";
- in un container senza i link `device` in `/sys/block` l'aggregato è
  `Unavailable`, non 0;
- le partizioni di boot eMMC (`mmcblk0boot*`) risultano dischi a sé, di
  solito senza I/O.

Nota di sviluppo: i test hanno trovato un bug reale prima del runtime.
`hidden` in sysfs termina con `\n` e il trim non lo rimuoveva, quindi un path
NVMe multipath nascosto sarebbe stato contato come disco.

## 8. Sampling

- `SystemMetricsSampler::sample(t)` è l'unico punto che legge le sorgenti:
  4 file in `/proc`, i `statvfs` configurati, controlli `/sys` per la
  classificazione, più un `hidden` per ogni disco fisico.
- Il tempo viene passato al sampler (clock monotono in produzione, valori
  arbitrari nei test). Le rate usano sempre il tempo misurato tra due
  campioni.
- La CLI campiona ogni ~1 s con cadenza fissa. Se resta indietro (es. dopo
  una sospensione) riparte senza raffiche di recupero; le rate restano
  corrette perché usano il tempo realmente trascorso.
- Nessun timer Qt e nessuna dipendenza dalla UI: un consumer futuro (la
  Dashboard) userà un solo `QTimer` che chiama `sample()` e distribuisce lo
  snapshot.

## 9. Modello di errore

| Stato | Significato | Esempio |
|---|---|---|
| Ok | valori validi | |
| Pending | una rate ha bisogno di un altro campione | primo campione, reset, tempo non avanzato |
| Unavailable | la sorgente non esiste qui | file `/proc` assente, nessuna interfaccia fisica, path di storage inesistente |
| Error | la sorgente esiste ma non è interpretabile | `/proc/meminfo` malformato, `statvfs` incoerente |

Valori assenti = `std::optional` vuoto, mai 0. La CLI mostra
`n/a: <motivo>` oppure `waiting: <motivo>`. Ogni sezione è indipendente; il
runner non dipende dal metrics layer.

## 10. Formattazione

- Unità binarie: `0 B`, `1023 B`, `1.0 KiB` (1024 B), …, `TiB`, `PiB`; una
  cifra decimale.
- Rate: stessa scala con `/s`.
- Percentuali: `23.4 %`.
- Arrotondamenti ai confini: 1023,6 B diventa `1.0 KiB` (non `1024 B`);
  1 MiB − 1 B diventa `1.0 MiB` (non `1024.0 KiB`).
- Valore non disponibile: `n/a`.
- **Indipendente dal locale** (`std::to_chars`): la CLI produce `23.4 %`
  anche con locale italiano. C'è un test con locale it_IT o de_DE attivo.
- Il modello non contiene mai stringhe formattate: un futuro output JSON
  serializzerà direttamente lo snapshot.

## 11. CLI

```text
aqtruncat metrics           report di uno snapshot (dopo ~1 s: le rate hanno bisogno di due campioni)
aqtruncat metrics --watch   una riga al secondo fino a Ctrl+C (exit 0)
aqtruncat metrics --help
```

Esempio reale (questa macchina):

```text
CPU
  Usage:      16.6 %  (8 logical CPUs)
Memory
  Used:       14.4 GiB / 31.1 GiB  (46.4 %)
  Available:  16.7 GiB
Swap
  Used:       171.1 MiB / 15.9 GiB  (1.1 %)
Network (wlan0)
  Download:   563 B/s
  Upload:     401 B/s
Storage /
  Used:       114.5 GiB / 195.8 GiB  (61.6 %)
  Available:  71.3 GiB
Disk I/O (nvme0n1, nvme1n1)
  Read:       0 B/s
  Write:      0 B/s
```

```text
13:14:13  CPU 16.0 % | Mem 14.4 GiB (46.2 %) | Net down 30.0 MiB/s up 55.7 KiB/s | Disk read 0 B/s write 0 B/s
```

- Exit code: 0; 1 se nessuna metrica è disponibile; 2 per un errore d'uso
  (stessa tabella di M2.1). Il comando non crea `QCoreApplication`, quindi
  non tocca il locale di processo.
- JSON non implementato (non richiesto). Il modello raw lo rende un
  formatter aggiuntivo.

## 12. Test

17 suite, **299 casi**, tutti verdi (225 di M1/M2 invariati + 74 nuovi):

| Suite | Casi | Copertura |
|---|---:|---|
| memory | 16 | meminfo reale, MemAvailable, fallback senza, clamp, swap presente/assente, total zero/assente, valori malformati, campi ignoti |
| network | 15 | parsing, più interfacce, loopback e virtuali escluse, nomi senza spazio dopo `:`, primo campione, delta, tempo reale (2,5 s), delta zero, tempo zero/negativo/NaN, reset senza valori enormi, comparsa/scomparsa, aggregato |
| storage | 11 | semantica df con dati reali, blocchi riservati, pieno/vuoto, statistiche invalide (5 casi), path inesistente |
| diskio | 11 | settori → byte, formati vecchi/nuovi, malformati, classificazione (NVMe, SATA, eMMC, multipath nascosto, dm, md, loop, zram, partizioni), rate, partizioni e zram non contate due volte, reset, comparsa/scomparsa, nessun disco fisico |
| metricsformat | 14 | 0 B, 1023 B, 1024 B, KiB, MiB, GiB, TiB, max uint64, rate, arrotondamenti ai confini, percentuali, n/a, locale con virgola |
| systemmetrics | 7 | integrazione fixture → sampler → snapshot → report e riga watch; CPU identica a `cpuBusyBetween`; sorgenti mancanti = n/a, mai 0; una sorgente rotta non nasconde le altre; parsing di `metrics` |

Nessun test legge il `/proc` reale: si usano fixture testuali e directory
`/sys` simulate in directory temporanee.

## 13. Validazione runtime (macchina reale)

Confronto con fonti indipendenti, usate solo per validare:

| Metrica | AQTRunCat | Riferimento |
|---|---|---|
| RAM | used 14.1 GiB / 31.1 GiB (45.5%), available 17.0 GiB | `free -b`: used 14.13 GiB, available 16.95 GiB, total − available = 45.5% |
| Swap | 257.6 MiB / 15.9 GiB | `free -b`: 257.6 MiB / 15.9 GiB |
| Storage `/` | 114.5 GiB / 195.8 GiB, available 71.3 GiB, 61.6% | `df -B1 /`: Used 122 968 162 304, Avail 76 519 202 816, Use% 62% (61.6 non arrotondato) |
| Interfacce | contata `wlan0`; escluse `lo`, `docker0` | `/sys/class/net` |
| Dischi | contati `nvme0n1`, `nvme1n1`; esclusi partizioni e `zram0` (swap) | `lsblk` |

Rate (idle → attività → idle), confrontate con un campionatore indipendente
di `/proc/net/dev` e `/proc/diskstats`:

| Attività | Misura indipendente | AQTRunCat (integrale delle rate) |
|---|---:|---:|
| download (Cloudflare speed test, 50 MB) | 50.0 MiB | 49.3 MiB, picco 49.3 MiB/s |
| upload (10 MB) | 9.9 MiB | 9.9 MiB |
| scrittura disco (256 MiB, `fsync`) | 256.2 MiB | 256.2 MiB |
| lettura diretta (`O_DIRECT`, 193 MiB) | 193.8 MiB | 193.8 MiB |
| rete a riposo | ~0.1 MiB in 4 s | ~0.1 MiB |

CPU: 15–30% a riposo (rumore di fondo della macchina), **100%** con un
`yes` per thread, poi di nuovo valori bassi. `metrics --watch` interrotto con
Ctrl+C: exit 0.

`tools/test-metrics-runtime.sh` (con download da 30 MB) mostra le fasi
nettamente: CPU 15% → 100% → 15%; rete ~5 KiB/s → 30.0 MiB/s → riposo;
disco ~0 → 256.2 MiB/s → riposo. Nessun processo o file residuo, anche con
Ctrl+C.

Runner invariato:

- `aqtruncat` ~11 fps a riposo;
- `tools/test-cpu-fps-curve.sh`: `10 10 11 … 40 … 10 10`;
- `tools/cycle-runners.sh`: OK;
- `tools/test-cpu-runtime.sh`: metà carico 23–25 fps, pieno 39–40,
  recovery 11–13. Un'escursione a 23 fps in idle segue un picco reale della
  CPU di sistema (grezzo 65–79% per 3 s, verificato nel log) e rientra a
  11 fps.

## 14. Performance

| Misura | Valore |
|---|---|
| snapshot completo (`sample()`, media su 2000) | **108 µs** (105 µs senza `statvfs`) |
| syscall per snapshot | ~50: 6 `openat`, 18 `read`, 14 `stat` (classificazione), 1 `statfs` |
| `aqtruncat metrics --watch`, 60 s | **0,05%** di un core, RSS 18 MB |

A 1 Hz il sampling costa circa lo 0,01% di un core. La classificazione di
interfacce e dischi viene rifatta a ogni campione (qualche `stat` sotto
`/sys`). Con cache si risparmierebbero ~14 syscall al secondo: non serve, e
così il sampler segue automaticamente dispositivi che compaiono e
scompaiono.

## 15. Strumenti

- **Nuovo** `tools/test-metrics-runtime.sh`: fasi baseline → carico CPU →
  download (solo con `--download-url`, per non contattare servizi esterni di
  default) → scrittura disco (file temporaneo con `fsync`, in
  `--disk-dir`, default la cache XDG) → recovery, con una riga di metriche
  al secondo e marcatori di fase. Cleanup e Ctrl+C sicuri (verificato via
  pseudo-terminale: exit 130, nessun residuo); nessun `pkill`, nessun path
  personale.
- Gli strumenti M1/M2 funzionano invariati.

## 16. Limitazioni note

1. Storage: solo `/` nella CLI. Il modello supporta più path, ma serve una
   decisione su quali mostrare (§17).
2. Rete: con una VPN si misura il traffico cifrato sulla NIC; link solo
   virtuali (vecchi modem `ppp0`) senza NIC fisica risultano `Unavailable`.
3. Disco: niente storage di rete o FUSE; RAID md conta i byte verso ogni
   membro.
4. `statvfs` su btrfs o filesystem compressi è approssimato, come `df`.
5. Uso CPU per singolo core non implementato.
6. `aqtruncat metrics` impiega ~1 s (servono due campioni per le rate).

## 17. Decisioni aperte

Nessuna bloccante. Le tre scelte con impatto pubblico seguono la pratica
standard di Linux e non le ho considerate arbitrarie:

- rete: solo interfacce con device hardware (evita il doppio conteggio con
  VPN e container);
- disco: solo dischi fisici interi (evita il doppio conteggio di partizioni e
  dm);
- storage: semantica di `df`.

Restano da decidere più avanti:

- **mount point mostrati** oltre a `/`. Ad esempio `/home` quando è un
  filesystem separato, come su questa macchina, dove è più grande della root.
  Opzioni: solo `/` (oggi); `/` più `$HOME` se su un filesystem diverso; lista
  configurabile in M5. Raccomandazione: `/` più `$HOME` in M4, quando la
  Dashboard mostrerà lo storage;
- **selezione manuale delle interfacce** (es. includere un `ppp0`): il
  modello conserva già i dati per interfaccia, la UI andrà in M5;
- **sampler unico nell'app** quando arriverà la Dashboard (M4), con
  `CpuMonitor` che consuma lo snapshot (§3).
