# Tahua Stage3D

Vue 3D en perspective de l'Advanced Output : chaque écran devient un plan placé dans l'espace, et ses slices y sont dessinées à leur position de sortie, avec la source du layer projetée dessus via leur Input Map. Une caméra en orbite autour de l'origine de la scène regarde l'ensemble, et les slices peuvent être extrudées en volume.

Comme [SliceChase](../SliceChase/README.md), l'effet lit le preset Advanced Output exporté en XML (`Documents/Resolume Arena/Presets/Advanced Output/*.xml`), et les écrans et familles de slices affichés se choisissent avec les mêmes cases à cocher.

| | |
|---|---|
| **Type** | Effet |
| **ID FFGL** | `TE03` |
| **Fichier** | `TahuaStage3D.dll` (Windows) / `TahuaStage3D.bundle` (macOS) |

## Paramètres

| Nom | Type | Plage | Défaut | Description |
|---|---|---|---|---|
| Slices | Fichier (`.xml`) | | vide | Preset Advanced Output à lire. Sans fichier, la sortie est transparente |
| Mode | Liste | Texture, Color, Chase | Texture | Ce qu'affichent les slices, voir plus bas. Seuls les réglages du mode choisi sont affichés |
| Threshold | Range | 0 – 1 | 0.5 | *Chase* : une slice part vers sa cible quand la luminance moyenne du layer (le mask) sur sa surface dépasse ce seuil |
| Smooth | Range | 0 – 10 s | 0 | *Chase* : durée du trajet vers la cible, et du retour à la position d'origine |
| Move X, Y, Z | Range | -1 – 1 | 0, 0, 0.2 | *Chase* : décalage de la slice à sa cible, dans les axes de son écran (Z vers l'avant), en hauteurs d'écran |
| Rotate X, Y, Z | Range | -180 – 180° | 0 | *Chase* : rotation de la slice à sa cible, autour de son centre (ordre Z, X, Y) |
| Scale | Range | 0 – 3 | 1 | *Chase* : échelle de la slice à sa cible, autour de son centre |
| Color | Couleur | | blanc | *Color* et *Chase* : couleur des slices |
| Backdrop | Range | 0 – 1 | 0.2 | Opacité du fond gris de chaque écran ; 0 le masque |
| Extrude | Range | 0 – 1 | 0 | Épaisseur des slices, en hauteurs d'écran. La face avant reste sur le plan de l'écran et le volume part vers l'arrière ; le fond gris passe derrière. Les côtés et le dos sont ombrés |
| **Material** | | | | |
| Base Color | Couleur | | gris foncé | *Texture* : matière des slices (faces avant, côtés et dos), éclairée par l'éclairage de base, la lumière ponctuelle et la lueur des autres slices. Son alpha est l'opacité de la matière ; à 0, seule la lueur du layer reste, sur fond transparent |
| Emissive | Range | 0 – 2 | 1 | *Texture* : intensité de la lueur du layer, ajoutée par-dessus la matière sans être éclairée. Les parties transparentes ou sombres du layer brillent peu. Au-delà de 1, la texture est surexposée |
| Emissive Color | Couleur | | blanc | *Texture* : teinte de la lueur, et de la lumière qu'elle projette. Son alpha l'atténue comme Emissive |
| Bounce | Range | 0 – 10 | 2 | *Texture* : force de la lumière que la lueur projette sur la matière autour d'elle (faces avant voisines, côtés extrudés). 0 la coupe (et son coût) |
| Spread | Range | 0 – 0.5 | 0.05 | *Texture* : distance jusqu'où la lueur éclaire, en hauteurs du layer |
| **Light** | | | | |
| X, Y, Z | Range | -2 – 2 | 0, 0.5, 1 | Position de la lumière ponctuelle dans la scène (repère de la scène, pas affecté par Total) |
| Intensity | Range | 0 – 5 | 0 | Puissance de la lumière, ajoutée à l'éclairage de base ; elle diminue avec le carré de la distance. 0 l'éteint |
| Global Illumination | Case à cocher | | cochée | Éclairage de base (faces avant à pleine couleur, côtés ombrés). Décochée, seule la lumière ponctuelle éclaire les slices : ce qu'elle n'atteint pas est noir |
| **Camera** | | | | |
| Yaw | Range | -180 – 180° | 0 | Rotation de la caméra autour de l'axe vertical passant par l'origine |
| Pitch | Range | -89 – 89° | 0 | Hauteur de la caméra |
| Distance | Range | 0 – 10 | 2.5 | Cadrage : la hauteur vue à l'origine est celle qu'un champ de 45° voit à cette distance (en unités de scène). Le cadrage ne s'adapte pas au contenu : il ne dépend que des réglages de la caméra |
| Perspective | Range | 0 – 1 | 0.5 | 0 = vue plate (orthographique), 1 = perspective forte (champ de 90°). La caméra avance ou recule pour garder le cadrage : seul l'effet de profondeur change |
| **Placement** | | | | |
| Total X, Y, Z | Range | -1 – 1 | 0 | Déplace tous les écrans ensemble, en plus de leur placement propre |
| Total Rot X, Y, Z | Range | -180 – 180° | 0 | Tourne tous les écrans ensemble autour de l'origine (ordre Z, X, Y) |
| Total Size | Range | 0.05 – 10 | 1 | Agrandit ou réduit tout l'ensemble autour de l'origine |
| Edit Screen | Liste | | | Écran que modifient les réglages ci-dessous. La liste porte les noms des écrans du preset |
| X, Y, Z | Range | -1 – 1 | | Position du centre de l'écran, sur son plan d'origine (la face avant des slices), quelle que soit l'extrusion. Y vers le haut |
| Rot X, Rot Y, Rot Z | Range | -180 – 180° | 0 | Rotation de l'écran (appliquée dans l'ordre Z, X, Y) |
| Size | Range | 0.05 – 10 | 1 | Hauteur de l'écran ; la largeur suit le ratio de sa sortie |
| Screens | Cases à cocher | | toutes cochées | Une case par écran du preset chargé (32 au maximum) |
| Slices › Screen | Liste | | premier écran | Écran dont les cases ci-dessous montrent les familles. La liste porte les noms des écrans du preset |
| Slices | Cases à cocher | | toutes cochées | Une case par famille de slices de l'écran choisi dans Screen (64 au maximum) : la décocher éteint cette famille sur cet écran seulement. Voir [SliceChase](../SliceChase/README.md#écrans-et-familles) |
| *famille* Z | Range | -1 – 1 | 0 | Sous chaque case : avance (positif) ou recule la famille sur l'écran choisi, en hauteurs d'écran, dans l'axe Z de l'écran. Enregistré par nom d'écran et de famille ; le mouvement Chase part de cette position |

### Modes

- **Texture** : chaque slice montre la partie du layer couverte par son Input Map, comme sur la sortie réelle. Le layer y est émissif : il brille par-dessus la matière des slices (Base Color), et sa lueur déborde sur ce qui l'entoure : chaque face est éclairée par le layer flouté (rayon Spread) autour de l'endroit où elle le lit. Les faces avant proches d'une zone lumineuse s'éclairent en dégradé, et les côtés extrudés prennent la lumière là où elle atteint le contour de la slice. Le débordement suit la disposition du layer, pas les distances de la scène, et ne projette pas d'ombre. Pour un rendu « matière sombre éclairée par la texture », décocher Global Illumination et prendre une Base Color claire : seule la lueur l'éclaire.
- **Color** : chaque slice est remplie d'une couleur unie.
- **Chase** : le layer sert de mask et les slices, toutes visibles, sont remplies de la couleur. Comme dans SliceChase, chaque slice a un niveau de 0 à 1 : il monte vers 1 en Smooth secondes quand la luminance moyenne du mask sur sa surface dépasse le seuil, et redescend aussi en Smooth secondes sinon. Ce niveau fait passer la slice de son transform d'origine à sa cible (Move, Rotate et Scale combinés) : à 0.5, elle est à mi-chemin. Les mouvements se font dans les axes de l'écran, autour du centre de chaque slice, et suivent l'écran s'il est déplacé ou tourné.

### Placement des écrans

Tant qu'un écran n'a pas été réglé, il est centré sur l'origine (X = Y = 0), 1 unité de haut, face à la caméra par défaut, et les écrans sont empilés en Z dans l'ordre du preset, du premier devant au dernier au fond, espacés de 0.1. Dès qu'un de ses réglages est modifié, sa position complète est enregistrée **par nom** dans la composition, comme les cases à cocher : elle survit à un preset réorganisé, et les noms absents du preset sont ignorés.

Les réglages de placement par écran ne sont pas animables : ils éditent l'écran choisi dans Edit Screen. Les réglages Total et la caméra, eux, sont animables comme n'importe quel paramètre.

## Fonctionnement

- Les **Slices** sont projetées de leur Input Rect vers leur Output Rect par une homographie. Le warper Bezier n'est pas pris en compte.
- Les **Polygons** sont découpés en triangles suivant leur contour d'entrée ; chaque triangle est placé sur le triangle correspondant du contour de sortie.
- Les masks découpent les formes comme dans SliceChase.
- Chaque écran a les proportions de sa sortie (taille de l'Output Device virtuel ou de l'écran physique dans le XML).
- Un tampon de profondeur ordonne les slices et leurs volumes ; les fonds gris, semi-transparents, sont dessinés ensuite, du plus loin au plus proche.

## Compatibilité

- Resolume Arena / Avenue 7.4.0 ou plus récent (FFGL 2.x) : les cases à cocher et la liste Edit Screen renommées à la volée en ont besoin
- Windows 10/11 x64
- macOS 10.15 ou plus récent, Intel et Apple Silicon (binaire universel)

## Notes de version

### 1.1 — 2026-11-02
- Mode Texture : matière Base Color pour les slices, layer émissif par-dessus (Emissive, Emissive Color), et lueur qui éclaire la matière autour d'elle (Bounce, Spread).
- Familles de slices choisies par écran, et décalage en Z par famille.

### 1.0 — 2026-10-28
- Première version : écrans de l'Advanced Output placés en 3D, slices et polygons à leur position de sortie, modes Texture / Color / Chase, caméra en orbite fixe autour de l'origine, extrusion des slices, sélection des écrans et familles comme SliceChase.
