# Tahua SliceEdges

Dessine les contours des slices de l'Advanced Output, avec un pulse calé sur le beat. La sortie est transparente en dehors des contours : le contenu du layer n'est pas utilisé.

Comme SliceChase, l'effet lit le preset Advanced Output exporté en XML (`Documents/Resolume Arena/Presets/Advanced Output/*.xml`). Il prend la forme d'entrée (Input Map) de chaque Slice et Polygon activé, formes concaves comprises, découpée par ses masks.

| | |
|---|---|
| **Type** | Effet |
| **ID FFGL** | `TE04` |
| **Fichier** | `TahuaSliceEdges.dll` (Windows) / `TahuaSliceEdges.bundle` (macOS) |

## Paramètres

| Nom | Type | Plage | Défaut | Description |
|---|---|---|---|---|
| Slices | Fichier (`.xml`) | | vide | Preset Advanced Output à lire. Sans fichier, la sortie est transparente |
| Width | Range | 0 – 50 px | 2 | Épaisseur du contour, en pixels du layer |
| Overscan | Range | -50 – 50 px | 0 | Décale le contour vers l'extérieur de la slice (positif) ou vers l'intérieur (négatif) |
| Softness | Range | 0 – 50 px | 0 | Largeur du dégradé sur les bords du contour |
| Color | Couleur | | blanc | Couleur des contours |
| Pulse Beats | Liste | 1/4 – 16 | 1 | Un pulse tous les N beats du tempo de Resolume |
| Pulse Width | Range | 0 – 50 px | 0 | Épaisseur ajoutée au moment du pulse |
| Pulse Fade | Range | 0 – 1 | 0 | À 1, le contour s'efface complètement entre deux pulses ; à 0, l'opacité ne bouge pas |
| Decay | Range | 0 – 1 | 0.5 | Vitesse de retombée après le pulse : à 1, elle dure toute la période ; plus bas, elle est plus rapide |
| Trace | Liste | Off / Fill / Comet | Off | Fait tourner le contour autour de chaque slice, voir ci-dessous |
| Trace Beats | Liste | 1/4 – 16 | 4 | Durée d'un tour, en beats du tempo de Resolume |
| Length | Range | 0 – 1 | 0.25 | Comet : longueur de la traînée, en fraction du périmètre |
| Spread | Range | 0 – 1 | 0 | À 0, toutes les slices tournent ensemble ; à 1, chaque slice part d'un point différent de son tour (fixe pour une slice donnée) |
| Screens | Cases à cocher | | toutes cochées | Une case par écran du preset chargé (32 au maximum) |
| Slices › Screen | Liste | | premier écran | Écran dont les cases ci-dessous montrent les familles |
| Slices | Cases à cocher | | toutes cochées | Une case par famille de slices de l'écran choisi dans Screen (64 au maximum) |

Avec Pulse Width et Pulse Fade à 0, le contour est fixe.

### Trace

- **Fill** : le contour se dessine progressivement autour de la slice, de 0 à 100 % du périmètre, en Trace Beats beats, puis repart de zéro.
- **Comet** : un trait fait le tour de la slice en Trace Beats beats, suivi d'une traînée qui s'efface sur Length du périmètre.

Le tour part du premier point du contour et avance à vitesse constante, en pixels. Une slice trouée par un mask fait tourner chacun de ses contours (bord extérieur et trous) en même temps. Le pulse s'applique par-dessus.

Les écrans et familles fonctionnent comme dans [SliceChase](../SliceChase/README.md#écrans-et-familles).

## Fonctionnement

- Pour chaque pixel proche d'une slice, le shader calcule la distance exacte au contour de la slice, trous des masks compris. Le contour est la bande de largeur Width centrée à Overscan pixels du bord. Les coins sont arrondis côté extérieur.
- Les slices qui se touchent ou se superposent (la même zone sur plusieurs écrans, par exemple) gardent la valeur la plus forte : un bord partagé n'est pas dessiné deux fois plus fort.
- Les coordonnées sont normalisées par la taille de composition enregistrée dans le XML. Les épaisseurs sont en pixels du layer.

Le coût dépend du nombre de points des contours : une slice découpée par un mask en courbe a beaucoup de segments et coûte plus cher qu'un rectangle.

## Compatibilité

- Resolume Arena / Avenue 7.4.0 ou plus récent (FFGL 2.x) : les cases à cocher renommées à la volée en ont besoin
- Windows 10/11 x64
- macOS 10.15 ou plus récent, Intel et Apple Silicon (binaire universel)

## Notes de version

### 1.0 — 2026-11-04
- Première version : contours des slices et polygons d'un preset Advanced Output, épaisseur, overscan, douceur, couleur, pulse sur le beat (épaisseur et opacité), modes Trace Fill et Comet avec décalage entre slices, sélection des écrans et familles.
