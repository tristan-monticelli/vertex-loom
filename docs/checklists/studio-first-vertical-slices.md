# Tranches verticales studio-first

## Règle de livraison

Une fonctionnalité est terminée uniquement lorsque son contrat, son outil
d'édition, sa preview, sa sauvegarde, ses tests et son chargement runtime sont
livrés. La fixture visuelle est créée avec le studio après livraison de l'outil
et reste versionnée comme preuve.

Les fondations existantes sont conservées. Cette feuille corrige en priorité
les écarts de perception visuelle et relie ensuite chaque capacité au workflow
complet Asset Studio → Map Studio → Preview Runtime → publication de map.

## Tranche 0 — Corriger les ajouts visuels existants

- [x] Inventorier les chemins qui transforment actuellement une texture en
  fill ou en géométrie et distinguer action explicite, preview et persistance.
- [x] Ajouter `RasterView v1` avec crop en pixels source, pivot, transform et
  filtrage, plus parseur strict, sérialiseur, validation et migration.
- [x] Interpréter une référence texture historique comme une vue complète afin
  de conserver son résultat visible.
- [x] Ajouter les commandes crop, reset crop et transform de vue dans Asset
  Studio avec undo/redo, autosave et récupération.
- [x] Afficher simultanément la source complète, les paramètres du cadre de
  crop et le rendu final sans réécrire le PNG.
- [x] Comparer les draw packets et pixels de référence entre Asset Studio et
  Preview Runtime.

Gate : importer une image, la recadrer, sauvegarder, recharger et la rendre
sans modifier les octets de la source ni créer une géométrie implicite.

Gate validé : le constructeur de packet raster partagé garantit les mêmes
quad, UV et filtres dans Asset Studio et Preview Runtime ; les tests vérifient
leur égalité, un pixel OpenGL de référence après crop et l'identité des octets
PNG après sauvegarde, autosave et récupération.

## Tranche 1 — Composition et composants visuels

- [x] Ajouter `VisualComposition v1` et ses calques raster, vectoriels,
  composants et chemins texturés.
- [x] Ajouter `VisualComponent v1`, instances, variantes, ancrages, paramètres
  typés, bounds et propriétés animables.
- [x] Livrer les presets œil, bouton, couture et fermeture éclair composée de
  deux rails texturés, dents répétées et curseur, sans logique spéciale dans le
  renderer.

  Preuve : `tests/fixtures/studio-preset-gallery` est produit par
  `ProjectSession`/`MapSession`, régénéré octet par octet en test, validé
  headless et chargé par Preview Runtime avec 24 draw packets génériques.
- [x] Ajouter dans Asset Studio l'arbre de calques, le placement, le Z, la
  visibilité, les ancrages, la duplication et les paramètres.

  Preuve : l'inspecteur Asset Studio pilote les documents par commandes
  validées ; le test session couvre undo/redo, autosave, récupération,
  sauvegarde atomique et reload des calques, ancrages et paramètres.
- [x] Construire une tête textile avec crop raster, deux yeux paramétriques,
  boutons et couture uniquement depuis l'outil.

  Preuve : `tests/fixtures/studio-textile-head` est régénérée par
  `ProjectSession` avec composition générique, crop 1 × 2 sur source 2 × 2,
  deux instances d'œil, deux boutons et une couture ; le resolver vérifie ses
  20 draw packets et les UV recadrés.
- [x] Prévisualiser et charger la composition dans une entité et une map.

  Preuve : la fixture crée aussi l'entité `textile-head-entity` et la map
  `textile-head-preview` avec les sessions Studio. Preview Runtime charge la
  map et ses 20 draw packets sont appariés par identifiant stable puis comparés
  aux packets résolus directement : géométrie, UV, indices, couleurs et
  textures sont identiques.

Gate : la composition conserve le même résultat visible après undo/redo,
sauvegarde, reload et chargement runtime.

Gate validé : les commandes de composition couvrent undo/redo, autosave,
récupération, sauvegarde et reload ; la fixture Studio est régénérée octet par
octet et son entité placée dans une map produit le même contenu de rendu dans
Preview Runtime.

## Tranche 2 — Bordure texturée et Beam

- [x] Ajouter `TexturedPath v1`, validation des courbes, largeurs, références,
  modes UV, raccords et valeurs finies.
- [x] Générer un ruban triangulé déterministe avec UV continus pour chemins
  ouverts, fermés et Bézier.
- [x] Ajouter dans Asset Studio plume, attaches, largeur, répétition, offset,
  couleur, opacité et animation de texture.

  Preuve : l'inspecteur édite points de départ/fin, lignes, Bézier et poignées,
  style et UV via `ProjectSession`. La preview reconstruit le packet depuis le
  document en mémoire et propose un défilement UV temporaire ; les tests
  couvrent packet, undo/redo, autosave, récupération, sauvegarde et reload.
- [x] Réutiliser le registre de propriétés pour animer le Beam sans type de
  piste spécialisé.

  Preuve : Asset Studio charge les descripteurs animables du composant choisi
  et renseigne le binding générique de timeline. Le test `beam` découvre
  `beam/offset`, crée deux clés scalaires et vérifie leur interpolation sans
  branche de code ni type d'animation propre au Beam.
- [x] Afficher le même Beam dans Asset Studio, Map Studio et Preview Runtime.

  Preuve : la fixture versionnée `studio-beam` est créée uniquement avec les
  sessions Asset Studio et Map Studio. À `1/60 s`, le résolveur animé partagé,
  la preview headless de Map Studio et Preview Runtime produisent exactement
  les mêmes sommets, UV, indices, couleurs et référence de texture. Le canvas
  Map Studio dessine ces packets avec le backend OpenGL partagé, derrière ses
  overlays ImGui.
- [x] Vérifier explicitement qu'aucune collision n'est générée sans référence
  de collision déclarée.

  Preuve : la map Beam ne déclare ni collision ni trigger et son entité ne
  possède aucun système XPBD. Le test résout pourtant ses packets visuels puis
  vérifie de nouveau que ces trois états physiques restent absents.

Gate : une bordure textile et un Beam animé sont produits depuis le studio,
sauvegardés et rendus sans rasterisation persistante.

Gate validé : la fixture Studio sauvegarde le chemin texturé, le composant,
l'animation, l'entité et la map sans image dérivée ; ses packets coïncident
dans les trois previews et aucune donnée de collision implicite n'apparaît.

## Tranche 3 — Graphe de mécanique et plateforme tournante

- [ ] Ajouter `MechanicGraph v1`, ports typés, paramètres d'instance,
  validation, sérialisation et enregistrement comme ressource.
- [ ] Ajouter les nœuds corps, pivot, joint, moteur, capteur, contrainte et
  événement en réutilisant `fabric_physics` et les événements map.
- [ ] Ajouter dans Map Studio l'édition du graphe, l'inspecteur, la simulation,
  pause, pas-à-pas et reset.
- [ ] Livrer une plateforme tournante paramétrique avec activation par capteur
  ou événement et limites angulaires optionnelles.
- [ ] Faire transporter le personnage et exposer début, fin et état de la
  mécanique dans les overlays de debug.
- [ ] Sauvegarder la mécanique dans un prefab et modifier ses paramètres par
  overrides typés.

Gate : une map sans code spécifique instancie, simule et recharge une
plateforme tournante pilotée par la présence du personnage.

## Tranche 4 — Publication centrée sur la map

- [ ] Définir le manifeste du paquet portable, sa version et la version
  minimale de runtime.
- [ ] Calculer la fermeture transitive déterministe des ressources référencées
  par une map, ses prefabs, compositions et mécaniques.
- [ ] Refuser chemins absolus, fichiers externes, références manquantes, cycles
  invalides et collision d'identifiants avant export.
- [ ] Ajouter Preview, Validate et Publish dans Map Studio en réutilisant les
  services headless.
- [ ] Charger le paquet directement depuis Preview Runtime et le chemin futur
  du catalogue du jeu.
- [ ] Vérifier qu'un paquet produit sur une plateforme se charge sur les deux
  autres sans conversion.

Gate : la map de référence est exportée proprement, transférée puis chargée par
le runtime avec le même résultat visible et physique.

## Tranche 5 — Scène textile de référence

- [ ] Produire avec les studios une scène originale contenant personnage
  raster recadré, yeux, boutons, couture, bordure texturée et Beam animé.
- [ ] Ajouter la plateforme tournante, son capteur, ses événements et la
  réaction animée du personnage.
- [ ] Tester création, sauvegarde, reload, simulation, replay et publication.
- [ ] Mesurer 60 FPS p95 sur la scène et conserver le rapport multiplateforme.
- [ ] Utiliser cette scène comme fixture de non-régression visuelle et
  fonctionnelle pour les futures tranches.

Gate : la scène complète est construite uniquement avec Asset Studio et Map
Studio, puis chargée depuis son paquet de map par Preview Runtime.

## Ordre de commits

Chaque case fonctionnelle forme un incrément séparé : ADR/C4 si le contrat
change, implémentation minimale, tests headless, validation visuelle ciblée,
`npm run validate`, commit et CI macOS/Windows/Linux. Une case n'est cochée
qu'après ces preuves.
