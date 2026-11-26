# OBSLink 1.02

**Multi-output plugin for OBS Studio with per-source "Sound off" / Plugin OBS Studio de sortie multiple avec « Son désactivé » par source audio.**

© 2026 F5PBG — GPL v2 — derived from [obs-multi-rtmp](https://github.com/sorayuki/obs-multi-rtmp) (SoraYuki).
Tested on OBS Studio 33.0.x, Windows 10/11 x64. / Testé sous OBS Studio 33.0.x, Windows 10/11 x64.

[English](#english) · [Français](#français)

---

## English

### What it does
OBSLink adds a dock **"OBSLink - Sortie multiple"** to OBS Studio: you can stream to several platforms at the same time (Twitch, YouTube, kick, Facebook…), each target being started/stopped independently.

* **Per-source "Sound off" (the main feature).** In *Edit target → Audio*, one line per OBS audio source (desktop audio, mic, VLC, radio…) with a *Son désactivé* checkbox. Example: everything goes to Twitch, but the radio sources are muted on YouTube. It works live, through a private OBS audio track reserved while the target runs (original routing restored on stop; a journal repairs it after a crash).
* **No need to start the main stream.** A target using "Reuse OBS streaming encoder" (kick, Facebook…) works even if the main OBS stream is not running: OBSLink then creates one shared video + audio encoder from your OBS output settings (simple or advanced mode). If the main stream *is* running, its encoder is reused as before.
* **Live per-source mute.** Each target has a **♪** button: tick a source (e.g. VLC) to mute it for that output *while live*, without stopping or restarting the stream (needs the target's own AAC encoder).
* **Chat docks (new in 1.01).** Three docks, "OBSLink - Chat Twitch / kick / YouTube", show the platform chats inside OBS (OBS built-in browser). Channel names are filled in automatically from your OBS profile (Twitch name, YouTube channel; kick reuses the Twitch name and can be changed). YouTube follows your current live by itself (`@channel/live`). Sign in once inside a dock if you want to write in the chat. Dock menu **♪** (or right-click on the bar): sound on each new message (choose a Windows sound or any .wav/.mp3/.wma/.m4a file, volume 0-100 %, test button), dark mode, reload, "same size as the Twitch dock". A sound plays for every new message, even identical ones (kick: identical text at least 2 s apart). Sizes and settings are remembered (`%APPDATA%\obs-studio\plugin_config\OBSLink\chat.json`).
* Per-target encoder, resolution, FPS, bitrate, audio track, output scene.
* Import / export of all settings (confirmation before exporting stream keys), and a "show / copy all settings" window (keys masked).
* Interface in English and French (other languages fall back to English).

### Install
1. Download **`Installer-OBSLink.exe`** from the [latest release](../../releases/latest).
2. **Close OBS Studio**, run the installer, accept the UAC prompt.
3. It removes the old "Sortie multiple" (obs-multi-rtmp) plugin (backup in `C:\ProgramData\obs-studio\plugins-sauvegarde-OBSLink`) and installs OBSLink in `C:\ProgramData\obs-studio\plugins\OBSLink`.
4. Options: `/desinstaller` (uninstall), `/silencieux` (no window).

The dock appears in OBS under *Docks* ("OBSLink - Sortie multiple"). Your existing obs-multi-rtmp configuration (`obs-multi-rtmp.json` in the profile) is read as is.

### Notes and limits
* **TikTok is not supported.** The TikTok stream key is bound to the live room created by TikTok LIVE Studio; OBS cannot hold a TikTok live alone (key conflict, then cut). An earlier prototype was removed in 1.00.
* "Sound off" checkboxes are greyed when the target reuses the main OBS audio encoder: pick the target's own AAC encoder to enable them (YouTube has its own by default).
* Tested locally (RTMP test server + volume measurement) and on real platforms by the author; not every platform combination has been tested.

### Build
Requirements: Visual Studio 2022 Build Tools (C++), CMake ≥ 3.20, Qt 6 (MSVC 2022 x64, in `_deps\qt6`), OBS Studio sources 33.x (`libobs` and `frontend/api` in `_src\obs`), OBS installed in `C:\Program Files\obs-studio`.

```powershell
powershell -File build.ps1      # -> dist\OBSLink\bin\64bit\OBSLink.dll + data\locale
```
The installer (`installer\Installer.cs`, C# .NET Framework) embeds `payload.zip` (the `dist\OBSLink` folder):
```powershell
Compress-Archive dist\OBSLink\* dist\payload.zip
csc /target:winexe /out:dist\Installer-OBSLink.exe /win32icon:resources\OBSLink.ico /resource:dist\payload.zip,payload.zip /r:System.Windows.Forms.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll installer\Installer.cs
```
Code map: `chat-docks.*` + `browser-panel.hpp` (chat docks, from obs-browser), `track-mixer.*` (per-source audio routing), `edit-widget.cpp` (target editor), `push-widget.cpp` (output + encoders, shared fallback encoders), `output-config.*` (JSON config), `obs-multi-rtmp.cpp` (dock, import/export), `data/locale/*.ini` (texts).

---

## Français

### Ce que fait le plugin
OBSLink ajoute à OBS Studio le dock **« OBSLink - Sortie multiple »** : diffusion simultanée vers plusieurs plateformes (Twitch, YouTube, kick, Facebook…), chaque cible se démarre et s'arrête indépendamment.

* **« Son désactivé » par source audio (fonction principale).** Dans *Modifier la cible → Audio*, une ligne par source audio d'OBS (audio du bureau, micro, VLC, radio…) avec une case *Son désactivé*. Exemple : tout part vers Twitch, mais les sources radio sont coupées vers YouTube. Fonctionne en direct, via une piste audio OBS privée réservée tant que la cible tourne (routage d'origine rétabli à l'arrêt ; un journal répare après un plantage).
* **Plus besoin de lancer le streaming principal.** Une cible qui « réutilise l'encodeur d'OBS » (kick, Facebook…) fonctionne même si le streaming principal n'est pas lancé : OBSLink crée alors un encodeur vidéo et audio partagé, d'après les réglages de sortie d'OBS (mode simple ou avancé). Si le streaming principal tourne, son encodeur est réutilisé comme avant.
* **Coupure du son par source en direct.** Chaque cible a un bouton **♪** : cochez une source (ex. VLC) pour la couper sur cette sortie *pendant le direct*, sans arrêter ni relancer le stream (il faut l'encodeur AAC propre à la cible).
* **Docks de chat (nouveau en 1.01).** Trois docks « OBSLink - Chat Twitch / kick / YouTube » affichent les chats dans OBS (navigateur intégré d'OBS). Les noms de chaîne sont remplis automatiquement d'après votre profil OBS (nom Twitch, chaîne YouTube ; kick reprend le nom Twitch, modifiable). YouTube suit tout seul votre direct en cours (`@chaîne/live`). Connectez-vous une fois dans un dock pour écrire dans le chat. Menu **♪** du dock (ou clic droit sur la barre) : son à chaque nouveau message (son Windows ou fichier .wav/.mp3/.wma/.m4a, volume 0-100 %, bouton Tester), mode sombre, recharger, « même taille que le dock Twitch ». Un son à chaque nouveau message, même identique (kick : texte identique à au moins 2 s d'écart). Tailles et réglages mémorisés (`%APPDATA%\obs-studio\plugin_config\OBSLink\chat.json`).
* Encodeur, résolution, FPS, débit, piste audio et scène de sortie par cible.
* Import / export de tous les réglages (confirmation avant d'exporter les clés), fenêtre « Afficher / copier tous les paramètres » (clés masquées).
* Interface en français et en anglais (autres langues : anglais).

### Installation
1. Télécharger **`Installer-OBSLink.exe`** dans la [dernière version](../../releases/latest).
2. **Fermer OBS Studio**, lancer l'installateur, accepter la fenêtre d'autorisation Windows (UAC).
3. Il retire l'ancien plugin « Sortie multiple » (obs-multi-rtmp ; sauvegarde dans `C:\ProgramData\obs-studio\plugins-sauvegarde-OBSLink`) et installe OBSLink dans `C:\ProgramData\obs-studio\plugins\OBSLink`.
4. Options : `/desinstaller`, `/silencieux`.

Le dock s'affiche dans le menu *Docks* d'OBS. Votre configuration obs-multi-rtmp existante (`obs-multi-rtmp.json` du profil) est lue telle quelle.

### Remarques et limites
* **TikTok n'est pas pris en charge.** La clé TikTok est liée au live créé par TikTok LIVE Studio ; OBS ne peut pas tenir seul un live TikTok (conflit de clé puis coupure). Un prototype a été retiré dans la 1.00.
* Les cases « Son désactivé » sont grisées si la cible réutilise l'encodeur audio principal d'OBS : choisir un encodeur AAC propre à la cible pour les activer (YouTube a déjà le sien).
* Testé en local (serveur RTMP de test + mesure du volume) et sur de vraies plateformes par l'auteur ; toutes les combinaisons n'ont pas été essayées.

### Compilation
Voir la section *Build* ci-dessus (`build.ps1`, installateur `installer\Installer.cs`).

---
73 de F5PBG
