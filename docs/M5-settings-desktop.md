# AQTRunCat — M5 Settings and Desktop Integration

M5 aggiunge configurazione tipizzata e persistente, una finestra Settings Qt
Widgets, selezione e anteprima del runner, modalità Slow-down, override della
rete e autostart XDG. Dashboard, sampler condiviso e CLI M4 restano separati
dalla finestra di configurazione.

## 1. Architettura e ownership

`AppSettings` è un `QObject` indipendente dai widget e rappresenta lo stato
applicativo tipizzato:

```text
SettingsWindow --immediate apply--> AppSettings --QSettings/INI--> XDG config
                                      |       |          |
                                      v       v          v
                                  runner    metrics   autostart
```

`runTrayApplication` possiede `AppSettings`, il singolo `MetricSampler`, il
runner e le finestre. `SettingsWindow` è creata una sola volta, solo alla prima
richiesta dal menu tray, e poi riutilizzata. Chiuderla la nasconde; non ferma
runner, sampler o Dashboard. Dashboard e Settings possono restare aperte
contemporaneamente e non sono proprietarie l'una dell'altra.

La UI chiama soltanto setter tipizzati. Ogni setter valida, sincronizza subito
la configurazione e notifica i consumer con signal/slot. Nessun componente
legge checkbox o combo box come sorgente di configurazione.

## 2. Persistenza, default e precedence

Il backend è `QSettings` in formato INI, scope utente, organization e
application `AQTRunCat`. Su Linux il file osservato è:

```text
$XDG_CONFIG_HOME/AQTRunCat/AQTRunCat.ini
```

Quando `XDG_CONFIG_HOME` non è definita, Qt usa il normale fallback XDG
(`~/.config`). I runner utente restano separati sotto `XDG_DATA_HOME`.

Default di una nuova installazione:

- runner `cat`;
- velocità `Normal`;
- rete `Automatic` (policy hardware-backed M3);
- refresh Dashboard ogni secondo;
- autostart disattivato.

La precedence di avvio è centralizzata in `resolveRunPreferences`:

```text
override CLI esplicito > setting persistente > default compilato
```

`--runner` vale per la sola invocazione. `--fps` impone il frame rate e ignora
la modalità CPU per il runner, senza cambiare Settings. I comandi `metrics` e
`runner ...` non istanziano widget e conservano il proprio contratto CLI.

Valori enum sconosciuti, refresh non ammesso e id persistiti non sicuri tornano
ai default. Un runner salvato che non esiste più ricade su quello predefinito e
la preferenza viene riparata. Se l'id esiste ma il runner è temporaneamente
invalido, l'app usa il fallback senza cancellare la preferenza. Un `--runner`
esplicito non valido resta invece un errore, come in M2.

`Restore Defaults` azzera soltanto le preferenze M5: non elimina runner o altri
dati utente.

## 3. Runner e preview

La combo Runner è popolata tramite `RunnerService::list()`, quindi rispetta
runner bundled/user e precedence M2. Un cambio carica il runner con lo stesso
servizio e aggiorna immediatamente `TrayPresenter` e la sequenza del singolo
`RunnerAnimator`, senza riavviare l'app.

`RunnerPreviewWidget` è soltanto un renderer Widgets. Usa frame già decodificati
e un proprio timer di animazione fisso a 20 FPS, non un secondo tray né un
sampler di sistema. Preserva aspect ratio e HiDPI; i runner monocromatici usano
la stessa tinta `QPalette::WindowText` della tray, quelli a colori restano
inalterati. Il timer si sospende quando la preview non è visibile e non legge
file a ogni frame.

## 4. Modalità di velocità

La pipeline desktop resta:

```text
MetricSampler raw CPU (1 Hz) -> EMA M1 (alpha 0.4)
                              -> curva selezionata
                              -> FpsStabilizer M1
                              -> RunnerAnimator
```

`Normal` conserva `round(10 + 30*(cpu/100)^2)`. `Slow-down` applica soltanto la
trasformazione speculare già decisa:

```text
round(10 + 30*(1 - cpu/100)^2)
```

Entrambe sono limitate a 10–40 FPS. Lo switch è immediato e riusa ultimo raw,
ultimo EMA e lo stesso stabilizzatore; non crea timer o sampler. `--fps N`
bypassa il controller anche quando il setting salvato è Slow-down.

## 5. Rete Automatic/Manual

`NetworkSelection` appartiene al metrics layer:

- `Automatic`: identica policy M3, aggrega solo interfacce con device hardware;
- `Manual`: aggrega esattamente gli id selezionati, anche loopback, VPN,
  bridge o altre interfacce virtuali.

La Dashboard non filtra lo snapshot. Il setting aggiorna il medesimo
`SystemMetricsSampler`, così ogni consumer vede lo stesso aggregato. A ogni
cambio di modalità o insieme selezionato il tracker di rete azzera soltanto la
propria baseline: il primo rate è `Waiting`, mai uno spike ottenuto sottraendo
aggregati incompatibili.

Le scelte assenti restano nel file e appaiono come `Not currently available`.
Non contribuiscono finché assenti e tornano automaticamente a contribuire se
l'interfaccia ricompare. Zero traffico resta un rate valido pari a zero;
nessuna interfaccia manuale presente è `Unavailable`.

## 6. Refresh Dashboard e autostart

Il refresh configurabile (1, 2 o 5 secondi) è un throttle di presentazione
signal-driven tra snapshot e Dashboard. Il `MetricSampler` resta sempre a 1 Hz
per preservare EMA e semantica M1; il runner riceve ogni snapshot. Cambiare
intervallo rende immediato il prossimo aggiornamento, senza polling UI.

L'autostart crea atomicamente:

```text
$XDG_CONFIG_HOME/autostart/aqtruncat.desktop
```

con `Type=Application`, nome/commento AQTRunCat, `Exec` quotato sul binario
corrente e `Terminal=false`. Disabilitarlo rimuove il file; non viene creato un
servizio systemd e non esiste una dipendenza KDE. La desktop entry installabile
e l'icona applicativa restano parte del packaging M9, non dell'autostart M5.

## 7. UI, Wayland e validazione visiva

La finestra `Qt::Tool` usa layout, font, palette e controlli Qt di sistema;
non contiene colori hardcoded né coordinate X11. Su Plasma 6 Wayland/Breeze a
scaling 125% è risultata leggibile e stabile. Menu tray e lifecycle verificati:

- `Settings…` apre una sola istanza e la riporta in primo piano;
- chiusura e riapertura conservano lo stato;
- Dashboard e Settings convivono e si chiudono indipendentemente;
- Quit dalla tray termina l'intera applicazione;
- la preview è animata; l'apply immediato Cat/Coffee è coperto dal test Widgets
  e dalla stessa connessione runtime `AppSettings -> RunnerService`.

Evidenza ritagliata alla sola finestra: [Settings](M5-settings/settings.png).
Il posizionamento e il trattamento nel task switcher restano decisioni di KWin,
come per la Dashboard: non sono stati aggiunti hack X11.

## 8. Test e regressioni runtime

Quattro nuove suite coprono `AppSettings`/XDG/autostart/precedence, controller
desktop e fallback runner, preview, e Settings UI offscreen. Le suite M3 sono
state estese per Manual network, interfacce virtuali, sparizione/ricomparsa e
reset della baseline; curve e animator coprono Slow-down e pausa preview.

Risultato: **24 suite, 359 casi Qt Test, 0 failure**.

Gate runtime Normal dopo M5:

| fase | CPU raw osservata | FPS applicati |
|---|---:|---:|
| idle | 17.4% | 11 |
| half (4/8 worker, più carico di fondo) | 72.9% | 25–26 |
| full | 100% | 37–40 |
| recovery | 18.7% | 11 |

Il valore half è coerente con la CPU realmente misurata; non è stato corretto
artificialmente. In Slow-down, con idle reale 17–23%, sono stati osservati
29–30 FPS; sotto full load 10 FPS, con recovery attraverso la stessa EMA. I
punti nominali 0–100% sono verificati deterministicamente dai test.

Persistenza reale con profilo XDG isolato: Coffee e Slow-down sono sopravvissuti
al restart; `--runner cat --fps 20` ha usato Cat a 20 FPS senza modificare il
file; un id rimosso è ricaduto su Cat ed è stato riparato. L'autostart è stato
creato/rimosso soltanto nel profilo isolato.

Tutti i tool permanenti sono rimasti operativi. `test-metrics-runtime.sh` non ha
contattato la rete senza `--download-url` e ha ripulito il file temporaneo; il
gate CPU non ha lasciato worker.

## 9. Performance

Misure grossolane su finestre di 15 secondi nella sessione reale:

| stato | CPU processo, quota di un core | RSS |
|---|---:|---:|
| Settings chiusa dopo la prima apertura | 0.400% | 78,120 KiB |
| Dashboard visibile, Settings chiusa | 0.467% | 80,192 KiB |
| Settings visibile con preview | 1.867% | 79,500 KiB |

Il costo aggiuntivo visibile è il timer preview a 20 FPS; quando Settings viene
nascosta torna al livello del runner/sampler. Dopo la prima apertura Qt
mantiene allocazioni Widgets/catalogo, quindi l'RSS non ritorna al valore M4 di
processo mai aperto. Rimane un solo sampler di sistema.

## 10. Limitazioni e lavoro successivo

- Le interfacce sono scoperte da `/proc`/`/sys`; non vengono mostrati nomi
  NetworkManager o stato di connessione avanzato.
- Non esiste file watching del catalogo runner: una rimozione esterna viene
  gestita al successivo avvio; riaprire Settings non forza hot reload.
- L'orientamento runner non è stato aggiunto: i requirements lo qualificano
  `where applicable`, ma M5 non ha definito una semantica Linux normativa.
- Enabled metrics/presentation avanzata appartengono alle milestone Custom
  Metrics/Metrics Bar; M5 configura rete e frequenza Dashboard senza anticiparle.
- Entry `.desktop` installabile e icona propria restano M9 packaging.

M6 non è iniziata.
