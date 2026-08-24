# Inventaire de compatibilité sprite et Aseprite

## Décision

Le pipeline sprite est obsolète pour l’architecture cible. Il reste compilé et
testé afin de ne supprimer ni capacité ni format sans confirmation. Il ne doit
être référencé par aucun nouveau contrat d’animation, d’entité, de map ou de
runtime.

Aseprite ne devient pas un import cible de calques ou d’images. Si ce besoin
réapparaît, il devra produire un artwork vectoriel ou des textures locales via
un nouvel ADR, sans réintroduire de spritesheet obligatoire.

## Surface actuelle

| Zone | Fichiers ou cibles | Rôle actuel | Traitement futur |
| --- | --- | --- | --- |
| Build | `CMakeLists.txt`, zlib 1.3.2 | Compile lecteur, atlas et tests. | Conserver tant que le legacy est présent. |
| Rendu | `aseprite.hpp/.cpp`, `sprite_atlas.hpp/.cpp` | Lit Aseprite, découpe PNG et génère un atlas. | Geler ; aucune dépendance du renderer vectoriel natif. |
| Projet | `sprite_sheet.hpp/.cpp`, `asset_storage.cpp`, `project_validator.cpp` | Contrat, publication, régénération et validation headless. | Continuer à charger v1 en compatibilité. |
| Éditeur | `project_session.hpp/.cpp` | Orchestre import et régénération. | Retirer de la voie cible seulement après confirmation. |
| Interface | `editors/asset_studio/main.cpp` | Affiche les deux prompts sprite. | Masquage ou retrait soumis à confirmation car il modifie une capacité existante. |
| Tests | `aseprite_tests.cpp`, `sprite_atlas_tests.cpp`, `sprite_sheet_tests.cpp`, `sprite_import_session_tests.cpp` | Protège format, déterminisme et stockage. | Conserver tant que le code ou le format reste chargeable. |
| Documentation | ADR-0021 et composants sprite | Décrit l’incrément livré. | Marquer explicitement legacy et pointer vers ADR-0022. |

## Conditions avant toute suppression

- [ ] Rechercher des `*.sprite.json`, `*.aseprite`, `*.source.png` et
  `*.atlas.png` dans les projets réels placés dans le périmètre par l’utilisateur.
- [ ] Définir si ces ressources sont ignorées, converties en textures ou
  conservées par un outil de migration.
- [ ] Présenter la liste exacte des fichiers et symboles à supprimer.
- [ ] Obtenir la confirmation explicite de suppression.
- [ ] Supprimer code, UI, contrats, tests et dépendance zlib devenus inutiles
  dans un incrément vérifié distinct.
