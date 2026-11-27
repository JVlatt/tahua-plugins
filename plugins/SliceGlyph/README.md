# Tahua SliceGlyph

Dessine des symboles procéduraux dans les slices de l'Advanced Output : runes nordiques, circuits imprimés, sceaux, constellations... Les symboles épousent la forme de chaque slice, d'une famille de slices ou de toutes les slices réunies. Ils se tracent au trait, s'effacent et se renouvellent au rythme du tempo. La sortie est transparente en dehors des traits : le contenu du layer n'est pas utilisé.

Comme SliceChase et SliceEdges, l'effet lit le preset Advanced Output exporté en XML (`Documents/Resolume Arena/Presets/Advanced Output/*.xml`) et prend la forme d'entrée (Input Map) de chaque Slice et Polygon activé.

| | |
|---|---|
| **Type** | Effet |
| **ID FFGL** | `TE06` |
| **Fichier** | `TahuaSliceGlyph.dll` (Windows) / `TahuaSliceGlyph.bundle` (macOS) |

## Paramètres

| Nom | Type | Plage | Défaut | Description |
|---|---|---|---|---|
| Slices | Fichier (`.xml`) | | vide | Preset Advanced Output à lire. Sans fichier, la sortie est transparente |
| Style | Liste | voir ci-dessous | Mix | Famille de symboles |
| Complexity | Range | 0 – 1 | 0.5 | Densité : nombre de symboles, de runes, de pistes, d'étoiles... |
| Seed | Range | 0 – 100 | 0 | Change tous les symboles ; une valeur donnée redonne toujours les mêmes |
| Group | Liste | Slice / Family / All | Slice | Ce qui forme une forme : chaque slice, chaque famille de slices d'un écran, ou toutes les slices cochées réunies (voir ci-dessous) |
| Fit | Liste | Shape / Inscribed / Box | Shape | Shape : le tracé suit la forme. Inscribed : un symbole dans le plus grand cercle inscrit. Box : un symbole étiré sur le rectangle englobant, découpé par la forme |
| Size | Range | 0.1 – 1.5 | 0.85 | Shape : place prise par chaque symbole dans son cercle (runes, sceaux...) et hauteur de la bande de runes. Inscribed et Box : taille du symbole |
| Thickness | Range | 0 – 20 px | 2 | Épaisseur des traits, en pixels du layer |
| Glow | Range | 0 – 1 | 0 | Halo autour des traits (intensité et rayon) |
| Color | Couleur | | blanc | Couleur des traits |
| Animation | Liste | Off / Write / Write + Erase / Comet | Write + Erase | Voir ci-dessous |
| Cycle Beats | Liste | 1 – 32 | 4 | Durée d'un cycle, en beats du tempo de Resolume |
| Draw | Range | 0 – 1 | 0.4 | Write et Write + Erase : part du cycle passée à tracer (et à effacer) |
| Length | Range | 0 – 1 | 0.3 | Comet : longueur de la traînée, en fraction du tracé |
| Change | Case | | cochée | Un nouveau tracé à chaque cycle. Décochée, chaque forme garde le sien |
| Spread | Range | 0 – 1 | 0.5 | À 0, toutes les formes suivent le même cycle ; à 1, chacune est décalée (fixe pour une forme donnée) |
| Flicker | Range | 0 – 1 | 0 | Part des traits éteints au hasard, retirée à chaque quart de beat |
| Screens | Cases à cocher | | toutes cochées | Une case par écran du preset chargé (32 au maximum) |
| Slices › Screen | Liste | | premier écran | Écran dont les cases ci-dessous montrent les familles |
| Slices | Cases à cocher | | toutes cochées | Une case par famille de slices de l'écran choisi dans Screen (64 au maximum) |

### Group

- **Slice** : chaque slice est une forme.
- **Family** : les slices d'une même famille (`METAL_1`, `METAL_2`... donnent `METAL`) forment une seule forme, sur chaque écran. Une même famille sur deux écrans compte pour deux formes.
- **All** : toutes les slices cochées, de tous les écrans cochés, forment une seule forme globale.

Une forme réunissant plusieurs slices est leur union : les bords partagés entre deux slices, ou recouverts par une autre slice, ne comptent pas comme des bords. Le tracé n'est visible qu'à travers les slices. Cycle, Change et Spread s'appliquent par forme.

Un preset contient souvent une slice plein cadre (fond, master) : en mode All, elle fait de la forme globale un simple rectangle. Décoche son écran ou sa famille.

### Fit Shape

Le tracé est généré à partir de la distance au bord de la forme, calculée sur une grille d'environ 4 pixels :

| Style | Dans une forme |
|---|---|
| **Futhark**, **Bindrune**, **Stave**, **Ogham** | Symboles empilés dans les plus grands cercles libres de la forme, du plus grand au plus petit : une forme longue reçoit une rangée de symboles, une forme large un grand symbole entouré de petits |
| **Rune Circle** | Le bord de la forme, une bande de runes qui le longe (debout vers l'extérieur), et une bindrune dans ce qui reste au centre |
| **Sigil** | Contour intérieur de la forme, étoiles reliant des points répartis le long de chaque contour (cordes gardées seulement si elles restent dans la forme), ornements |
| **HUD** | Contours de la forme, de plus en plus rentrés : pleins, pointillés, gradués ou pointés ; réticule au point le plus profond |
| **Circuit** | Pistes sur une grille limitée à l'intérieur de la forme, dont le pas suit son épaisseur |
| **Chip** | Circuits intégrés dans les plus grands cercles libres, pistes qui s'arrêtent au bord |
| **Constellation** | Étoiles réparties dans la forme, en nombre proportionnel à sa surface |
| **Maze** | Labyrinthe limité aux cellules de la forme ; un labyrinthe par morceau séparé |

### Fit Inscribed et Box

Un seul symbole par forme, carré au centre :
- **Inscribed** : dans le plus grand cercle inscrit dans la forme.
- **Box** : étiré sur le rectangle englobant de la forme, découpé par elle. Circuit, Chip, Constellation et Maze remplissent tout le rectangle.

### Styles

| Style | Symbole |
|---|---|
| **Futhark** | Une rune du futhark ancien (24 runes), cerclée quand Complexity dépasse 0.4 |
| **Bindrune** | 2 à 4 runes liées sur une même hampe, certaines retournées |
| **Rune Circle** | Un anneau de petites runes autour d'une grande rune centrale |
| **Stave** | Branches partant du centre terminées par des ornements (flèches, fourches, cercles, tridents, crochets), à la manière des galdrastafir islandais |
| **Ogham** | Une hampe et des groupes d'encoches |
| **Circuit** | Pistes de circuit imprimé à angles de 45°, d'une pastille à un via |
| **Chip** | Un circuit intégré et ses pistes en éventail |
| **Sigil** | Étoile polygonale dans un cercle, avec ornements (cercle intérieur, rayons, croissant, triangle...) |
| **Constellation** | Étoiles reliées par leur arbre couvrant minimal |
| **Maze** | Les couloirs d'un labyrinthe, tracés dans l'ordre où il est creusé |
| **HUD** | Anneaux concentriques d'interface : arcs, graduations, pointillés, réticule |
| **Mix** | Un style au hasard par forme, qui change avec le tracé |

### Animation

Chaque tracé est une suite de traits dans l'ordre de dessin, comme à la main.

- **Off** : le tracé est affiché en entier.
- **Write** : il se trace pendant la part Draw du cycle, puis reste affiché.
- **Write + Erase** : il se trace au début du cycle et s'efface, du premier trait au dernier, à la fin.
- **Comet** : une tête parcourt le tracé sur tout le cycle, suivie d'une traînée de Length.

Avec Change cochée, chaque forme prend un nouveau tracé au début de son cycle : avec Write + Erase, il apparaît sur une forme vide. Avec Spread, les formes ne changent pas toutes en même temps.

Les écrans et familles fonctionnent comme dans [SliceChase](../SliceChase/README.md#écrans-et-familles).

## Fonctionnement

- Les tracés sont générés sur le CPU, en segments, arcs et points, à partir d'une graine propre à chaque forme, de Seed et du numéro de cycle. Seules les formes qui changent de cycle sont regénérées, et seule leur part de la texture est réécrite.
- En Fit Shape, la distance au bord de chaque forme est calculée une fois, au chargement du preset ou quand la sélection change. Les contours rentrés sont tirés de cette grille (marching squares).
- Le shader dessine les triangles des slices de chaque forme : un tracé ne déborde jamais. Chaque tracé est découpé en une grille de cellules qui listent les traits proches : un pixel ne teste que ceux de sa cellule.
- Les slices qui se superposent (la même zone sur plusieurs écrans, par exemple) gardent la valeur la plus forte.
- Un tracé compte au plus 4096 traits.

Mesuré sur le preset de test (467 slices, 1080p, Release) : moins d'une milliseconde par image avec Cycle Beats à 4, environ 4 ms avec Cycle Beats à 1 en Fit Shape. Le calcul des formes au chargement prend quelques centaines de millisecondes.

## Compatibilité

- Resolume Arena / Avenue 7.4.0 ou plus récent (FFGL 2.x) : les cases à cocher renommées à la volée en ont besoin
- Windows 10/11 x64
- macOS 10.15 ou plus récent, Intel et Apple Silicon (binaire universel)

## Notes de version

### 1.0 — 2026-11-26
- Première version : 11 styles de symboles et un mode Mix ; tracés qui suivent la forme de chaque slice, de chaque famille ou de toutes les slices réunies (Fit Shape), ou un symbole dans le cercle inscrit ou le rectangle ; animations Write, Write + Erase et Comet, renouvellement à chaque cycle, décalage entre formes, scintillement, halo, sélection des écrans et familles.
