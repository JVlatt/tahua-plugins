# Tahua plugins

Plugins [FFGL](https://github.com/resolume/ffgl) (effets, sources, mixers) pour **Resolume Arena / Avenue 7**, sous Windows x64 et macOS (Intel et Apple Silicon).

| Plugin | Type | ID | Description |
|---|---|---|---|
| [Tahua Tint](plugins/Tint/README.md) | Effet | `TE01` | Rotation de teinte, saturation, inversion |

## Structure

```
plugins/<Nom>/         un dossier par plugin : sources, CMakeLists.txt, README.md
templates/             gabarits effect / source / mixer + README de plugin
scripts/new_plugin.cmake   crée un plugin depuis un gabarit
cmake/                 build du SDK FFGL et fonction tahua_add_plugin()
common/include/        en-tête partagé par les plugins
external/ffgl/         SDK FFGL officiel (submodule)
```

Le build est entièrement en **CMake**. Le CMake racine ajoute tout seul chaque `plugins/*/` : on n'a jamais besoin de le modifier.

Le SDK est figé sur la branche `master` de resolume/ffgl (FFGL 2.2 + GLEW, compatible Resolume ≥ 7.3.1), plutôt que sur le tag `v2.2` de 2020, qui n'a ni GLEW ni CMake.

## Prérequis

**Windows**
- Visual Studio ou Build Tools 2022 ou plus récent, avec la charge de travail « Développement Desktop en C++ ». CMake choisit automatiquement la version la plus récente installée.
- CMake ≥ 3.25 et Git.

```powershell
winget install Kitware.CMake Git.Git
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

**macOS**
- Outils Xcode en ligne de commande : `xcode-select --install`.
- CMake ≥ 3.25 : `brew install cmake`.

**Optionnel :** VS Code avec les extensions recommandées par le repo (C/C++, CMake Tools, CodeLLDB).

## Cloner

```bash
git clone --recurse-submodules https://github.com/JVlatt/tahua-plugins.git
```

Si le repo a été cloné sans `--recurse-submodules` :

```bash
git submodule update --init --recursive
```

## Compiler et installer dans Resolume

Une commande configure le projet, compile tous les plugins et les copie dans le dossier où Resolume charge les plugins tiers :

| | Debug | Release |
|---|---|---|
| Windows | `cmake --workflow --preset windows-dev` | `cmake --workflow --preset windows-release` |
| macOS | `cmake --workflow --preset macos-dev` | `cmake --workflow --preset macos-release` |

Destination :
- Windows : `Documents\Resolume Arena\Extra Effects`. Le chemin est lu dans le registre, donc un dossier Documents déplacé sur un autre disque fonctionne aussi.
- macOS : `~/Documents/Resolume Arena/Extra Effects`.

Pour copier ailleurs : `cmake --preset windows -DTAHUA_EXTRA_EFFECTS_DIR=<dossier>`.

À savoir :
- **Resolume ne charge les plugins qu'au démarrage, et verrouille ceux qui sont chargés.** Il faut quitter Arena avant de recompiler, sinon la copie échoue avec un message explicite.
- Les binaires compilés sont aussi dans `build/<preset>/bin/<Config>/`.
- Windows : runtime MSVC statique (`/MT`). La `.dll` ne dépend que de `OPENGL32` et `KERNEL32`, donc rien à installer chez l'utilisateur.
- macOS : `.bundle` universel (arm64 + x86_64), cible macOS 10.15, signature ad hoc.

Dans VS Code, `Ctrl+Shift+B` lance le build Debug avec installation.

## Déboguer

Dans VS Code, avec Arena fermé, choisis la configuration de lancement suivante, puis F5 :

- **Resolume Arena (Windows, debug)** : compile en Debug, installe, puis lance `Arena.exe --nosplash --debug-gl` avec le débogueur attaché. Les points d'arrêt dans les plugins fonctionnent.
- **Attach to running Arena (Windows)** : s'attache à un Arena déjà lancé.
- **Resolume Arena (macOS, debug)** : même principe avec CodeLLDB. Pas encore testé. Arena est signé avec le hardened runtime, et macOS peut refuser d'y attacher un débogueur. Pour vérifier :

  ```bash
  codesign -d --entitlements - "/Applications/Resolume Arena/Arena.app"
  ```

  Sans l'entitlement `com.apple.security.get-task-allow`, on se rabat sur les logs : `FFGLLog::LogToHost(...)` dans le plugin, puis lecture dans le log de Resolume.

Autres arguments utiles d'Arena : `--crash-on-gl-error`, qui fait planter Arena dès que le plugin provoque une erreur OpenGL.

## Créer un nouveau plugin

```bash
cmake -DNAME=Blur -DTYPE=effect "-DDESCRIPTION=Gaussian blur" -P scripts/new_plugin.cmake
```

Dans VS Code, la tâche « plugin: new » fait la même chose.

Paramètres :
- `NAME` : PascalCase, 10 caractères au maximum. Resolume affiche « Tahua <NAME> », et FFGL limite les noms à 16 caractères.
- `TYPE` : `effect`, `source` ou `mixer`.

Le script crée `plugins/Blur/` avec les sources, le `CMakeLists.txt` et le README. Il attribue le prochain **ID libre** pour le type demandé : `TE..` pour un effet, `TS..` pour une source, `TM..` pour un mixer. Il reste ensuite à :

1. écrire le shader et les paramètres dans `plugins/Blur/Blur.cpp` ;
2. compléter `plugins/Blur/README.md` (paramètres, notes de version) ;
3. ajouter le plugin dans le tableau en haut de ce README.

L'ID, le nom affiché et la version sont définis une seule fois, dans `plugins/<Nom>/CMakeLists.txt` (`tahua_add_plugin(... ID ... VERSION ...)`). **Un ID ne doit jamais changer une fois publié**, car Resolume s'en sert pour retrouver le plugin dans les compositions sauvegardées. Le configure échoue si deux plugins partagent un ID.

Les shaders sont en GLSL `#version 410 core`, le maximum disponible sous macOS. Les textures de Resolume sont en couleurs prémultipliées : voir les gabarits.

## Installer une release sous macOS (Gatekeeper)

Les bundles ne sont pas notarisés. Après téléchargement, macOS les marque en quarantaine, et Resolume ne peut alors pas les charger. Après avoir copié les `.bundle` dans Extra Effects :

```bash
xattr -dr com.apple.quarantine ~/Documents/"Resolume Arena"/"Extra Effects"/Tahua*.bundle
```

## CI et releases

Le workflow [`.github/workflows/build.yml`](.github/workflows/build.yml) se déclenche sur chaque push vers `main`, sur chaque PR, ou manuellement.

- Il compile tous les plugins en Release sur `windows-latest` et `macos-latest`.
- Il vérifie les binaires :
  - Windows : pas de dépendance au runtime MSVC, export de `plugMain` ;
  - macOS : binaire universel, export de `plugMain`, signature valide.
- Il publie un zip par OS en artefact téléchargeable.

Les deux plateformes vont toujours au bout. Le workflow échoue si l'une d'elles échoue.

**Publier une release :**

```bash
git tag v0.1.0
git push origin v0.1.0
```

Le tag déclenche la création d'une GitHub Release avec `tahua-plugins-v0.1.0-windows-x64.zip` et `tahua-plugins-v0.1.0-macos-universal.zip`. Chaque zip contient tous les plugins, la licence et un `INSTALL.txt`.

### Notarisation macOS (optionnelle, pas encore active)

L'étape « Sign and notarize » du workflow est ignorée tant que les secrets suivants n'existent pas dans le repo (Settings > Secrets and variables > Actions) :

| Secret | Contenu |
|---|---|
| `APPLE_CERT_P12_BASE64` | Certificat « Developer ID Application » exporté en .p12, encodé en base64 |
| `APPLE_CERT_PASSWORD` | Mot de passe du .p12 |
| `APPLE_ID` | Apple ID du compte développeur |
| `APPLE_TEAM_ID` | Team ID |
| `APPLE_APP_PASSWORD` | Mot de passe spécifique d'application pour `notarytool` |

Il faut un compte Apple Developer payant. Cette étape n'a pas encore été testée.

## Licence

[MIT](LICENSE). Le SDK FFGL (`external/ffgl`) est sous licence BSD-3-Clause.
