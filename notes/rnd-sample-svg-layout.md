# rnd-sample SVG layout notes

Le panneau fait `150 x 380` pixels SVG, soit un module VCV Rack de 10 HP.

Le fichier a modifier a la main est :

```text
plugins/rnd-sample/res/rnd-sample.svg
```

## Important

Dans ce plugin, le SVG contient le fond graphique du panneau, mais les textes,
knobs, boutons, entrees et sorties sont places par le code C++ dans :

```text
plugins/rnd-sample/src/RndSample.cpp
```

Si tu modifies seulement le SVG, il faut donc garder les zones libres autour de
ces coordonnees.

## Taille du panneau

- Largeur : `150`
- Hauteur : `380`
- Centre horizontal : `x = 75`

## Coordonnees des controles

### Affichage basket

- Zone dynamique : `x=18`, `y=49`, `width=114`, `height=42`

### Knobs

- `PROB` : centre `36,139`
- `N` : centre `75,139`
- `SEED` : centre `114,139`

### Boutons

- `NOREP` switch : centre `38,216`
- `RESET` bouton lumineux : centre `112,216`

### Entrees

- `TRIG` : centre `38,279`
- `RESET` : centre `112,279`

### Sorties

- `V/OCT` : centre `38,319`
- `CV` : centre `112,319`
- `INDEX` : centre `38,347`
- `GATE` : centre `112,347`

## Recompiler apres modification

Apres avoir sauvegarde le SVG :

```sh
./scripts/build-install-rnd-sample.sh
```

Puis redemarrer VCV Rack pour voir le nouveau panneau.
