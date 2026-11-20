# Cinema Surround Player

Lecteur vidéo Qt 6 (C++17) qui transforme n'importe quelle piste stéréo en
**5.1 ou 7.1** grâce à un upmix par matriçage de type Dolby Surround.

## Fonctionnalités

- Lecture de la plupart des vidéos (MP4, MKV, AVI, MOV, WebM, TS…)
- Upmix stéréo → 5.1 / 7.1 avec réglage du surround, du centre et du LFE
- Repli automatique sur 5.1 puis stéréo si le périphérique ne supporte pas le mode demandé
- Choix du périphérique de sortie, de la piste audio et des sous-titres
- Reprise de lecture automatique, réglages mémorisés
- Glisser-déposer, ouverture par ligne de commande, plein écran (double-clic)

## Raccourcis

| Touche | Action |
|---|---|
| Espace | Lecture / pause |
| ← / → | −5 s / +5 s (avec Maj : 30 s) |
| ↑ / ↓ | Volume |
| M | Couper le son |
| F ou F11 | Plein écran |
| Échap | Quitter le plein écran |

## Le traitement audio

`SurroundProcessor` (C++ pur, sans Qt) :

- **Centre** : information commune gauche/droite, retirée en partie des fronts
- **Surround** : information différentielle, filtrée passe-bas à ~7 kHz et retardée (10 à 22 ms)
- **LFE** : filtre Linkwitz-Riley d'ordre 4 à 120 Hz
- **Limiteur doux** en sortie (pas d'écrêtage brutal)

Ordre des canaux : FL, FR, FC, LFE, BL, BR (+ SL, SR en 7.1).

## Compiler

Prérequis : CMake ≥ 3.21 et Qt ≥ 6.8 (modules Widgets, Multimedia, MultimediaWidgets).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Pour ne compiler que le traitement audio et ses tests, sans Qt :
`cmake -S . -B build -DBUILD_PLAYER=OFF`.

## Limites connues

- Le son upmixé passe par un `QAudioSink` avec ~100 ms de tampon : un léger décalage son/image est possible.
- Le décodage 5.1/7.1 « natif » (AC-3, DTS) dépend du backend Qt et n'est pas géré ici.
