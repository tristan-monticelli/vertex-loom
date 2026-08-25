# Checklist restante après audit produit

## Intention corrigée

Vertex Loom est un atelier et un moteur 2D à rendu vectoriel. « Vectoriel » ne
signifie pas que le format interne doit être du SVG : le SVG est un format
d’échange possible, tandis que le projet conserve une géométrie native,
éditable, stable et animable.

La géométrie définit la silhouette, le masque et le contour esthétique. Son
remplissage peut être une couleur, un matériau, un motif ou une image locale.
Une image placée dans une forme reste découpée par cette forme ; son cadrage et
sa transformation sont éditables indépendamment du contour.

Le produit n’utilise pas de spritesheets. Le lecteur Aseprite, le packer
d’atlas et `SpriteSheetDefinition v1` ont été retirés après inventaire et
confirmation explicite ; ADR-0025 enregistre cette décision.

Chaque opération de création distincte possède son propre prompt ou assistant
typé. Un import de source n’est pas présenté comme la création d’un document.

## Audit de l’état actuel

| État | Constat et preuve | Correction attendue |
| --- | --- | --- |
| CONFORME | La création de projet valide le nom, l’identifiant généré et une destination vide dans `project_creator.cpp`. | Conserver la sûreté du stockage. |
| CONFORME | `CreateProjectPrompt` demande destination, nom, unités monde, preset et `pixelsPerUnit`, puis affiche l’identifiant calculé, les erreurs et le résumé avant création. | Conserver les tests headless du modèle. |
| CONFORME | Les états PNG et SVG sont isolés, l’aperçu précède la publication et `Add existing` sélectionne désormais une ressource indexée sans créer de document. | Conserver la séparation sélection, validation/décodage, aperçu et publication. |
| PARTIEL | `VectorAsset v2 native` persiste primitives, chemins, fills, contours et clips et produit désormais des draw packets headless ; l’édition de sommets et le cache GPU restent ouverts. | Livrer personnalisateur et cache de géométrie. |
| MANQUE | L’aperçu SVG est rasterisé et téléversé en texture ; aucun nœud, contour, fill ou masque n’est éditable. | Construire un renderer de géométrie native et le personnalisateur intégré. |
| MANQUE | Il n’existe ni contrat `AnimationClip`, ni timeline, ni liaison de propriété générique. | Livrer le registre de propriétés typées et l’évaluateur de keyframes avant toute animation spécialisée. |
| CONFORME | Le pipeline sprite a été retiré du build, des contrats, du validateur et de l’interface par ADR-0025. | Empêcher sa réintroduction dans les futurs contrats. |
| CONFORME | `CommandStack`, sauvegarde atomique, autosave et récupération sont testés sans fenêtre. | Réutiliser ces services pour chaque futur document éditable. |
| CONFORME | Le registre de ressources vérifie types, doublons, documents manquants et cycles. | Étendre ses types aux formes, fills, animations, entités et maps. |
| CONFORME | La branche passe `npm run validate` et la matrice macOS/Windows/Linux. | Garder ce gate après chaque incrément. |

## Décisions architecturales acceptées

Les cases de cette section prouvent une décision documentée, pas son
implémentation. Les preuves fonctionnelles restent exclusivement dans les
étapes et gates ci-dessous.

- [x] Une action `Create…` ouvre un prompt propre au type créé : projet,
  artwork vectoriel, matériau/fill, entité, animation, prefab, map ou scène.
- [x] Chaque prompt explique le document produit, propose des valeurs par
  défaut, valide en direct et affiche un résumé avant publication.
- [x] `Import…` choisit et normalise une source ; `Create…` produit un document
  éditable ; `Add existing…` référence une ressource déjà enregistrée.
- [x] Le format vectoriel interne ne dépend ni du DOM SVG ni d’une
  rasterisation persistante.
- [x] Le SVG importé peut rester lié et opaque ou être converti explicitement
  vers les primitives prises en charge ; le choix est visible et réversible
  tant que le document n’est pas publié.
- [x] Les images intégrées ou importées sont des ressources locales explicites.
  Une forme peut les employer comme fill avec transform UV, mode de cadrage,
  opacité et clipping par le contour.
- [x] Les références externes d’un SVG ne sont jamais suivies silencieusement :
  elles sont refusées ou copiées après choix explicite dans le prompt.
- [x] La spritesheet n’est pas un contrat requis par le personnalisateur,
  l’animation, Map Studio ou le runtime cible.
- [x] Toute mutation éditable passe par `CommandStack`, autosave et sauvegarde
  atomique ; les gestes continus fusionnent en une seule commande.
- [x] La timeline découvre les propriétés via un registre typé ; elle ne
  contient pas de `switch` métier par nom de propriété.

## Étape A — Recaler les contrats et l’architecture

- [x] Écrire un ADR qui remplace l’orientation sprite-first par l’authoring
  vectoriel natif et documente le statut hérité d’ADR-0021.
- [x] Écrire un ADR pour le modèle `shape + fill + stroke + clip` et les images
  contenues dans les formes.
- [x] Écrire un ADR pour les prompts de création typés et la séparation
  `Create / Import / Add existing`.
- [x] Mettre à jour le C4 Container et les composants Asset Studio, format
  projet et rendu avant le prochain changement structurel.
- [x] Définir la migration `VectorAsset v1 -> v2` sans perte : les SVG actuels
  deviennent `sourceKind = linkedSvg`.
- [x] Décider par inventaire que le pipeline devient entièrement obsolète et le
  retirer après confirmation explicite.

Gate : la documentation ne présente plus les sprites comme une fondation du
runtime et chaque futur document possède un propriétaire clair.

## Étape B — Hub de création et prompts dédiés

- [x] Remplacer la colonne d’actions d’import par des sections distinctes
  `Create`, `Import` et `Add existing`.
- [x] Rendre `Add existing` fonctionnel avec un sélecteur de ressources
  enregistrées ; la sélection recharge le document sans en créer un nouveau.
- [x] Enrichir `Create project` : destination, nom, identifiant automatique, unités,
  pixels par unité, preset de projet et résumé final.
- [x] Ajouter le prompt `Create vector artwork` : nom, identifiant automatique, taille de
  travail, origine, unités, première forme, fill initial et couleur.
- [x] Publier réellement l’artwork créé comme `VectorAsset v2 native` par
  sauvegarde atomique après validation complète du prompt et du document.
- [ ] Ajouter les prompts dédiés pour matériau/fill, entité et animation après
  livraison de leurs contrats ; les boutons actuels sont uniquement visibles
  et désactivés.
- [x] Garder chaque état de prompt isolé ; fermer ou annuler un assistant ne
  doit pas modifier le projet ni polluer le prompt suivant.
- [x] Résoudre automatiquement les conflits d’identifiants par suffixe et
  afficher l’identifiant calculé ainsi que la destination avant confirmation.
- [x] Tester les modèles de prompt et leurs validations sans Dear ImGui.
- [x] Sortir de `main.cpp` l’orchestration des imports, des previews OpenGL et
  du legacy afin de n’y laisser que le routage et le rendu des widgets.

Gate : deux opérations différentes ne partagent ni libellé ambigu, ni état
caché, ni publication implicite.

État : gate partiellement validé. La séparation des libellés, des modèles et
des états, la publication d’artwork, `Add existing` et l’extraction du workflow
d’import sont livrées.

## Étape C — `VectorAsset v2` natif

Tranche livrée : dimensions et origine du document, nœuds et formes à
identifiants stables, visibilité, verrouillage, transform, rectangle, ellipse,
fill couleur, transparent ou image locale, cadrage et transform du fill,
opacité, liaison à la déformation, round-trip, publication atomique et
validation du graphe headless. Les cases ci-dessous restent ouvertes
lorsqu’elles contiennent encore des variantes non livrées.

- [x] Définir des identifiants stables pour document, nœuds, formes et
  ressources de fill.
- [x] Stocker rectangle, ellipse, ligne et chemin `move/line/cubic/close`.
- [x] Stocker fill couleur, fill image, transform du fill, opacité, contour,
  largeur, jointure, extrémité et ordre de dessin.
- [x] Autoriser une image locale comme contenu d’une forme sans transformer la
  forme en sprite ou en bitmap autonome.
- [x] Conserver hiérarchie, groupes, visibilité, verrouillage, transform, pivot
  et clipping.
- [x] Limiter la première version aux contours simples ; détecter les
  auto-intersections au lieu de produire une géométrie ambiguë.
- [x] Aplatir les Bézier selon une tolérance fournie par la vue, trianguler de
  façon déterministe et mettre en cache la géométrie par version de document.
- [x] Charger les SVG liés avec NanoSVG ; convertir explicitement les chemins
  cubiques, fills couleur et contours simples, et signaler gradients ou paints
  non pris en charge avant validation.
- [ ] Ajouter round-trip, migration v1, validation stricte, chemins sûrs,
  tessellation et rendu headless.

Gate : un artwork combinant contour vectoriel et image remplissante est créé,
sauvegardé, rechargé et rendu sans atlas ni rasterisation persistante.

Le convertisseur NanoSVG produit désormais un `VectorAsset v2 native` pour les
chemins cubiques, fills couleur et contours simples ; les pertes sont
diagnostiquées avant publication. Asset Studio expose cette conversion comme
commande undoable, avec retour au SVG lié et publication native atomique. Le
payload headless de fill image et la triangulation de sa silhouette sont
désormais disponibles. Les draw packets appliquent les transforms locales et
parentes avant d’exposer leurs sommets monde. Le backend OpenGL 3 compile,
initialise et dessine les triangles de fills couleur dans le canvas Asset
Studio ainsi que les contours ouverts/fermés. Asset Studio résout maintenant
les `TextureAsset` locaux à la demande et met leurs handles GPU en cache ; la
validation visuelle complète du gate reste ouverte. Les `clipNodeId` simples
sont maintenant appliqués par stencil dans le backend ; clips imbriqués,
gizmos de clip et validation visuelle complète restent ouverts.

## Étape D — Personnalisateur intégré

Tranche livrée : navigateur persistant, sélection d'un artwork natif,
prévisualisation rectangles/ellipses, pan, zoom sous le curseur et édition des
nœuds avec historique, sauvegarde, autosave et récupération. Les cases
ci-dessous restent ouvertes jusqu'au support complet de tous les nœuds et des
draw packets du renderer.

- [ ] Ajouter canvas avec pan, zoom sous le curseur, cadrage, grille et unités.

Le canvas natif affiche désormais une grille adaptative et son pas en unités
monde ; le cadrage explicite et les interactions de gizmo restent ouverts.
- [ ] Ajouter arbre de calques/nœuds, sélection multiple, verrouillage,
  visibilité, groupes et ordre Z.

L’inspecteur permet désormais de choisir le parent et le clip d’un nœud natif
avec historique et validation ; l’arbre de calques, la sélection multiple et
les groupes restent ouverts.
- [ ] Ajouter plume Bézier, primitives, édition des nœuds et poignées
  liées/libres, ouverture et fermeture de contour.
- [ ] Ajouter gizmos rotation, échelle, pivot et transform du fill
  indépendamment du transform de la forme.
- [ ] Ajouter sélecteur de fill : couleur, image, motif et matériau référencé.
- [ ] Ajouter modes de cadrage image (`contain`, `cover`, `stretch`, libre),
  offset, rotation et échelle dans le masque.
- [ ] Ajouter presets réutilisables sans figer les propriétés dans le code.
- [ ] Brancher toutes les mutations à undo/redo, fusion continue, dirty,
  autosave et récupération.
- [ ] Ajouter une prévisualisation fidèle utilisant les mêmes draw packets que
  le futur runtime.

Gate : l’utilisateur fabrique et personnalise entièrement un asset esthétique
dans Asset Studio sans préparer une spritesheet externe.

Le canvas permet désormais de déplacer le nœud sélectionné par glisser gauche,
via la même commande réversible que l’inspecteur ; rotation, échelle, pivot et
transform du fill restent ouverts.

## Étape E — Keyframes génériques et intelligentes

- [x] Définir `PropertyDescriptor` : identifiant stable, chemin affiché, type,
  lecture, écriture, bornes, unité, animabilité et mode de composition.
- [x] Définir `PropertyBinding` par `nodeId + componentId + propertyId`, jamais
  par pointeur, index temporaire ou chaîne interprétée au runtime.
- [x] Supporter scalaire, `Vec2`, couleur, booléen et référence de ressource.
  Angle, transform et paramètres de fill restent à relier aux descripteurs.
- [x] Définir `AnimationClip v1` : durée, boucle, markers, pistes typées et clés.
- [x] Supporter step, linear et cubic ; tangentes, easing et rotation par
  chemin angulaire court restent à définir.
- [ ] Lister automatiquement dans la timeline toutes les propriétés déclarées
  animables par le registre.
- [x] Définir des états et transitions déterministes avec conditions
  booléennes/numériques, priorités et exit time normalisé.
- [x] Définir l’ordre des contraintes copy-transform, limites et look-at et
  refuser les cycles avant résolution.
- [x] Ajouter IK 2D par FABRIK avec racine fixe, tolérance et nombre maximal
  d’itérations déterministe.
- [x] Ajouter le socle XPBD distance/pin avec compliance, lambdas et
  quantification après sous-pas.
- [ ] Un geste « déplacer de A à B » capture la valeur de départ au temps A et
  la valeur d’arrivée au temps B, puis crée ou met à jour la piste générique.
- [x] Supporter création explicite de clé, déplacement et suppression de clés
  par commandes undo/redo. Auto-key, multi-sélection, copier/coller, snapping
  et scrubbing restent à ajouter.
- [ ] Permettre valeurs absolues, offsets relatifs et composition additive sans
  coder une animation particulière dans le moteur.
- [ ] Prévisualiser une animation sur n’importe quelle propriété compatible et
  expliquer les liaisons devenues invalides après modification du document.
- [ ] Évaluer dans un ordre déterministe : valeurs de base, pistes, contraintes,
  déformations, puis simulation.
- [ ] Tester interpolation, tangentes, bindings, renommage, suppression de
  cible, reload et replay déterministe.

Gate : le même éditeur crée translation, rotation, changement de fill et
transformation d’image de A vers B sans ajouter de code spécifique à ces cas.

## Étape F — Entités, matériaux et déformations

- [x] Définir `MaterialDefinition v1` autour des fills et contours, sans
  dépendance sprite.
- [x] Définir `EntityDefinition v1` avec nœuds stables, parentage, transforms,
  drawables vectoriels et références d’artworks.
- [ ] Refuser cycles, références invalides et valeurs non finies.
- [ ] Ajouter contraintes copy-transform, limites, look-at et ordre explicite.
- [ ] Ajouter maillage triangulé, poids, FABRIK 2D et solveur XPBD unifié.
- [ ] Quantifier l’état simulé après chaque sous-pas pour le replay portable.
- [x] Ajouter `fabric_asset_preview` headless basé sur les draw packets réels.

Gate : une entité vectorielle personnalisée, animée et déformée conserve le
même résultat visible après sauvegarde, rechargement et replay.

## Étape G — Map Studio

- [ ] Réutiliser la session, les prompts typés, le personnalisateur de
  propriétés et le renderer vectoriel d’Asset Studio.
- [ ] Définir maps, calques, prefabs, instances, collisions et triggers sans
  référence obligatoire à une frame de sprite.
- [ ] Indexer 100 000 éléments par chunks de 64 × 64 unités et viser 10 000
  éléments visibles.
- [ ] Ajouter placement, overrides typés, snapping, profondeur, sélection,
  visibilité et verrouillage.
- [ ] Intégrer Box2D 3.1.1, formes validées, capteurs et événements nommés.
- [ ] Appliquer commandes, autosave, récupération et validation headless à
  toutes les opérations.

Gate : une map vectorielle de référence est éditable, sauvegardée, validée et
inspectable avec collisions et événements.

## Étape H — Preview Runtime

- [ ] Valider tout le graphe avant création de la fenêtre.
- [ ] Charger artworks natifs, fills image, matériaux, entités, animations,
  maps, contraintes et solveurs sans conversion manuelle.
- [ ] Ajouter cache vectoriel, batching des fills image, culling par chunk, tri
  stable et caméra interpolée.
- [ ] Exécuter Box2D et XPBD à 60 Hz fixe puis rendre avec interpolation.
- [ ] Ajouter overlays, smoke test, benchmark et replay par checkpoints.
- [ ] Tenir 60 FPS p95 à 1440 × 900 avec 10 000 éléments visibles sur la scène
  de référence.

Gate : une map produite par les deux ateliers fonctionne directement sur les
trois plateformes.

## Étape I — Runtime jouable

- [ ] Ajouter actions clavier/manette, scènes, transitions et points d’entrée.
- [ ] Ajouter contrôleur de personnage, caméra, zones et événements en
  réutilisant les propriétés et animations génériques.
- [ ] Ajouter audio PCM WAV, progression versionnée atomique et replay gameplay.
- [ ] Créer uniquement des niveaux, personnages et artworks originaux.

Gate final : prototype original jouable, déterministe et construit uniquement
depuis les documents produits par Asset Studio et Map Studio.

## Validation continue

- [ ] Mettre à jour ADR et C4 avant chaque nouveau contrat, module ou dépendance.
- [ ] Ajouter migration, round-trip, parseur strict et validateur pour chaque
  version de document.
- [ ] Tester chaque commande d’éditeur avec execute, undo, redo, merge et
  restauration de l’état clean.
- [ ] Tester autosave, récupération et sauvegarde atomique sans fenêtre.
- [ ] Exécuter `npm run validate` avant chaque commit.
- [ ] Exiger macOS, Windows et Linux verts avant de cocher un gate.
- [x] Retirer le code sprite uniquement après inventaire et confirmation
  explicite, sans toucher à des ressources utilisateur.
