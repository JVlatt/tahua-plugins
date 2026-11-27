# Tahua SliceWaves

Fait tourner les formes de [Tahua Waves](../Waves/README.md) à l'intérieur de chaque slice de l'Advanced Output, chacune dans son propre repère, avec un décalage dans le temps d'une slice à l'autre. La sortie est transparente en dehors des slices : le contenu du layer n'est pas utilisé.

Comme SliceChase et SliceEdges, l'effet lit le preset Advanced Output exporté en XML (`Documents/Resolume Arena/Presets/Advanced Output/*.xml`). Il prend la forme d'entrée (Input Map) de chaque Slice et Polygon activé, formes concaves comprises, découpée par ses masks.

| | |
|---|---|
| **Type** | Effet |
| **ID FFGL** | `TE07` |
| **Fichier** | `TahuaSliceWaves.dll` (Windows) / `TahuaSliceWaves.bundle` (macOS) |

## Paramètres

| Nom | Type | Plage | Défaut | Description |
|---|---|---|---|---|
| Slices | Fichier (`.xml`) | | vide | Preset Advanced Output à lire. Sans fichier, la sortie est transparente |
| Shape … Sides | | | | Les paramètres de [Tahua Waves](../Waves/README.md#paramètres), dans le même ordre et avec le même sens, appliqués dans le repère de chaque slice. Shape a une forme de plus, Contour |
| Offset | Liste | Random / Order / Neighbour | Random | Comment le départ du cycle est décalé d'une slice à l'autre, voir ci-dessous |
| Delay | Range | 0 – 1 | 0 | Écart entre la première et la dernière slice, en part du cycle. À 0, toutes les slices sont synchrones |
| Origin X / Origin Y | Standard (slider) | 0 – 1 | 0.5 | Neighbour : point de départ de la vague, en coordonnées de la composition. La slice qui le contient (ou la plus proche) part la première |
| Color | Couleur | | blanc | Couleur des formes |
| Screens | Cases à cocher | | toutes cochées | Une case par écran du preset chargé (32 au maximum) |
| Slices › Screen | Liste | | premier écran | Écran dont les cases ci-dessous montrent les familles |
| Slices | Cases à cocher | | toutes cochées | Une case par famille de slices de l'écran choisi dans Screen (64 au maximum) |

Les écrans et familles fonctionnent comme dans [SliceChase](../SliceChase/README.md#écrans-et-familles).

### Repère de chaque slice

Chaque slice a son propre repère : le plus petit rectangle qui la contient, tourné avec elle. Des quatre façons de le poser, c'est celle à moins de 45° de la verticale qui est gardée, pour qu'une slice presque à plat garde le haut en haut. Les formes s'y dessinent comme Waves dans le cadre de la composition : Scale 1 atteint les coins du rectangle, CenterX / CenterY à 0.5 sont son centre, une Line à Direction 270 le descend.

### Contour

Une bande qui suit le contour réel de la slice, trous des masks compris, et se resserre du bord vers son point le plus profond en un cycle. Thickness règle sa largeur, Echoes et Trail ajoutent des bandes qui la suivent, Mirror inverse le sens (du centre vers le bord).

### Offset

- **Random** : chaque slice part à une phase fixe, tirée au hasard pour elle.
- **Order** : les slices partent l'une après l'autre, dans l'ordre du preset (écran par écran, puis slice par slice).
- **Neighbour** : une vague part de la slice sous Origin et se propage de slice en slice. Deux slices sont voisines quand elles se touchent ou sont à moins de 8 pixels du layer l'une de l'autre ; la vague avance à vitesse constante de centre en centre. Une slice isolée est atteinte quand même, mais la vague met trois fois plus de temps pour franchir le vide.

Le décalage de chaque slice est recalculé quand le preset, la sélection, Offset ou Origin changent. Avec Neighbour, ce calcul prend une fraction de seconde sur quelques centaines de slices : bouger Origin en continu (automation) n'est pas conseillé.

## Fonctionnement

- Pour chaque pixel d'une slice, le shader calcule la distance exacte au contour, qui donne le bord antialiasé de la slice et la profondeur utilisée par Contour.
- Les slices qui se superposent (la même zone sur plusieurs écrans, par exemple) gardent la valeur la plus forte.
- Le code des formes est partagé avec Tahua Waves (`common/include/WaveShapes.h`) : une forme ajoutée à Waves l'est aussi ici.

## Compatibilité

- Resolume Arena / Avenue 7.4.0 ou plus récent (FFGL 2.x) : les cases à cocher renommées à la volée en ont besoin
- Windows 10/11 x64
- macOS 10.15 ou plus récent, Intel et Apple Silicon (binaire universel)

## Notes de version

### 1.0 — 2026-11-26
- Première version : formes de Waves par slice, forme Contour, décalages Random / Order / Neighbour.
