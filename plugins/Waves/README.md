# Tahua Waves

Animated wave masks: rings, noise contours, beat pulses

| | |
|---|---|
| **Type** | Source |
| **ID FFGL** | `TS01` |
| **Fichier** | `TahuaWaves.dll` (Windows) / `TahuaWaves.bundle` (macOS) |

## Paramètres

| Nom | Type | Plage | Défaut | Description |
|---|---|---|---|---|
| Shape | Options | Rings, Ring, Noise, Pulse, Lateral, Flat, Line, Radar, Lines | Rings | Forme du masque (voir ci-dessous) |
| Sync | Booléen | on/off | off | Cale chaque cycle sur le BPM de Resolume au lieu de Speed |
| Beats | Options | 1/4 – 16 | 1 | Durée d'un cycle en beats quand Sync est actif |
| Speed | Range | 0 – 4 | 0.5 | Cycles par seconde quand Sync est inactif |
| Scale | Range | 0.1 – 4 | 1 | Densité des anneaux, des lignes et du bruit ; rayon atteint en fin de cycle pour Ring, Pulse et Lateral (1 = les coins) |
| Thickness | Range | 0 – 1 | 0.5 | Épaisseur des anneaux, de l'anneau unique, des lignes et de la bande de bruit ; largeur du rayon de Radar |
| Softness | Range | 0 – 1 | 0 | Flou des bords |
| Echoes | Range | 0 – 12 | 8 | Nombre d'échos, de plus en plus sombres, derrière le front. Pulse / Lateral : niveaux peints à l'intérieur ; Rings / Ring / Line / Radar / Lines : copies qui suivent la forme |
| Trail | Range | 0 – 0.9 | 0.4 | Part du cycle entre le front et le dernier écho (toutes les formes sauf Noise et Flat) |
| Spread | Range | 0 – 1 | 0.5 | Lateral : écart entre les deux centres gauche/droite |
| Attack | Range | 0 – 1 | 0.5 | Flat : part du cycle passée à monter (0.5 = respiration, 0 = strobe) |
| Direction | Range | 0 – 360 | 270 | Line, Lines : sens de déplacement en degrés (270 = de haut en bas, 0 = de gauche à droite) |
| Mirror | Booléen | on/off | off | Line, Lines : les lignes partent du centre en sens opposés |
| CenterX / CenterY | Standard (slider) | 0 – 1 | 0.5 | Centre des formes |
| Invert | Booléen | on/off | off | Inverse le masque |
| Sides | Options | Circle, Triangle, Square, Pentagon, Hexagon, Octagon | Circle | Rings, Ring, Pulse : polygone au lieu du cercle, un côté en bas |

Formes :
- **Rings** : anneaux concentriques qui défilent vers l'extérieur.
- **Ring** : un seul anneau qui grandit du centre jusqu'à Scale.
- **Noise** : bande de contour sur un bruit qui évolue lentement.
- **Pulse** : un disque qui s'ouvre, suivi d'échos de plus en plus sombres jusqu'au noir.
- **Lateral** : comme Pulse, avec pour front l'intersection de deux disques centrés à gauche et à droite.
- **Flat** : tout l'écran monte puis redescend (respiration, strobe).
- **Line** : une ligne qui traverse tout l'écran d'un bord à l'autre dans le sens de Direction, une fois par cycle ; avec Mirror, deux lignes partent du centre en sens opposés.
- **Radar** : un rayon qui tourne dans le sens horaire depuis midi, une fois par cycle.
- **Lines** : des lignes parallèles qui défilent dans le sens de Direction ; avec Mirror, elles s'écartent de la ligne centrale.
La sortie est un masque blanc porté par l'alpha.

## Compatibilité

- Resolume Arena / Avenue 7.3.1 ou plus récent (FFGL 2.x)
- Windows 10/11 x64
- macOS 10.15 ou plus récent, Intel et Apple Silicon (binaire universel)

## Notes de version

### 1.1 — 2026-11-26
- Formes Radar et Lines.
- Sides : anneaux et pulses en polygone (triangle à octogone).

### 1.0 — 2026-11-02
- Première version.
