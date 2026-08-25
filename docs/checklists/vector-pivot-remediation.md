# Remédiation de l’audit du pivot vectoriel

## Verdict du 25 août 2026

La cible est vectorielle native et ne dépendra d’aucune spritesheet. Le produit
compilé n’a pas encore atteint toute cette cible : `VectorAsset v2` sait migrer
un SVG opaque v1 vers `linkedSvg` et publier une première géométrie `native`.
Les chemins, fills image, contours, clips et le renderer restent à livrer. Le
pipeline sprite demeure compilé, validé et accessible sous `Legacy`.

Cette checklist distingue la construction de la voie vectorielle cible du
retrait du legacy. Aucune case de suppression ne peut avancer sans confirmation
explicite.

## P0 — Rendre la voie vectorielle réelle

- [x] Migrer `VectorAsset v1` vers `VectorAsset v2` avec
  `sourceKind = linkedSvg`, sans réécrire les octets du SVG source.
- [x] Livrer le socle `sourceKind = native` : taille, origine, nœuds, transform,
  rectangle, ellipse et fill couleur ou transparent.
- [x] Publier `CreateVectorArtworkPrompt` comme document natif par
  sauvegarde atomique.
- [ ] Ajouter identifiants stables, formes, chemins, groupes, transforms,
  fills, contours et clips selon ADR-0022 et ADR-0023.
- [ ] Permettre un fill image local avec transform UV indépendant et clipping
  par la forme, sans atlas ni frame.
- [x] Étendre le registre et le validateur headless aux deux `sourceKind`.
- [ ] Ajouter migration, round-trip, validation stricte et fixtures natives.

Gate : créer, sauvegarder, recharger et valider un artwork natif contenant une
forme avec fill couleur ou image, sans utiliser le pipeline sprite.

## P1 — Séparer correctement l’application et l’interface

- [ ] Déplacer l’orchestration import/publication hors de
  `editors/asset_studio/main.cpp` vers `fabric_editor`.
- [ ] Déplacer la gestion des textures d’aperçu OpenGL vers un composant de
  présentation possédé par Asset Studio.
- [ ] Conserver dans `main.cpp` uniquement événements, routage d’intentions et
  widgets Dear ImGui.
- [ ] Ajouter un flux d’import en quatre temps : sélection, validation/décodage,
  aperçu révisable, publication explicite.
- [ ] Implémenter `Add existing` avec résolution par le registre, sans copie ni
  publication implicite.
- [ ] Tester annulation et fermeture de chaque flux sans écriture ni fuite
  d’état entre prompts.

Gate : `main.cpp` ne contient aucune règle métier d’import et chaque opération
affiche son résultat exact avant toute écriture.

## P2 — Prouver l’absence de nouvelle dépendance sprite

- [ ] Interdire dans les nouveaux contrats d’entité, animation, map et runtime
  toute référence à `SpriteSheetDefinition`, frame ou atlas.
- [ ] Ajouter une vérification mécanique qui échoue si ces contrats incluent
  les en-têtes sprite hérités.
- [ ] Construire le renderer vectoriel et ses draw packets sans inclure
  `sprite_atlas.hpp`.
- [ ] Vérifier qu’un projet composé uniquement de ressources natives se charge
  lorsque les imports legacy ne sont jamais invoqués.

Gate : Asset Studio, Map Studio et Preview Runtime exécutent la scène de
référence vectorielle sans ressource `.sprite.json`.

## P3 — Legacy, sans suppression automatique

- [x] Classer Aseprite, atlas et `SpriteSheetDefinition v1` comme legacy gelé.
- [x] Les isoler visuellement sous `Legacy` dans Asset Studio.
- [ ] Inventorier les projets réels et leurs fichiers sprite selon
  `sprite-legacy-inventory.md`.
- [ ] Proposer une migration vers textures locales ou artworks natifs.
- [ ] Présenter la liste exacte des fichiers, symboles, tests et dépendances à
  retirer.
- [ ] Obtenir la confirmation explicite de suppression.
- [ ] Retirer le legacy dans un incrément vérifié distinct.

Gate : aucune suppression avant confirmation ; après confirmation, aucune
référence sprite ne subsiste dans le build cible ni dans les projets migrés.

## Validation continue

- [ ] Une case fonctionnelle exige tests locaux, `npm run validate`, CI macOS,
  Windows et Linux, puis commit dédié.
- [ ] Les checklists ne confondent plus décision, présence visuelle, intention
  en mémoire et fonctionnalité persistée.
- [ ] Chaque audit cite le fichier ou le test constituant sa preuve.
