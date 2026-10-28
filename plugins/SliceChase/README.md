# Tahua SliceChase

Chaser de slices : la source du layer sert de mask, et chaque slice de l'Advanced Output s'allume quand le mask est assez fort sur sa surface.

Un plugin FFGL n'a pas accès aux slices de Resolume. On lui donne donc le preset Advanced Output exporté en XML (`Documents/Resolume Arena/Presets/Advanced Output/*.xml`). L'effet prend la forme d'entrée (Input Map) de chaque Slice et Polygon activé, formes concaves comprises, découpée par ses masks.

| | |
|---|---|
| **Type** | Effet |
| **ID FFGL** | `TE02` |
| **Fichier** | `TahuaSliceChase.dll` (Windows) / `TahuaSliceChase.bundle` (macOS) |

## Paramètres

| Nom | Type | Plage | Défaut | Description |
|---|---|---|---|---|
| Slices | Fichier (`.xml`) | | vide | Preset Advanced Output à lire. Sans fichier, la sortie est transparente |
| Threshold | Range | 0 – 1 | 0.5 | Une slice s'allume quand la luminance moyenne du mask (× alpha) sur sa surface dépasse ce seuil |
| Fade In | Range | 0 – 10 s | 0 | Durée pour passer d'éteinte à allumée |
| Fade Out | Range | 0 – 10 s | 0 | Durée pour passer d'allumée à éteinte |
| Color | Couleur | | blanc | Couleur des slices allumées ; le reste est transparent |
| Screens | Cases à cocher | | toutes cochées | Une case par écran du preset chargé (32 au maximum) |
| Slices | Cases à cocher | | toutes cochées | Une case par famille de slices présente dans les écrans cochés (64 au maximum) |

### Écrans et familles

Les cases sont construites à partir du XML chargé : elles portent les noms de ton Advanced Output, quel que soit le preset.

Une **famille** regroupe les slices dont le nom ne diffère que par le numéro final : `METAL_1` … `METAL_40` donnent `METAL`, `Wing 3` donne `Wing`, `EYE-02` donne `EYE`. Un nom sans numéro final forme sa propre famille.

Ce qui est décoché est enregistré **par nom** dans la composition, pas par position. Les choix restent valables si le preset est réorganisé. Avec un autre preset, les noms qui n'y existent pas sont ignorés.

Au-delà de 32 écrans ou 64 familles, les suivants n'ont pas de case et restent toujours actifs ; un message l'indique dans le log de Resolume.

## Fonctionnement

1. Chaque slice reçoit 64 points d'échantillonnage répartis dans sa forme. La moyenne du mask sur ces points donne sa valeur.
2. Au-dessus du seuil, la slice monte vers « allumée » en Fade In secondes ; en dessous, elle redescend en Fade Out secondes. Un fade interrompu repart de là où il en était.
3. Chaque slice est dessinée dans la couleur choisie, avec une opacité égale à son niveau.

### Masks

Les masks activés d'une slice (onglet Input de l'Advanced Output) découpent sa forme :
- un mask avec **Invert** activé (le réglage par défaut de Resolume) garde ce qui est à l'intérieur ;
- un mask sans Invert retire ce qui est à l'intérieur.

Plusieurs masks qui gardent s'additionnent ; les masks qui retirent s'appliquent ensuite. Le découpage est calculé une fois au chargement du XML.

Les coordonnées sont normalisées par la taille de composition enregistrée dans le XML. L'effet reste donc aligné même si le layer a une autre résolution.

## Compatibilité

- Resolume Arena / Avenue 7.4.0 ou plus récent (FFGL 2.x) : les cases à cocher renommées à la volée en ont besoin
- Windows 10/11 x64
- macOS 10.15 ou plus récent, Intel et Apple Silicon (binaire universel)

## Notes de version

### 1.0 — 2026-10-27
- Première version : slices et polygons lus depuis un preset Advanced Output, découpage par les masks, sélection par cases à cocher des écrans et des familles de slices, seuil, fade in / fade out, couleur.
