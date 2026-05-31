# VCVRack

Modules VCV Rack crees avec Codex.

Ce depot servira a stocker le code source, les assets et les notes de developpement
pour les modules VCV Rack que nous allons construire.

## Structure

- `Nanotk/` : plugins VCV Rack Nanotk
- `notes/` : idees, specifications et recherches
- `assets/` : images, panneaux, ressources graphiques

## Plugins

- `Nanotk/rnd-sample` : plugin `rnd-sample`, sample aleatoire inspire de `rnd.sample`.
- `Nanotk/stf-pad1` : plugin `Stf-Pad1`, pad wavetable stereo inspire du code Csound PAD1.

## Compiler et installer les plugins Nanotk

Pour compiler les plugins et les installer dans VCV Rack sur ce Mac :

```sh
./scripts/build-install-nanotk.sh
```

Redemarrer VCV Rack apres l'installation pour recharger le plugin.
