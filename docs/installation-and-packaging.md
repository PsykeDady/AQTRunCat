# AQTRunCat — M9 Packaging, Installation & Release Readiness

M9 rende AQTRunCat installabile, disinstallabile e distribuibile secondo gli
standard Linux/XDG, senza cambiare l'architettura validata in M8 (Qt ≥ 6.5,
tray Qt/SNI generico, nessuna dipendenza KDE, nessun Plasmoid).

Stati: **SUPPORTED**, **SUPPORTED WITH LIMITATIONS**, **DEFERRED**, **NOT
TESTED**, **NOT APPLICABLE**. Evidenze: **[OSS]** eseguito/misurato,
**[SRC]** letto da codice/documentazione, **[IPO]** inferenza.

> **Nota F4.1 (2026-10-21).** Da F4.1 ogni installazione contiene anche il
> backend `bin/aqtruncatd`, la CLI `bin/aqtruncatctl` e il file di
> attivazione D-Bus `share/dbus-1/services/io.github.psykedady.AQTRunCat.service`
> (con `Exec` = l'`aqtruncatd` installato). `install.sh` chiude anche
> l'`aqtruncatd` della stessa installazione (percorso esatto, SIGTERM) e chiede
> `ReloadConfig` al bus di sessione dopo l'installazione e la
> disinstallazione. I pacchetti dipendono da D-Bus (PKGBUILD `dbus`, Debian
> `default-dbus-session-bus | dbus-session-bus`, RPM `Requires: dbus`). Il test
> `backendipc` è registrato solo se c'è `dbus-run-session`. I pacchetti non
> sono stati ricostruiti nei container dopo F4.1. Dettagli:
> [backend-dbus-api-cli.md](backend-dbus-api-cli.md) §10.

> **Nota v2F2 (2.2.0).** Il pacchetto installa anche
> `aqtruncat-settings.desktop` e il servizio D-Bus
> `io.github.psykedady.AQTRunCat.Settings.service`. Il launcher esegue
> `aqtruncat settings`; tray e Plasmoid attivano lo stesso processo tramite
> D-Bus. `install.sh` sostituisce i percorsi di esecuzione con quelli
> dell'installazione system-wide o locale e li registra nel manifest, così
> upgrade e disinstallazione seguono il meccanismo esistente.

> **Nota F4.2 (2026-10-21).** Da F4.2 c'è anche il widget KDE Plasma 6
> (`share/plasma/plasmoids/io.github.psykedady.aqtruncat/`, solo QML) e i
> runner bundled come file (`share/aqtruncat/runners/`). `install.sh` installa
> il widget quando trova Plasma 6 (`--plasmoid` / `--no-plasmoid` per
> deciderlo), non tocca mai pannelli né plasmashell, e la disinstallazione
> rimuove anche le cartelle vuote del widget e dei runner. I pacchetti lo
> includono con una dipendenza debole da `plasma-workspace` (optdepends /
> Suggests). Nessuna dipendenza KDE è stata aggiunta al resto di AQTRunCat.
> Dettagli: [plasma-widget.md](plasma-widget.md) §9–10.

## 1. Riepilogo

| Canale | Stato | Evidenza |
|---|---|---|
| `cmake --install` (prefix, DESTDIR) | SUPPORTED | [OSS] staging verificato, test dell'installer |
| `install.sh` system-wide | SUPPORTED | [OSS] container Arch, Fedora 44, Debian 13, Ubuntu 26.04: root, utente+sudo, `sudo ./install.sh` |
| `install.sh --local` | SUPPORTED | [OSS] host (HOME temporanea) e 4 container; suite automatica |
| Pacchetto Arch (`packaging/arch/PKGBUILD`) | SUPPORTED | [OSS] `makepkg` sull'host con `check()`, install/avvio/rimozione in container Arch |
| RPM Fedora (`packaging/rpm/aqtruncat.spec`) | SUPPORTED | [OSS] `rpmbuild` con `%check`, `rpmlint`, `dnf install/remove` in Fedora 44 |
| `.deb` Debian/Ubuntu (`packaging/debian/`) | SUPPORTED | [OSS] `dpkg-buildpackage` con test, `lintian`, `apt install/purge` in Debian 13 e Ubuntu 26.04 |
| Flatpak | DEFERRED | §8 |
| AppImage | DEFERRED | §9 |
| Ubuntu 24.04 LTS | NOT APPLICABLE | Qt 6.4.2 < 6.5 (§6) |


## 2. Layout di installazione

Unica fonte di verità: le regole `install()` di `CMakeLists.txt` /
`src/CMakeLists.txt` (GNUInstallDirs). Nessun percorso è hardcoded nel codice
runtime: le posizioni utente vengono da `QStandardPaths` (XDG).

| File | `cmake --install` (prefix P) | `install.sh` system-wide | `install.sh --local` |
|---|---|---|---|
| eseguibile | `P/bin/aqtruncat` | `/usr/local/bin/aqtruncat` (o `--prefix`) | `${XDG_BIN_HOME:-~/.local/bin}/aqtruncat` |
| menu | `P/share/applications/aqtruncat.desktop` | `…/share/applications/` | `${XDG_DATA_HOME:-~/.local/share}/applications/` |
| icone | `P/share/icons/hicolor/{16,22,24,32,48,64,128,256}x…/apps/aqtruncat.png`, `scalable/apps/aqtruncat.svg` | idem sotto il prefix | idem sotto `$XDG_DATA_HOME` |
| man | `P/share/man/man1/aqtruncat.1` | idem | `$XDG_DATA_HOME/man/man1/` |
| documentazione | `P/share/doc/aqtruncat/` (README, LICENSE, THIRD_PARTY_NOTICES, LICENSE.RunCatNeo) | idem | `$XDG_DATA_HOME/doc/aqtruncat/` |
| manifest | — | `P/share/aqtruncat/install-manifest.txt` | `${XDG_STATE_HOME:-~/.local/state}/aqtruncat/install-manifest.txt` |
| autostart (per utente) | — | `${XDG_CONFIG_HOME:-~/.config}/autostart/aqtruncat.desktop` | idem |

- I runner inclusi sono compilati nell'eseguibile: nessun file dati runtime.
- `DESTDIR` è supportato (staging dei pacchetti e dell'installer).
- [OSS] Il binario installato contro il Qt di sistema non ha RPATH/RUNPATH
  (`qt_standard_project_setup()` aggiungeva `$ORIGIN:$ORIGIN/../lib64`,
  segnalato da rpmlint; ora `CMAKE_INSTALL_RPATH_USE_LINK_PATH` lo aggiunge
  solo per un Qt fuori dai percorsi di sistema).
- `BUILD_TESTING=OFF` salta i test (usato dall'installer); i pacchetti
  eseguono la suite completa.
- Dati utente mai installati né rimossi: `~/.config/AQTRunCat/`,
  `~/.local/share/AQTRunCat/{runners,metrics}/`.

## 3. `install.sh`

### 3.1 CLI

| Opzione | Effetto |
|---|---|
| *(nessuna)* | installazione system-wide, prefix `/usr/local`, autostart ON |
| `--local` | installazione per l'utente corrente nelle directory XDG; mai sudo, mai `/usr` |
| `-A`, `--autostart=true` | abilita autostart (default) |
| `-na`, `--autostart=false` | autostart disabilitato (rimuove una voce esistente) |
| `-U`, `--uninstall` | disinstalla (system-wide; con `--local` quella locale) |
| `--prefix=DIR` | prefix system-wide (non con `--local`; `/` rifiutato) |
| `--dry-run` | mostra il piano senza modificare nulla |
| `--build-dir=DIR` | riusa un build tree CMake (sviluppo/test) |
| `-h`, `--help` | help completo (sintassi, layout, autostart, uninstall, requisiti, esempi) |

Errori di sintassi → exit 2 con messaggio e rimando a `--help`: opzioni
sconosciute, `--autostart=` con valori diversi da true/false, opzioni di
autostart contraddittorie (`-A` con `-na` in qualunque ordine), autostart o
`--build-dir` con `-U`, `--prefix` con `--local`, prefix relativo o `/`,
`sudo ./install.sh --local`. Ripetere la stessa opzione è ammesso; l'ordine
degli argomenti non conta. Fallimenti → exit 1.

### 3.2 Funzionamento

1. Rilevamento della distribuzione (§4) e controllo dei requisiti: `cmake`,
   compilatore C++, `ninja` o `make`, Qt ≥ 6.5 (`qtpaths6`/`qmake6`),
   libarchive (`pkg-config`). Se manca qualcosa stampa il comando esatto per la
   famiglia rilevata ed esce; **non installa pacchetti**.
2. Build Release senza test in una directory temporanea (`mktemp`, rimossa
   all'uscita), con Ninja se disponibile.
   > Nota (2026-10-20): la build ora avviene, sempre da zero, in
   > `${XDG_CACHE_HOME:-~/.cache}/aqtruncat/install-build` (svuotata solo se
   > creata dall'installer, file marcatore `.aqtruncat-install-build`;
   > conservata dopo; per root la cache di root). `--no-build` installa quella
   > build senza compilare, solo se esiste, è di questo sorgente, è Release e
   > `ninja -n`/`make -q` non ha nulla da fare; altrimenti errore e nessuna
   > installazione. `--build-dir=DIR` ora aggiorna tutti i target (prima solo
   > `aqtruncat`: l'adattatore Claude poteva restare vecchio).
3. Staging con `cmake --install --prefix /` in `DESTDIR`; sono ammessi solo
   `bin/` e `share/`, mappati sulle destinazioni (§2). Ogni destinazione deve
   stare sotto la directory bin o data scelta, senza componenti `.`/`..`.
4. Il menu entry installato usa il percorso assoluto dell'eseguibile (la bin
   directory può non essere nel PATH); quotato secondo la specifica Desktop
   Entry quando serve.
5. Copia con `install -D` (sostituisce anche un binario in esecuzione); `sudo`
   solo se una destinazione non è scrivibile e solo per copie, rimozioni e
   manifest. Mai per la build, mai con `--local`.
   > Nota (F2, 2026-10-20): prima della copia (e prima della rimozione in
   > `--uninstall`) l'installer ora chiude con SIGTERM l'AQTRunCat in
   > esecuzione di questa installazione e dell'utente reale, con attesa
   > limitata, senza SIGKILL e senza riavvio; se non esce, si ferma senza
   > modificare nulla. Dettagli in `docs/integration-claude-code.md` §A.8.
6. Manifest con l'elenco dei file; su reinstallazione i file del vecchio
   manifest non più installati vengono rimossi (aggiornamento idempotente).
7. Autostart tramite `aqtruncat autostart enable|disable`, eseguito come
   l'utente reale (con `sudo ./install.sh`: l'utente di `SUDO_USER`).
8. `--local`: avviso se la bin directory non è nel PATH; i file della shell non
   vengono mai modificati.

Disinstallazione: legge e valida il manifest (intestazione obbligatoria;
validazione nella shell principale), rimuove la voce di autostart **solo se
avvia questa installazione**, rimuove i soli file elencati (rifiuta percorsi
fuori dalle destinazioni, directory e manifest estranei), rimuove le proprie
directory vuote (manifest, documentazione) ed elenca i dati utente conservati.
Senza manifest non rimuove nulla (nessuna ricerca euristica). Le directory XDG
condivise rimaste vuote (`~/.local/bin`, `applications`, `icons/hicolor/*/apps`)
non vengono rimosse, come fanno i pacchetti.

### 3.3 Sicurezza

`set -Eeuo pipefail`, quoting completo (percorsi con spazi supportati), nessun
`rm -rf` su variabili non validate (l'unico rimuove la directory `mktemp`
riconosciuta per pattern), nessun download o esecuzione remota, `shellcheck`
pulito.

## 4. Distribuzioni e dipendenze

Rilevamento da `/etc/os-release` (`ID`, poi `ID_LIKE`): `arch` → Arch;
`fedora`/`rhel`/`centos` → Fedora; `debian`/`ubuntu` → Debian. Derivate testate
(fixture): Manjaro, EndeavourOS, Rocky, Linux Mint. Sconosciute: nessuna
operazione legata al package manager; il comando suggerito diventa una
descrizione generica e l'installazione procede se i requisiti ci sono.

| | Arch | Fedora 44 | Debian 13 / Ubuntu 26.04 |
|---|---|---|---|
| build | `cmake ninja gcc qt6-base libarchive` | `cmake ninja-build gcc-c++ qt6-qtbase-devel libarchive-devel` | `cmake ninja-build g++ qt6-base-dev libarchive-dev libgl-dev` |
| runtime | `qt6-base libarchive hicolor-icon-theme` | `qt6-qtbase` (`libQt6*`), `libarchive`, `hicolor-icon-theme` | `libqt6widgets6 (>= 6.5)`, `libqt6gui6`, `libqt6core6t64`, `libarchive13t64`, `hicolor-icon-theme` |
| opzionali | `qt6-wayland` | `qt6-qtwayland` | `qt6-wayland` |
| test | `ttf-dejavu`, `desktop-file-utils` | `dejavu-sans-fonts`, `desktop-file-utils` | `fonts-dejavu-core`, `desktop-file-utils` |

[OSS] I nomi sono verificati: in ogni container il comando stampato
dall'installer, eseguito così com'è, ha installato tutto il necessario e la
successiva installazione è riuscita. Senza il plugin Wayland di Qt le
finestre usano XWayland (la tray è D-Bus e non cambia).

## 5. Autostart

La preferenza Settings è **Start AQTRunCat Tray Plugin automatically on
login**: riguarda il frontend Qt tray. Non configura l'avvio del backend
(`aqtruncatd`), che resta attivato su richiesta via D-Bus, né l'avvio del
Plasmoid, gestito da Plasma.

- Default **ON** per `install.sh`; `-na`/`--autostart=false` lo disabilitano.
- È una preferenza **per utente** anche per un'installazione system-wide: la
  voce XDG `~/.config/autostart/aqtruncat.desktop` e la chiave
  `desktop/autostart` di `AQTRunCat.ini`. L'installer non scrive mai la voce da
  sé: chiama il nuovo comando `aqtruncat autostart enable|disable|status`, che
  usa gli stessi `AppSettings` + `AutostartManager` della finestra Settings.
  Motivo: l'app riapplica la preferenza all'avvio, quindi voce e preferenza
  devono coincidere (una voce scritta a mano sarebbe stata rimossa al primo
  avvio con la preferenza di default).
- `Exec` punta al percorso canonico dell'eseguibile (su /usr unificato,
  `/usr/sbin/aqtruncat` → `/usr/bin/aqtruncat`), quotato.
- Reinstallazioni: una sola voce, transizioni ON→OFF→ON verificate.
- Pacchetti nativi: non creano autostart (preferenza dell'utente, Settings o
  `aqtruncat autostart enable`).

## 6. Requisito Qt e Ubuntu 24.04

Qt ≥ 6.5 resta il minimo (M8). Ubuntu 24.04 LTS fornisce Qt 6.4.2: CMake si
ferma con un messaggio esplicito e `install.sh` riconosce Ubuntu 24.04 e
rimanda al README. Il pacchetto `.deb` dipende da `qt6-base-dev (>= 6.5)` in
build e, a runtime, dalle versioni calcolate da `dpkg-shlibdeps`; non viene
abbassato nulla per Ubuntu 24.04. Un pacchetto self-contained con Qt incluso
sarebbe l'unica via (AppImage, §9: DEFERRED).

## 7. Pacchetti nativi

Tutti costruiti dal tarball di `packaging/make-source-tarball.sh` (versione da
`project(VERSION)`, nessun build tree incluso) e installati con le regole
CMake. Il campo Maintainer è un segnaposto (`maintainers@aqtruncat.invalid`) da
sostituire prima della pubblicazione; il progetto non ha ancora un URL
pubblico. Le versioni dei tre recipe e della pagina man sono verificate contro
CMake dal test `packaging-versions`.

| Pacchetto | Build | Validazione | Install / avvio / rimozione | Stato |
|---|---|---|---|---|
| Arch `aqtruncat-0.0.1-1-x86_64.pkg.tar.zst` | [OSS] `makepkg` sull'host (Arch, Qt 6.11), `check()` con la suite completa | `namcap` non disponibile; contenuto e `.PKGINFO` ispezionati (`depends`: `qt6-base>=6.5`, `libarchive`, `hicolor-icon-theme`, `gcc-libs`, `glibc`) | [OSS] container `archlinux:latest` (Qt 6.12): `pacman -U`, versione, `metrics`, tray offscreen 4 s, `autostart enable/disable` come utente, `pacman -R`, nessun residuo | SUPPORTED |
| RPM `aqtruncat-0.0.1-1.fc44.x86_64.rpm` | [OSS] `rpmbuild -ba` in Fedora 44, `%check` = `desktop-file-validate` + suite completa | `rpmlint`: nessun errore dopo le correzioni (RUNPATH, LICENSE duplicato); restano `no-url-tag`/`invalid-url` (nessun URL pubblico) e `no-manual-page` risolto con la pagina man | [OSS] `dnf install`, avvio, autostart (`Exec="/usr/bin/aqtruncat"`), `dnf remove`, nessun residuo | SUPPORTED |
| DEB `aqtruncat_0.0.1-1_amd64.deb` | [OSS] `dpkg-buildpackage -b` in Debian 13 (Qt 6.8) e Ubuntu 26.04 (Qt 6.10), `dh_auto_test` con la suite completa | `lintian`: sinossi e pagina man corrette; resta `initial-upload-closes-no-bugs` (solo per l'archivio Debian) | [OSS] `apt install`, avvio, autostart, `apt purge`, nessun residuo; `Depends` calcolati da `dpkg-shlibdeps` (`libqt6widgets6 (>= 6.5)`) | SUPPORTED |

Nota Arch: `makepkg` genera anche `aqtruncat-debug` (opzione `debug` di
`makepkg.conf`), come per ogni pacchetto Arch.


## 8. Flatpak — DEFERRED

Valutazione sul codice reale e sulle evidenze M0/M8, senza costruire un
artefatto che sarebbe comunque non funzionante:

| Aspetto | In sandbox | Conseguenza |
|---|---|---|
| Tray SNI | serve `--talk-name=org.kde.StatusNotifierWatcher` e `--own-name=org.kde.StatusNotifierItem-*` | accettabile |
| Autostart | `AutostartManager` scrive in `QStandardPaths::ConfigLocation` = `~/.var/app/<id>/config`, con `Exec=/app/bin/aqtruncat`, inesistente sull'host | **non funziona**: serve il portale Background (`RequestBackground` con autostart), cioè codice nuovo |
| Custom Metrics | discovery in `GenericDataLocation` = `~/.var/app/<id>/data/AQTRunCat/metrics`, non `~/.local/share`; i producer esistenti (es. `~/.claude/…`) scrivono altrove; i file scelti dal portale documenti arrivano via FUSE, dove inotify non segnala le modifiche fatte dall'host (M0) | richiederebbe `--filesystem=home:ro` o percorsi espliciti: compromesso del modello di sicurezza |
| Runner utente | stessa relocazione in `~/.var/app/…`; import ZIP solo da percorsi accessibili | funziona con limiti |
| Metriche | `/proc/stat`, `meminfo` sono dell'host; `statvfs("/")` misura il runtime, non il disco dell'host | storage Root **errato** senza `--filesystem=host` |
| Tema | runtime KDE o Freedesktop: integrazione del tema del pannello da verificare | rischio come §9 |

Conclusione: un Flatpak corretto richiede modifiche funzionali (portale
autostart, gestione percorsi sandbox per Custom Metrics e storage) o permessi
che annullano la sandbox. Rimandato alla roadmap post-M9.

## 9. AppImage — DEFERRED

| Aspetto | Valutazione |
|---|---|
| Qt incluso | linuxdeploy + plugin Qt possono includere Qt, plugin xcb/wayland e libarchive; nessuno di questi tool è installato e andrebbero scaricati ed eseguiti |
| Integrazione tema / tray | un Qt incluso non usa il platform theme KDE dell'host: su Plasma si torna al percorso generico (M8 §3.3), dove lo schema colore è `Unknown` e runner/Metrics Bar restano neri sul pannello scuro, e il runner è incorniciato in un quadrato. È una regressione funzionale sul target primario |
| Autostart | `applicationFilePath()` dentro un AppImage è il mount temporaneo `/tmp/.mount_*/usr/bin/aqtruncat`: la voce di autostart sarebbe invalida dopo il riavvio; servirebbe usare `$APPIMAGE` (codice nuovo) |
| Portabilità | richiede una build su una base glibc vecchia con Qt ≥ 6.5 compilato appositamente |
| Desktop integration / uninstall | manuali o tramite strumenti esterni (appimaged) |

Conclusione: rimandato; sarebbe anche l'unica via per Ubuntu 24.04, ma con le
regressioni sopra non può essere dichiarato supportato.

## 10. Matrice install / reinstall / uninstall

### 10.1 Installer, ambienti reali

| Scenario | Host Arch (HOME temporanea) | Container Arch | Fedora 44 | Debian 13 | Ubuntu 26.04 |
|---|---|---|---|---|---|
| requisiti mancanti → messaggio + comando esatto (exit 1) | — | PASS | PASS | PASS | PASS |
| comando suggerito installa davvero le dipendenze | — | PASS | PASS | PASS | PASS |
| system install (root) → avvio → reinstall → uninstall, nessun residuo | — | PASS | PASS | PASS | PASS |
| system install come utente + sudo (solo copie), autostart nella home utente | — | PASS | PASS | PASS | PASS |
| `sudo ./install.sh`: autostart all'utente di sudo, nessuna voce per root | — | PASS | PASS | PASS | PASS |
| `sudo ./install.sh --local` rifiutato (exit 2) | — | PASS | PASS | PASS | PASS |
| `--local` install → uninstall | PASS | PASS | PASS | PASS | PASS |
| dati utente conservati all'uninstall | PASS | PASS | PASS | PASS | PASS |
| `--local` installato e avviato nella sessione Plasma reale | PASS (runner + 2 metriche registrati, Quit exit 0) | — | — | — | — |

### 10.2 Autostart e aggiornamento ([OSS] host, HOME temporanea, e suite automatica)

| Transizione | Risultato |
|---|---|
| install (default) | voce creata, `Exec` assoluto quotato, preferenza `autostart=true` |
| reinstall `-na` | voce rimossa, `autostart=false` |
| reinstall `--autostart=true` | voce ricreata |
| reinstall `-A` | "Updating the existing installation", una sola voce |
| reinstall `--autostart=false` | voce rimossa |
| reinstall default | voce ricreata |
| file obsoleto nel manifest | rimosso dall'aggiornamento |
| `--local -U` | 15 file rimossi, autostart rimosso, impostazioni (`id=coffee`), runner utente e Custom Metrics conservati; seconda disinstallazione: exit 1 con messaggio |

## 11. Test automatici

- `tests/installer/test_install.sh` (ctest `installer`, 119 controlli):
  help/-h, errori di sintassi (12 casi), semantica di tutte le opzioni
  (dry run), rilevamento distro (Arch, Manjaro, EndeavourOS, Fedora, Rocky,
  Debian, Ubuntu, Linux Mint, sconosciuta, file mancante), versioni Qt con un
  `qmake6` finto (usage su stdout, 6.4.2 rifiutata, 6.10/6.12 accettate),
  variabili XDG personalizzate/relative, ciclo locale reale con un `sudo` finto
  che non deve mai essere chiamato, modalità system-wide su prefix scrivibile,
  aggiornamento con rimozione dei file obsoleti, sicurezza dell'uninstall
  (manifest manomesso, percorso `..`, manifest estraneo).
- `tests/packaging/check_versions.sh` (ctest `packaging-versions`):
  PKGBUILD, spec, changelog Debian e pagina man con la versione di CMake.
- `tst_cli::autostartCommands`, `tst_appsettings::autostartCommandKeepsEntryAndPreferenceInStep`,
  `tst_appsettings::autostartCommandReportsFailure`.
- I pacchetti eseguono l'intera suite nel loro ambiente di build.

## 12. Problemi trovati e corretti in M9

| Problema | Correzione | Test |
|---|---|---|
| Nessun `LICENSE` di progetto, nessuna icona, nessuna regola di install | `LICENSE` Apache-2.0, icona originale, regole CMake | `cmake --install`, pacchetti |
| Una voce di autostart scritta dall'installer sarebbe stata rimossa al primo avvio (l'app riapplica la preferenza INI) | `aqtruncat autostart enable/disable/status` condiviso con Settings | `tst_appsettings`, suite installer |
| Validazione del manifest dentro una process substitution: un manifest estraneo non fermava l'uninstall | validazione nella shell principale | "foreign manifest is rejected" |
| Rilevamento Qt: l'usage di `qmake6 --query` finiva nella versione (falliva su Arch con Qt 6.12 senza `qtpaths6`) | ogni tentativo catturato a parte e validato | test con `qmake6` finto |
| Generatore Makefile di default dove è installato solo `ninja` (Arch minimale) | Ninja quando disponibile; `ninja (or make)` tra i requisiti | container Arch |
| RUNPATH `$ORIGIN` da `qt_standard_project_setup()` (rpmlint E) | `CMAKE_INSTALL_RPATH_USE_LINK_PATH` | `readelf`, rpmlint |
| `Exec=/usr/sbin/aqtruncat` su /usr unificato | percorso canonico dell'eseguibile | container Arch |
| LICENSE duplicato nell'RPM; sinossi Debian con articolo; pagina man assente; categorie desktop doppie | spec, control, `aqtruncat.1`, `Categories=System;Monitor;` | rpmlint, lintian, desktop-file-validate |

## 13. Release readiness

| Voce | Stato |
|---|---|
| Versione | `0.0.1` da `project(VERSION)`; nessuna policy di versione nel repository, quindi non modificata. Da F4.1: 1.4.1, regola in `requirements.md` ("Versioning") |
| Metadati CMake | nome, versione, descrizione; nessun `HOMEPAGE_URL` (nessun repository pubblico) |
| Eseguibile / nome applicazione / icona | `aqtruncat` / `AQTRunCat` / `aqtruncat` (originale, non RunCat) |
| Licenze | `LICENSE` (Apache-2.0), `THIRD_PARTY_NOTICES.md`, `third_party/runcatneo/LICENSE` installato come `LICENSE.RunCatNeo` |
| Desktop metadata | `desktop-file-validate` senza avvisi (menu, autostart, voce installata) |
| Pagina man | `man --warnings` senza avvisi |
| Percorsi | nessun percorso della macchina di sviluppo in codice, pacchetti o installer |
| Artefatti | nessun file temporaneo o build tree fuori da `build/` (ignorato); tarball senza build |
| Strumenti | `install.sh` e `packaging/` per gli utenti/packager; `tools/` documentato come diagnostica per sviluppatori; `spike/` escluso dalla build di default |
| Git | il progetto non è un repository Git: `git status`/`git diff --check` non applicabili, sostituiti da controlli equivalenti (§15) |

## 14. Limitazioni

1. Flatpak e AppImage: DEFERRED (§8, §9).
2. Nessun URL pubblico né Maintainer reale: da impostare prima di pubblicare
   AUR/COPR/PPA (segnaposto `.invalid`).
3. AppStream metainfo non fornito (serve un ID applicazione stabile e una
   homepage): candidato post-M9.
4. L'installer non installa pacchetti di sistema (scelta voluta): stampa il
   comando esatto.
5. L'uninstall system-wide gestisce l'autostart dell'utente che lo esegue; le
   voci di altri utenti che puntano al binario rimosso restano (preferenze
   personali, non file del programma).
6. Ubuntu 24.04 LTS non supportata (Qt 6.4.2).

## 15. Validazione finale

Batteria finale sul codice definitivo, un container alla volta (una prima
esecuzione in parallelo era stata interrotta dal sistema per memoria
insufficiente; i container rimasti sono stati fermati e la batteria è stata
ripetuta in sequenza con il consenso dell'utente). Checksum di `install.sh` e
del test verificati prima e dopo l'esecuzione.

| Verifica | Risultato |
|---|---|
| `makepkg` (host Arch) con `check()` | PASS, 32/32 test |
| pacchetto Arch in `archlinux:latest` | PASS (install, avvio, autostart, rimozione, nessun residuo) |
| RPM in Fedora 44 (`rpmbuild`, `rpmlint`, `dnf`) | PASS, 32/32 test; rpmlint: 0 errori |
| `.deb` in Debian 13 e Ubuntu 26.04 (`dpkg-buildpackage`, `lintian`, `apt`) | PASS, 32/32 test |
| `install.sh` in Arch, Fedora 44, Debian 13, Ubuntu 26.04 | PASS, tutti gli scenari di §10.1 |
| host: `cmake -S . -B build && cmake --build build && ctest --test-dir build` | PASS: 0 warning, 32/32 suite ctest (30 Qt + `installer` + `packaging-versions`), 480 casi Qt, 0 saltati; installer 119 controlli |
| `DESTDIR=… cmake --install build --prefix /usr` | 16 file, `desktop-file-validate` ok, nessun RPATH, `man --warnings` ok |
| `shellcheck` | pulito su `install.sh`, `packaging/make-source-tarball.sh`, test Bash, `tools/*.sh` |
| `git diff --check` / `git status` | non applicabili (nessun repository Git); equivalenti: nessuno spazio finale né marcatore di conflitto nei file di testo, nessun file temporaneo nel progetto |
| pulizia | nessun processo AQTRunCat, nessun item tray di test, nessun container attivo, nessuna installazione di test sull'host (né `/usr/local`, né `~/.local`, né pacchetti), immagini docker scaricate per M9 rimosse |

## 16. File nuovi e modificati

- Nuovi: `LICENSE`, `install.sh`, `data/aqtruncat.desktop`, `data/aqtruncat.1`,
  `data/icons/hicolor/{16x16,22x22,24x24,32x32,48x48,64x64,128x128,256x256}/apps/aqtruncat.png`,
  `data/icons/hicolor/scalable/apps/aqtruncat.svg`, `src/app/autostartcommand.{h,cpp}`,
  `packaging/make-source-tarball.sh`, `packaging/arch/PKGBUILD`,
  `packaging/rpm/aqtruncat.spec`, `packaging/debian/{control,rules,changelog,copyright,source/format}`,
  `tests/installer/test_install.sh`, `tests/packaging/check_versions.sh`,
  questo documento.
- Modificati: `CMakeLists.txt` (GNUInstallDirs, install rules, `BUILD_TESTING`,
  RPATH), `src/CMakeLists.txt`, `src/cli/commandline.{h,cpp}`, `src/app/main.cpp`,
  `src/app/autostartmanager.{h,cpp}`, `tests/CMakeLists.txt`, `tests/tst_cli.cpp`,
  `tests/tst_appsettings.cpp`, `README.md`, `requirements.md`, `MEMORY.md`.



