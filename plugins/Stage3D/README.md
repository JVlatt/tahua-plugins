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
| Curve | Liste | Linear, Smooth, Elastic, Bounce | Linear | *Chase* : façon dont les slices suivent le trajet. Linear : à vitesse constante. Smooth : elles accélèrent puis freinent, sans dépasser. Elastic : elles dépassent leur cible (et leur position d'origine au retour) et reviennent en oscillant. Bounce : elles tombent sur leur cible et rebondissent dessus. Avec Smooth à 0, la courbe fait tout le trajet ; si le mask change en plein mouvement, les slices repartent avec leur élan |
| Elasticity | Range | 0 – 1 | 0.5 | *Chase* : Elastic : ampleur et durée des oscillations (0 : aucun dépassement). Bounce : hauteur des rebonds |
| Speed | Range | 0.5 – 8 Hz | 2 | *Chase* : vitesse du mouvement pour Smooth, Elastic et Bounce |
| Move Preset | Liste | Pop Out, Flip, Spin, Fall, Zoom, Shrink, Tilt, Lift | Choose... | *Chase* : mouvement tout fait : règle Move, Rotate, Scale, Curve, Elasticity et Speed (à retoucher ensuite si besoin), puis revient sur Choose... |
| Move X, Y, Z | Range | -1 – 1 | 0, 0, 0.2 | *Chase* : décalage de la slice à sa cible, dans les axes de son écran (Z vers l'avant), en hauteurs d'écran |
| Rotate X, Y, Z | Range | -180 – 180° | 0 | *Chase* : rotation de la slice à sa cible, autour de son centre (ordre Z, X, Y) |
| Scale | Range | 0 – 3 | 1 | *Chase* : échelle de la slice à sa cible, autour de son centre |
| Color | Couleur | | blanc | *Color* et *Chase* : couleur des slices |
| Backdrop | Range | 0 – 1 | 0.2 | Opacité du fond gris de chaque écran ; 0 le masque |
| Extrude | Range | 0 – 1 | 0 | Épaisseur des slices, en hauteurs d'écran. La face avant reste sur le plan de l'écran et le volume part vers l'arrière ; le fond gris passe derrière. Les côtés et le dos sont ombrés |
| **Gradient** | | | | |
| Gradient | Liste | None, Vertical, Horizontal, Diagonal, Radial, Depth | None | Dégradé de la couleur des slices (Base Color en *Texture*, Color en *Color* et *Chase*) vers Color 2. Vertical, Horizontal, Diagonal et Radial suivent la position sur l'écran ; Depth va de la face avant (première couleur) au fond de l'extrusion (Color 2) |
| Variation | Liste | None, Alternate, Random Hue, Family Hue, Random Mix | None | Couleur différente par slice. Alternate : une slice sur deux prend Color 2. Random Hue : chaque slice tourne sa teinte au hasard. Family Hue : même chose, mais une teinte par famille de slices. Random Mix : chaque slice prend une nuance au hasard entre sa couleur et Color 2. Se combine avec Gradient |
| Color 2 | Couleur | | bleu | Seconde couleur du dégradé et des variations. Son alpha règle l'opacité à son bout du dégradé |
| **Edges** | | | | |
| Edge Width | Range | 0 – 10 | 0 | Épaisseur des arêtes, en pixels (pour une sortie de 1080 de haut) : le contour de chaque slice devant et au fond de l'extrusion, et ses coins marqués entre les deux (les courbes n'en ont pas). 0 les masque |
| Edge Color | Couleur | | blanc | Couleur des arêtes ; son alpha les atténue |
| Edge Glow | Range | 0 – 1 | 0 | Halo lumineux autour des arêtes |
| See Through | Range | 0 – 1 | 0.3 | Opacité des arêtes cachées derrière les slices, vues à travers elles comme en rayons X. 0 ne montre que les arêtes visibles. Avec des slices transparentes, toutes les arêtes se voient |
| **Material** | | | | |
| Material Preset | Liste | Default, Chrome, Mirror, Gold, Copper, Brushed Steel, Glass, Plastic, Matte, Pearl, Holographic, Neon Tube | Choose... | *Texture* : matière toute faite : règle tous les paramètres de Material (à retoucher ensuite si besoin), puis revient sur Choose... Default remet la matière par défaut. Les reflets de lumière (Specular) ne se voient que si l'Intensity de la lumière est au-dessus de 0 |
| Base Color | Couleur | | gris foncé | *Texture* : matière des slices (faces avant, côtés et dos), éclairée par l'éclairage de base, la lumière ponctuelle et la lueur des autres slices. Son alpha est l'opacité de la matière ; à 0, seule la lueur du layer reste, sur fond transparent |
| Emissive | Range | 0 – 2 | 1 | *Texture* : intensité de la lueur du layer, ajoutée par-dessus la matière sans être éclairée. Les parties transparentes ou sombres du layer brillent peu. Au-delà de 1, la texture est surexposée |
| Emissive Color | Couleur | | blanc | *Texture* : teinte de la lueur, et de la lumière qu'elle projette. Son alpha l'atténue comme Emissive |
| Bounce | Range | 0 – 10 | 2 | *Texture* : force de la lumière que la lueur projette sur la matière autour d'elle (faces avant voisines, côtés extrudés). 0 la coupe (et son coût) |
| Spread | Range | 0 – 0.5 | 0.05 | *Texture* : distance jusqu'où la lueur éclaire, en hauteurs du layer |
| Specular | Range | 0 – 5 | 0 | *Texture* : reflet de la lumière ponctuelle sur la matière, opaque là où il brille. Sans effet tant que l'Intensity de la lumière est à 0 |
| Glossiness | Range | 0 – 1 | 0.5 | *Texture* : netteté du reflet : 0 large et diffus, 1 petit et précis |
| Fresnel | Range | 0 – 1 | 0 | *Texture* : vues de biais, les faces deviennent opaques et renvoient la lumière qu'elles reçoivent (éclairage de base, lumière ponctuelle, lueur), comme le verre sur ses bords. Pour du verre : alpha de Base Color bas, Fresnel et Specular montés, lumière allumée |
| Metallic | Range | 0 – 1 | 0 | *Texture* : métal : la matière ne diffuse plus sa couleur, elle la met dans ses reflets (Specular, Reflection, Fresnel). 1 pour du chrome, de l'or, du cuivre |
| Reflection | Range | 0 – 1 | 0 | *Texture* : reflet de l'environnement : le layer, comme s'il entourait la scène, sur un ciel de la couleur de l'éclairage de base (Global Color). Net avec une Glossiness haute, flou quand elle est basse. Le reflet bouge avec la caméra |
| Iridescence | Range | 0 – 1 | 0 | *Texture* : couleurs qui changent avec l'angle de vue, comme un hologramme ou de la nacre. Teinte la matière et ses reflets |
| **Light** | | | | |
| Light Preset | Liste | Flat, Studio, Soft Box, Spotlight, Backlight, Sunset, Night, Neon, Orbit, Searchlight, Beat Pulse, Strobe, Candle, Rainbow, Police, Disco | Choose... | Éclairage tout fait : règle tous les paramètres de Light, animation comprise (à retoucher ensuite si besoin), puis revient sur Choose... Flat remet l'éclairage par défaut. Les huit derniers sont animés |
| X, Y, Z | Range | -2 – 2 | 0, 0.5, 1 | Position de la lumière ponctuelle dans la scène (repère de la scène, pas affecté par Total) |
| Intensity | Range | 0 – 5 | 0 | Puissance de la lumière, ajoutée à l'éclairage de base ; elle diminue avec le carré de la distance. 0 l'éteint |
| Global Illumination | Case à cocher | | cochée | Éclairage de base (faces avant à pleine couleur, côtés ombrés). Décochée, seule la lumière ponctuelle éclaire les slices : ce qu'elle n'atteint pas est noir |
| Point Color | Couleur | | blanc | Teinte de la lumière ponctuelle, et de son reflet (Specular). Son alpha l'atténue comme Intensity |
| Global Color | Couleur | | blanc | Teinte de l'éclairage de base (Global Illumination). Son alpha l'atténue |
| Softness | Range | 0 – 1 | 0 | Adoucit l'éclairage : la lumière ponctuelle enveloppe les faces au lieu de s'arrêter net et porte plus loin, et les côtés ombrés s'éclaircissent vers la couleur des faces avant |
| Animation | Liste | None, Orbit, Sweep, Pulse, Strobe, Flicker, Color Cycle, Police, Disco | None | Anime la lumière ponctuelle par-dessus ses réglages, en rythme avec le tempo de Resolume. Orbit : tourne autour de l'axe vertical passant par l'origine (à la distance donnée par X et Z). Sweep : balaie de gauche à droite. Pulse : flash à chaque cycle, qui s'éteint ensuite. Strobe : allumée seulement au début de chaque cycle. Flicker : vacille au hasard. Color Cycle : fait le tour des couleurs, aller et retour. Police : rouge d'un côté, bleu de l'autre, en clignotant. Disco : tourne en sautant de couleur en couleur (8 par cycle) |
| Animation Rate | Liste | 4 Bars … 1/4 Beat | 1 Bar | Durée d'un cycle d'animation, calée sur la mesure de Resolume |
| Animation Amount | Range | 0 – 1 | 1 | Force de l'animation : amplitude du balayage, profondeur des flashs et du vacillement, part du cercle des couleurs. Sans effet sur Orbit |
| **Camera** | | | | |
| Yaw | Range | -180 – 180° | 0 | Rotation de la caméra autour de l'axe vertical passant par l'origine |
| Pitch | Range | -89 – 89° | 0 | Hauteur de la caméra |
| Distance | Range | 0 – 10 | 2.5 | Cadrage : la hauteur vue à l'origine est celle qu'un champ de 45° voit à cette distance (en unités de scène). Le cadrage ne s'adapte pas au contenu : il ne dépend que des réglages de la caméra |
| Perspective | Range | 0 – 1 | 0.5 | 0 = vue plate (orthographique), 1 = perspective forte (champ de 90°). La caméra avance ou recule pour garder le cadrage : seul l'effet de profondeur change |
| **Placement** | | | | |
| Total X, Y, Z | Range | -1 – 1 | 0 | Déplace tous les écrans ensemble, en plus de leur placement propre |
| Total Rot X, Y, Z | Range | -180 – 180° | 0 | Tourne tous les écrans ensemble autour du centre de l'ensemble (le milieu de la boîte qui englobe les slices, avec leur décalage Z et leur extrusion, et les backdrops visibles, sans les mouvements du Chase ; ordre Z, X, Y) |
| Total Size | Range | 0.05 – 10 | 1 | Agrandit ou réduit tout l'ensemble autour de son centre |
| Edit Screen | Liste | | | Écran que modifient les réglages ci-dessous. La liste porte les noms des écrans du preset |
| X, Y, Z | Range | -1 – 1 | | Position du centre de l'écran, sur son plan d'origine (la face avant des slices), quelle que soit l'extrusion. Y vers le haut |
| Rot X, Rot Y, Rot Z | Range | -180 – 180° | 0 | Rotation de l'écran (appliquée dans l'ordre Z, X, Y) |
| Size | Range | 0.05 – 10 | 1 | Hauteur de l'écran ; la largeur suit le ratio de sa sortie |
| Screens | Cases à cocher | | toutes cochées | Une case par écran du preset chargé (32 au maximum) |
| Slices › Screen | Liste | | premier écran | Écran dont les cases ci-dessous montrent les familles. La liste porte les noms des écrans du preset |
| Slices › Search | Texte | | vide | Recherche : seules les familles dont le nom contient ce texte (sans tenir compte des majuscules) ont une case. Vide, toutes s'affichent. Une famille masquée par la recherche garde son état coché ou décoché |
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
- Quand les slices sont transparentes (alpha de Base Color en *Texture*, de Color sinon, sous 1), slices et fonds sont tous dessinés du plus loin au plus proche, triés par leur centre, pour que chacun laisse voir ce qui est derrière lui. Deux slices qui se croisent, ou les déplacements du mode Chase, peuvent encore s'afficher dans le mauvais ordre.

## Compatibilité

- Resolume Arena / Avenue 7.4.0 ou plus récent (FFGL 2.x) : les cases à cocher et la liste Edit Screen renommées à la volée en ont besoin
- Windows 10/11 x64
- macOS 10.15 ou plus récent, Intel et Apple Silicon (binaire universel)

## Notes de version

### 1.4 — 2026-11-04
- Lumière : couleur de la lumière ponctuelle et de son reflet (Point Color), et couleur de l'éclairage de base (Global Color).

### 1.3 — 2026-11-02
- Total Rot et Total Size tournent et agrandissent l'ensemble autour de son centre (slices avec leur décalage Z et leur extrusion, backdrops visibles), et non plus autour de l'origine de la scène.

### 1.2 — 2026-11-02
- Mode Texture : effet verre, avec le reflet de la lumière ponctuelle (Specular, Glossiness) et des faces qui deviennent réfléchissantes vues de biais (Fresnel).
- Slices transparentes dessinées du plus loin au plus proche : elles ne cachent plus celles qui sont derrière.

### 1.1 — 2026-11-02
- Mode Texture : matière Base Color pour les slices, layer émissif par-dessus (Emissive, Emissive Color), et lueur qui éclaire la matière autour d'elle (Bounce, Spread).
- Familles de slices choisies par écran, et décalage en Z par famille.

### 1.0 — 2026-10-28
- Première version : écrans de l'Advanced Output placés en 3D, slices et polygons à leur position de sortie, modes Texture / Color / Chase, caméra en orbite fixe autour de l'origine, extrusion des slices, sélection des écrans et familles comme SliceChase.
