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
  boutons, couture et Beam animé uniquement depuis l'outil.

  Preuve : `tests/fixtures/studio-textile-head` est régénérée par
  `ProjectSession` avec composition générique, crop 1 × 2 sur source 2 × 2,
  deux instances d'œil, deux boutons, une couture et un Beam à bordure
  texturée ; le resolver vérifie ses 21 draw packets et les UV recadrés. La
  map assigne `beam-scroll` à l'instance et Preview Runtime vérifie le binding
  `root/beam/offset` à mi-lecture.
- [x] Prévisualiser et charger la composition dans une entité et une map.

  Preuve : la fixture crée aussi l'entité `textile-head-entity` et la map
  `textile-head-preview` avec les sessions Studio. Preview Runtime charge la
  map et ses 21 draw packets sont appariés par identifiant stable puis comparés
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

- [x] Ajouter `MechanicGraph v1`, ports typés, paramètres d'instance,
  validation, sérialisation et enregistrement comme ressource.

  Preuve : le document strict `*.mechanic.json` conserve paramètres typés,
  nœuds, propriétés, ports orientés et connexions. Les tests couvrent
  round-trip, champs inconnus, valeurs non finies ou incompatibles, ports et
  nœuds absents, entrée liée plusieurs fois, doublons, cycles, sauvegarde
  atomique et résolution des références par le validateur headless.
- [x] Ajouter les nœuds corps, pivot, joint, moteur, capteur, contrainte et
  événement en réutilisant `fabric_physics` et les événements map.

  Preuve : les sept types possèdent un schéma fermé de propriétés et ports
  typés, y compris les handles réservés aux ports `body`, `pivot` et `joint`.
  `fabric_physics` applique les paramètres liés puis compile dans l'ordre un
  plan headless contenant les sept descriptions, sans handle Box2D persistant.
  Les événements sont résolus contre ceux de la map ; câblages obligatoires,
  événements absents et configurations physiques invalides sont refusés.
- [x] Ajouter dans Map Studio l'édition du graphe, l'inspecteur, la simulation,
  pause, pas-à-pas et reset.

  Preuve : le panneau `Mechanics` liste, crée et ouvre les documents du projet,
  ajoute ou retire les sept nœuds, édite leurs propriétés typées et relie leurs
  ports avec validation immédiate. `MechanicSession` applique CommandStack,
  undo/redo, sauvegarde atomique, autosave et récupération sans rendre la map
  dirty. Chaque mutation recompile puis reconstruit une preview Box2D
  éphémère ; Play, Pause, Step `1/60`, Reset, compteur de pas et états des corps
  sont disponibles dans l'inspecteur. Les tests headless couvrent authoring,
  connexions, rejet de preview incomplète, undo/redo, sauvegarde/reload,
  récupération et contrôles à pas fixe.
- [x] Livrer une plateforme tournante paramétrique avec activation par capteur
  ou événement et limites angulaires optionnelles.

  Preuve : le prompt `Rotating platform` de Map Studio assemble un graphe
  générique corps/pivot/joint/moteur avec source `sensor` ou événement
  `listen`. Taille, vitesse, direction, accélération, couple, zone capteur et
  limites sont des paramètres liés et validés ; la preview injecte les deux
  types de signal, accélère le moteur puis laisse Box2D borner le joint. La
  fixture versionnée `studio-rotating-platform` est régénérée octet par octet
  avec `ProjectSession`, `MapSession` et `MechanicSession` : Asset Studio crée
  sa bande textile texturée et son entité, puis Map Studio crée et sauvegarde
  la mécanique qui la référence sans confondre visuel et collision. Les tests
  couvrent mode inactif, capteur, événement, direction, accélération, limites,
  référence visuelle, reload et validation du projet complet.
- [x] Faire transporter le personnage et exposer début, fin et état de la
  mécanique dans les overlays de debug.

  Preuve : la preview matérialise le capteur du graphe comme forme Box2D et
  permet à Map Studio de placer et déplacer un personnage dynamique. La
  friction du même monde l'entraîne sur la plateforme, sans parentage ou
  correction propre au preset. Le canvas superpose corps, capteur actif et
  personnage ; l'inspecteur affiche l'état courant et un journal borné
  `begin/end` ordonné par pas fixe. Les tests headless couvrent entrée physique,
  transport, sortie, injection manuelle et la fixture Studio versionnée.
- [x] Sauvegarder la mécanique dans un prefab et modifier ses paramètres par
  overrides typés.

  Preuve : `PrefabDefinition` référence séparément l'entité textile et le
  `MechanicGraph`, puis conserve des `mechanicOverrides` ciblant les paramètres
  stables du graphe. Map Studio crée calques et prefabs, propose les ressources
  du projet, dérive l'éditeur des paramètres déclarés et prévisualise la copie
  effective sans modifier le graphe source. Le validateur global refuse noms,
  types et références invalides. La fixture `studio-rotating-platform`
  instancie réellement ce prefab dans sa map avec vitesse et zone capteur
  remplacées ; son transform place ensemble entité, corps, pivot et capteur.
  Round-trip, undo, reload, rendu de map, compilation transformée et simulation
  headless sont vérifiés ; les échelles physiques non uniformes sont refusées.

Gate : une map sans code spécifique instancie, simule et recharge une
plateforme tournante pilotée par la présence du personnage.

## Tranche 4 — Publication centrée sur la map

- [x] Définir le manifeste du paquet portable, sa version et la version
  minimale de runtime.

  Preuve : `MapPackageManifest v1` décrit `map-package.json`, sa map racine,
  sa version minimale SemVer et ses ressources ordonnées avec chemins de
  document et payloads relatifs. Le parseur strict et le validateur refusent
  schéma inconnu, champs inconnus, racine absente, ordre instable, doublons,
  collisions et chemins absolus, traversants ou Windows. Le contrôle de
  compatibilité compare la version minimale à la version réelle de
  `fabric_core`. Le round-trip déterministe et ces refus sont couverts par la
  suite `fabric_map_package_tests`.
- [x] Calculer la fermeture transitive déterministe des ressources référencées
  par une map, ses prefabs, compositions et mécaniques.

  Preuve : `plan_map_package` part du `MapDocument`, résout chaque référence
  typée avec les loaders partagés et utilise une frontière ordonnée par type et
  identifiant. Les prefabs inline suivent entité, mécanique, overrides et
  références de propriétés sans ajouter une seconde copie du fichier map. Les
  textures ajoutent leur PNG, les SVG liés leur source et les vecteurs natifs
  aucun payload. Les fixtures Studio `studio-rotating-platform` et
  `studio-textile-head` vérifient la fermeture complète, l'ordre stable, la
  déduplication et la sérialisation répétable.
- [x] Refuser chemins absolus, fichiers externes, références manquantes, cycles
  invalides et collision d'identifiants avant export.

  Preuve : les loaders canonisent documents, PNG et SVG ; une source liée hors
  projet produit désormais `invalid_path`, tandis qu'une absence produit
  `missing_file`. Le planificateur maintient les arêtes de la fermeture,
  détecte les cycles sans récursion et impose un identifiant unique tous types
  confondus. Des copies temporaires de la fixture plateforme vérifient document
  manquant, source absolue, symlink externe, cycle composant/composition et
  collision entité/mécanique avant toute écriture de paquet.
- [x] Ajouter Preview, Validate et Publish dans Map Studio en réutilisant les
  services headless.

  Preuve : Map Studio expose `Preview`, `Validate` et `Publish` dans la barre
  de la map. Preview redémarre l'aperçu graphique partagé ; Validate appelle
  `plan_map_package` et affiche ses diagnostics ; Publish utilise le dialogue
  de dossier natif et `publish_map_package`, dans un sous-dossier nommé par la
  map, sans écrasement. Une session dirty est sauvegardée avant les deux
  opérations de paquet. Le service headless copie manifeste, documents et
  payloads et son round-trip est vérifié par `fabric_map_package_tests`.
- [x] Charger le paquet directement depuis Preview Runtime et le chemin futur
  du catalogue du jeu.
- [x] Vérifier qu'un paquet produit sur une plateforme se charge sur les deux
  autres sans conversion.

  Preuve : le workflow CI publie `studio-rotating-platform` une seule fois sur
  Ubuntu avec `fabric_map_package_export`, transfère le dossier inchangé comme
  artefact, puis lance `game_runtime --package ... --smoke-test 1` sur Ubuntu,
  macOS et Windows.

Gate : la map de référence est exportée proprement, transférée puis chargée par
le runtime avec le même résultat visible et physique.

## Tranche 5 — Scène textile de référence

- [x] Produire avec les studios une scène originale contenant personnage
  raster recadré, yeux, boutons, couture, bordure texturée et Beam animé.

  Preuve : la fixture `studio-textile-head` est régénérée octet par octet par
  Asset Studio, conserve la source raster intacte et combine crop, yeux,
  boutons, couture et Beam texturé ; la map et l'animation sont créées par les
  sessions Studio et chargées par Preview Runtime.
- [x] Ajouter la plateforme tournante, son capteur, ses événements et la
  réaction animée du personnage.

  Preuve : la fixture contient le preset de plateforme, son entité textile,
  son prefab, le graphe `rotating-platform`, le capteur `presence` et
  `platform-activate`. Le personnage conserve l'animation `beam-scroll` ; la
  simulation déclenche le capteur, fait tourner la plateforme dans ses limites
  et transporte le personnage, tandis que Preview Runtime vérifie le binding
  animé du Beam.
- [x] Tester création, sauvegarde, reload, simulation, replay et publication.

  Preuve : la génération par `ProjectSession`, `MapSession` et
  `MechanicSession` sauvegarde la scène ; le test la recharge par les sessions,
  recharge le replay, l'exécute pendant 61 frames, puis publie un paquet et le
  charge directement dans Preview Runtime. La simulation vérifie activation,
  rotation et transport.
- [x] Mesurer 60 FPS p95 sur la scène et conserver le rapport multiplateforme.

  Preuve locale : `fabric_runtime_benchmark --project
  tests/fixtures/studio-textile-head --map textile-head-preview --frames 60
  --min-fps 60` atteint 187.263 FPS p95 sur macOS. Le workflow CI
  `textile-reference-benchmark` répète la mesure sur Ubuntu, macOS et Windows
  pendant 600 frames et archive un rapport JSON par plateforme.
- [x] Utiliser cette scène comme fixture de non-régression visuelle et
  fonctionnelle pour les futures tranches.

  Preuve : la fixture est régénérée octet par octet ; ses packets sont comparés
  entre resolver direct, Preview Runtime et paquet publié, et son graphe est
  simulé depuis les documents sauvegardés.

Gate : la scène complète est construite uniquement avec Asset Studio et Map
Studio, puis chargée depuis son paquet de map par Preview Runtime.

## Ordre de commits

Chaque case fonctionnelle forme un incrément séparé : ADR/C4 si le contrat
change, implémentation minimale, tests headless, validation visuelle ciblée,
`npm run validate`, commit et CI macOS/Windows/Linux. Une case n'est cochée
qu'après ces preuves.
