# Tahua SliceGlyph

Dessine un symbole procédural dans chaque slice de l'Advanced Output : runes nordiques, circuits imprimés, sceaux, constellations... Les symboles se tracent au trait, s'effacent et se renouvellent au rythme du tempo. La sortie est transparente en dehors des traits : le contenu du layer n'est pas utilisé.

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
| Complexity | Range | 0 – 1 | 0.5 | Nombre d'éléments de chaque symbole (runes autour d'un cercle, pistes d'un circuit, branches...) |
| Seed | Range | 0 – 100 | 0 | Change tous les symboles ; une valeur donnée redonne toujours les mêmes |
| Fit | Liste | Inscribed / Box | Inscribed | Inscribed : le symbole tient dans le plus grand cercle inscrit dans la slice. Box : il couvre le rectangle englobant de la slice, découpé par sa forme |
| Size | Range | 0.1 – 1.5 | 0.85 | Taille du symbole par rapport à ce cercle ou ce rectangle |
| Thickness | Range | 0 – 20 px | 2 | Épaisseur des traits, en pixels du layer |
| Glow | Range | 0 – 1 | 0 | Halo autour des traits (intensité et rayon) |
| Color | Couleur | | blanc | Couleur des traits |
| Animation | Liste | Off / Write / Write + Erase / Comet | Write + Erase | Voir ci-dessous |
| Cycle Beats | Liste | 1 – 32 | 4 | Durée d'un cycle, en beats du tempo de Resolume |
| Draw | Range | 0 – 1 | 0.4 | Write et Write + Erase : part du cycle passée à tracer (et à effacer) |
| Length | Range | 0 – 1 | 0.3 | Comet : longueur de la traînée, en fraction du symbole |
| Change | Case | | cochée | Un nouveau symbole à chaque cycle. Décochée, chaque slice garde le sien |
| Spread | Range | 0 – 1 | 0.5 | À 0, toutes les slices suivent le même cycle ; à 1, chacune est décalée (fixe pour une slice donnée) |
| Flicker | Range | 0 – 1 | 0 | Part des traits éteints au hasard, retirée à chaque quart de beat |
| Screens | Cases à cocher | | toutes cochées | Une case par écran du preset chargé (32 au maximum) |
| Slices › Screen | Liste | | premier écran | Écran dont les cases ci-dessous montrent les familles |
| Slices | Cases à cocher | | toutes cochées | Une case par famille de slices de l'écran choisi dans Screen (64 au maximum) |

### Styles

| Style | Symbole |
|---|---|
| **Futhark** | Une rune du futhark ancien (24 runes), cerclée quand Complexity dépasse 0.4 |
| **Bindrune** | 2 à 4 runes liées sur une même hampe, certaines retournées |
| **Rune Circle** | Un anneau de petites runes autour d'une grande rune centrale |
| **Stave** | Branches partant du centre terminées par des ornements (flèches, fourches, cercles, tridents, crochets), à la manière des galdrastafir islandais |
| **Ogham** | Une hampe et des groupes d'encoches |
| **Circuit** | Pistes de circuit imprimé à angles de 45°, d'une pastille à un via. Remplit toute la zone : à utiliser avec Fit Box |
| **Chip** | Un circuit intégré et ses pistes en éventail |
| **Sigil** | Étoile polygonale dans un cercle, avec ornements (cercle intérieur, rayons, croissant, triangle...) |
| **Constellation** | Étoiles reliées par leur arbre couvrant minimal |
| **Maze** | Les couloirs d'un labyrinthe, tracés dans l'ordre où il est creusé |
| **HUD** | Anneaux concentriques d'interface : arcs, graduations, pointillés, réticule |
| **Mix** | Un style au hasard par slice, qui change avec le symbole |

Circuit, Chip, Constellation et Maze remplissent la zone donnée par Fit ; les autres styles restent carrés au centre.

### Animation

Chaque symbole est une suite de traits dans l'ordre de tracé, comme à la main.

- **Off** : le symbole est affiché en entier.
- **Write** : il se trace pendant la part Draw du cycle, puis reste affiché.
- **Write + Erase** : il se trace au début du cycle et s'efface, du premier trait au dernier, à la fin.
- **Comet** : une tête parcourt le symbole sur tout le cycle, suivie d'une traînée de Length.

Avec Change cochée, chaque slice prend un nouveau symbole au début de son cycle : avec Write + Erase, il apparaît sur une slice vide. Avec Spread, les slices ne changent pas toutes en même temps.

Les écrans et familles fonctionnent comme dans [SliceChase](../SliceChase/README.md#écrans-et-familles).

## Fonctionnement

- Les symboles sont générés sur le CPU, en segments, arcs et points, à partir d'une graine propre à chaque slice, de Seed et du numéro de cycle. Ils sont regénérés quand l'un de ces éléments change.
- Le shader dessine les triangles de chaque slice : un symbole ne déborde jamais de sa slice. Pour chaque pixel, il calcule la distance à chaque trait.
- Les slices qui se superposent (la même zone sur plusieurs écrans, par exemple) gardent la valeur la plus forte.
- Un symbole compte au plus 128 traits.

Le coût dépend de la surface couverte et du nombre de traits : Fit Box et Complexity élevée sur de grandes slices coûtent le plus.

## Compatibilité

- Resolume Arena / Avenue 7.4.0 ou plus récent (FFGL 2.x) : les cases à cocher renommées à la volée en ont besoin
- Windows 10/11 x64
- macOS 10.15 ou plus récent, Intel et Apple Silicon (binaire universel)

## Notes de version

### 1.0 — 2026-11-26
- Première version : 11 styles de symboles et un mode Mix, placement dans le cercle inscrit ou le rectangle de chaque slice, animations Write, Write + Erase et Comet, renouvellement à chaque cycle, décalage entre slices, scintillement, halo, sélection des écrans et familles.
