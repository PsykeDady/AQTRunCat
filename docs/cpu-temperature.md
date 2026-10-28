# Temperatura CPU — v2F3.2 (2.3.2)

Il backend espone la metrica nativa `cpu.temperature` (unità API `celsius`).
Non è una Custom Metric e non esegue script o processi esterni. Il campione
tipizzato viene condiviso da `aqtruncatd`, Dashboard, Metrics Bar, CLI e
Plasmoid. L'API D-Bus resta `io.github.psykedady.AQTRunCat1`; lo schema JSON
incrementa in modo additivo da 7 a 8.

## Acquisizione e selezione

Il collector legge `/sys/class/hwmon` e riconosce i driver CPU `coretemp`,
`k10temp`, `zenpower` e `cpu_thermal`. Esamina tutti i canali `tempN_input`,
le etichette `tempN_label` quando esistono e l'identità stabile del device.
Non usa l'indice `hwmonN` come identità del sensore.

Ordine di preferenza: etichetta package/pkg, die (`Tdie`), `Tctl`, CPU
esplicitamente etichettata e infine core. Sui driver noti viene usato l'ordine
standard dei canali quando il kernel non fornisce label. Se ci sono più
package, ne viene scelto uno in modo deterministico e stabile; non si fa una
media di package o core e non si sceglie il valore più alto.

Solo se nessun canale hwmon identificato fornisce un valore leggibile, sono
considerate le zone termiche con tipo esplicito `x86_pkg_temp`, `cpu-thermal`,
`cpu_thermal`, `cpu-temp` o `cpu_temperature`. `thermal_zone0` e i tipi ACPI
generici non sono assunti essere la CPU. Le temperature sysfs in milligradi
Celsius sono convertite in °C; letture non numeriche o fuori dall'intervallo
plausibile da -40 a 150 °C sono rifiutate.

Il backend legge il canale selezionato durante il normale campionamento
metriche (una volta al secondo). La discovery viene ripetuta ogni cinque
minuti, e immediatamente se il canale scompare o non è più leggibile. Questo
evita la scansione completa e l'avvio di processi a ogni campione.

## Stato e interfacce

- Nessun sensore CPU identificabile: `Unavailable`, nessun numero inventato.
- Sensore identificato ma nessuna lettura valida: `Error`.
- Dashboard: `54.3 °C` nella sezione CPU, con l'etichetta del sensore.
- Metrics Bar: selezione e ordine condivisi; valore compatto in gradi, senza
  barra percentuale o soglia termica universale.
- Plasmoid: la riga arriva dal backend come metrica built-in, senza logica
  hardware o raccolta duplicata in QML.
- CLI: `aqtruncat metrics` e `aqtruncatctl metrics --category builtin`.

Non sono aggiunte impostazioni specifiche: la Dashboard mostra le metriche
built-in secondo le convenzioni esistenti; la Metrics Bar resta opt-in e
configurabile nella lista condivisa.

## Dipendenze e limiti

`lm_sensors`/`sensors -j` non è invocato in questa versione: l'accesso hwmon
diretto copre i driver implementati senza dipendenza opzionale o subprocessi
periodici. `sensors-detect` non viene mai avviato. Il supporto richiede che il
kernel/driver esponga un'interfaccia hwmon riconosciuta o una zona termica con
tipo CPU esplicito; hardware esposto solo da altri driver resta `Unavailable`.
Non sono implementate letture per-core, GPU o ventole.

Test riproducibili con sysfs sintetico: `tst_cputemperature`, senza dipendere
dai sensori o dalla distribuzione dell'host.
