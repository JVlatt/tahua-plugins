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
| Shape | Options | Rings, Ring, Noise, Pulse, Lateral, Flat, Line | Rings | Forme du masque (voir ci-dessous) |
| Sync | Booléen | on/off | off | Cale chaque cycle sur le BPM de Resolume au lieu de Speed |
| Beats | Options | 1/4 – 16 | 1 | Durée d'un cycle en beats quand Sync est actif |
| Speed | Range | 0 – 4 | 0.5 | Cycles par seconde quand Sync est inactif |
| Scale | Range | 0.1 – 4 | 1 | Densité des anneaux et du bruit ; rayon atteint en fin de cycle pour Ring, Pulse et Lateral (1 = les coins) |
| Thickness | Range | 0 – 1 | 0.5 | Épaisseur des anneaux, de l'anneau unique, de la ligne et de la bande de bruit |
| Softness | Range | 0 – 1 | 0 | Flou des bords |
| Echoes | Range | 0 – 12 | 8 | Nombre d'échos, de plus en plus sombres, derrière le front. Pulse / Lateral : niveaux peints à l'intérieur ; Rings / Ring / Line : copies qui suivent la forme |
| Trail | Range | 0 – 0.9 | 0.4 | Part du cycle entre le front et le dernier écho (Rings, Ring, Pulse, Lateral, Line) |
| Spread | Range | 0 – 1 | 0.5 | Lateral : écart entre les deux centres gauche/droite |
| Attack | Range | 0 – 1 | 0.5 | Flat : part du cycle passée à monter (0.5 = respiration, 0 = strobe) |
| Direction | Range | 0 – 360 | 270 | Line : sens de déplacement en degrés (270 = de haut en bas, 0 = de gauche à droite) |
| Mirror | Booléen | on/off | off | Line : deux lignes partent du centre en sens opposés |
| CenterX / CenterY | Standard (slider) | 0 – 1 | 0.5 | Centre des formes |
| Invert | Booléen | on/off | off | Inverse le masque |

Formes :
- **Rings** : anneaux concentriques qui défilent vers l'extérieur.
- **Ring** : un seul anneau qui grandit du centre jusqu'à Scale.
- **Noise** : bande de contour sur un bruit qui évolue lentement.
- **Pulse** : un disque qui s'ouvre, suivi d'échos de plus en plus sombres jusqu'au noir.
- **Lateral** : comme Pulse, avec pour front l'intersection de deux disques centrés à gauche et à droite.
- **Flat** : tout l'écran monte puis redescend (respiration, strobe).
- **Line** : une ligne qui traverse tout l'écran d'un bord à l'autre dans le sens de Direction, une fois par cycle ; avec Mirror, deux lignes partent du centre en sens opposés.
La sortie est un masque blanc porté par l'alpha.

## Compatibilité

- Resolume Arena / Avenue 7.3.1 ou plus récent (FFGL 2.x)
- Windows 10/11 x64
- macOS 10.15 ou plus récent, Intel et Apple Silicon (binaire universel)

## Notes de version

### 1.0 — 2026-11-02
- Première version.
