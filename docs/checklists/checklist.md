# Checklist de remise à niveau complète du Studio

## Règle de fermeture

Une case fonctionnelle ne peut être cochée que lorsque le même parcours est :

- défini par un contrat versionné ;
- éditable depuis le Studio propriétaire ;
- prévisualisable sans état de sélection caché ;
- couvert par undo, redo, dirty, autosave, récupération et sauvegarde atomique ;
- validé avant écriture ;
- rechargeable avec un résultat identique ;
- consommé par Map Studio et Preview Runtime lorsqu'il s'agit d'une capacité
  runtime ;
- couvert par des tests headless et un test end-to-end du parcours graphique ;
- documenté dans les C4 et ADR concernés ;
- livré dans un commit fonctionnel dédié.

Une validation technique verte ne ferme pas un gate UX.

## 0 — Verrouiller les parcours et le vocabulaire

- [ ] Décrire les parcours de référence avant toute modification de code.
- [ ] Définir le parcours « sélectionner une ressource interne ».
- [ ] Définir le parcours « créer une ressource sans perdre le document actif ».
- [ ] Définir le parcours « dupliquer une ressource et ses dépendances choisies ».
- [ ] Définir le parcours « supprimer une ressource après analyse des références ».
- [ ] Définir le parcours « créer une entité avec plusieurs artworks ».
- [ ] Définir le parcours « ajouter ou remplacer un artwork après création ».
- [ ] Définir le parcours « créer une animation pour une entité explicite ».
- [ ] Définir le parcours « programmer un joueur et un monstre avec le même
  système logique ».
- [ ] Définir le parcours « transformer une instance d'une entité vers une
  autre ».
- [ ] Définir le parcours « créer et modifier intégralement un artwork
  vectoriel natif ».
- [ ] Employer `Input bindings` uniquement pour les périphériques physiques.
- [ ] Employer `Behavior` pour la logique d'une entité, humaine ou non.
- [ ] Employer `Transformation` pour un remplacement d'entité avec transfert
  d'état explicite.

Gate : chaque parcours possède des entrées, sorties, erreurs, annulation et
critères de succès observables.

## 1 — Architecture de logique d'entité

- [ ] Mettre à jour le C4 Context pour inclure l'authoring de comportements.
- [ ] Mettre à jour le C4 Container avant d'ajouter le nouveau contrat.
- [ ] Ajouter un diagramme de composants pour l'éditeur et l'évaluateur de
  comportements.
- [ ] Ajouter un diagramme de séquence input/IA/événement → comportement →
  action → runtime.
- [ ] Écrire un ADR pour `BehaviorGraph v1`.
- [ ] Définir un document de comportement générique attachable à une entité.
- [ ] Utiliser des identifiants stables pour nœuds, ports, paramètres et
  connexions.
- [ ] Définir des ports et propriétés strictement typés.
- [ ] Refuser les références absentes, types incompatibles et cycles interdits.
- [ ] Définir les sources de signaux suivantes :
  - [ ] action joueur ;
  - [ ] décision IA ;
  - [ ] événement map ;
  - [ ] capteur ou trigger ;
  - [ ] timer ;
  - [ ] état ou propriété d'entité.
- [ ] Définir les nœuds de contrôle suivants :
  - [ ] condition ;
  - [ ] branche ;
  - [ ] séquence ;
  - [ ] délai ;
  - [ ] cooldown ;
  - [ ] état ;
  - [ ] transition.
- [ ] Définir les actions suivantes :
  - [ ] écrire une propriété ;
  - [ ] émettre un événement ;
  - [ ] lancer ou changer une animation ;
  - [ ] appliquer un mouvement ou une impulsion ;
  - [ ] activer une mécanique ;
  - [ ] demander une transformation d'entité.
- [ ] Ne coder aucune branche spéciale `player` ou `monster` dans l'évaluateur.
- [ ] Permettre à une même action sémantique d'être produite par un joueur, une
  IA ou un événement.
- [ ] Ajouter la sérialisation, le parseur strict et les migrations prévues.
- [ ] Ajouter la résolution au graphe de ressources et aux paquets de map.
- [ ] Ajouter une session d'édition avec CommandStack et sauvegarde atomique.
- [ ] Ajouter un éditeur de graphe dans Asset Studio.
- [ ] Ajouter une preview pas-à-pas avec journal borné des signaux et actions.
- [ ] Évaluer le même graphe dans Preview Runtime.
- [ ] Tester un comportement piloté par le joueur.
- [ ] Tester le même comportement piloté par une IA de monstre.
- [ ] Tester le même comportement piloté par un événement de map.
- [ ] Tester le déterminisme après sauvegarde, reload et replay.

Gate : le Studio programme un joueur et un monstre sans ajouter de code propre
à l'un des deux dans le runtime.

## 2 — Transformation d'une entité vers une autre

- [ ] Écrire un ADR pour `EntityTransformation v1`.
- [ ] Choisir si la transformation est une action du BehaviorGraph ou une
  ressource réutilisable référencée par cette action.
- [ ] Définir explicitement l'entité source et l'entité destination.
- [ ] Définir une politique de transfert pour :
  - [ ] transform monde ;
  - [ ] identifiant d'instance ;
  - [ ] couche et ordre Z ;
  - [ ] vitesse et état physique ;
  - [ ] propriétés d'instance compatibles ;
  - [ ] paramètres de comportement ;
  - [ ] animation et temps courant ;
  - [ ] cooldowns et timers ;
  - [ ] caméra et suivi éventuel.
- [ ] Définir les propriétés incompatibles comme reset, mapping explicite ou
  erreur de validation.
- [ ] Rendre l'opération atomique dans le runtime.
- [ ] Empêcher une frame intermédiaire sans entité valide.
- [ ] Préserver ou reconstruire proprement collision et mécanique.
- [ ] Exposer un formulaire typé dans Asset Studio.
- [ ] Permettre la sélection des entités source et destination depuis le
  Resource Explorer.
- [ ] Prévisualiser la transformation dans Asset Studio et Map Studio.
- [ ] Tester aller simple, aller-retour et chaîne de transformations.
- [ ] Tester les références manquantes et cycles non autorisés.
- [ ] Tester sauvegarde, reload, replay et publication de map.

Gate : une action configurée dans le Studio transforme une instance A vers B
avec la politique de transfert choisie, puis produit le même résultat après
reload et dans Preview Runtime.

## 3 — Transition uniforme entre documents

- [ ] Écrire un ADR pour la politique de changement de document actif.
- [ ] Remplacer les contrôles dispersés de `commands_.dirty()` par un service
  commun de transition.
- [ ] Couvrir les actions suivantes :
  - [ ] sélectionner une autre ressource ;
  - [ ] créer une ressource ;
  - [ ] importer une ressource ;
  - [ ] dupliquer une ressource ;
  - [ ] ouvrir un projet ;
  - [ ] créer un projet ;
  - [ ] fermer le Studio.
- [ ] Sauvegarder automatiquement le document courant lorsqu'il est valide.
- [ ] Continuer automatiquement lorsque la sauvegarde réussit.
- [ ] Ne jamais bloquer silencieusement après remplissage d'une modale.
- [ ] En cas d'échec, proposer `Retry`, `Discard` et `Cancel`.
- [ ] Garder le document courant et son historique lorsque l'utilisateur annule.
- [ ] Ne jamais publier partiellement la nouvelle ressource avant la résolution
  de la transition.
- [ ] Afficher la ressource et le chemin concernés dans la confirmation.
- [ ] Tester chaque action avec document clean, dirty valide et dirty invalide.
- [ ] Tester l'échec d'écriture et la conservation du document principal.
- [ ] Tester la création immédiatement après une édition non sauvegardée.

Gate : créer ou sélectionner une ressource sauvegarde l'ancienne sans blocage
si elle est valide et ne perd jamais les changements en cas d'échec.

## 4 — Resource Explorer unifié

- [ ] Déplacer ou reproduire l'explorateur dans le rail droit selon la maquette
  validée.
- [ ] Garder un seul composant de navigation réutilisable dans tous les prompts.
- [ ] Afficher dossiers logiques, types, noms, identifiants et états dirty.
- [ ] Ajouter recherche insensible à la casse et filtres par type.
- [ ] Ajouter navigation clavier et sélection persistante.
- [ ] Ajouter une barre d'actions contextuelle :
  - [ ] New ;
  - [ ] Import ;
  - [ ] Duplicate ;
  - [ ] Rename ;
  - [ ] Replace references ;
  - [ ] Reveal on disk ;
  - [ ] Copy ID ;
  - [ ] Copy path ;
  - [ ] Delete.
- [ ] Ajouter les mêmes actions dans un menu contextuel.
- [ ] Définir une commande générique de duplication par type de ressource.
- [ ] Générer un nouvel identifiant stable et un nouveau chemin à la duplication.
- [ ] Permettre de choisir la copie superficielle ou la duplication de certaines
  dépendances.
- [ ] Réécrire uniquement les références internes choisies dans la copie.
- [ ] Analyser les références entrantes avant suppression.
- [ ] Bloquer la suppression lorsqu'elle casserait le projet sans stratégie
  choisie.
- [ ] Proposer remplacement des références, suppression en cascade explicitée
  ou annulation.
- [ ] Demander confirmation avant toute suppression matérielle.
- [ ] Ne jamais supprimer automatiquement une source externe ou partagée.
- [ ] Ajouter undo pour les opérations récupérables du registre.
- [ ] Tester les opérations sur chaque type de ressource.
- [ ] Tester collision d'identifiant, cycles, fichiers absents et échec disque.

Gate : toutes les ressources peuvent être parcourues et administrées depuis le
même rail sans saisie manuelle d'identifiants.

## 5 — Textures et browsing cohérent

- [ ] Utiliser le picker recherchable commun dans tous les champs texture.
- [ ] Remplacer la combo simple du prompt vectoriel.
- [ ] Permettre de changer la texture d'un fill image après création.
- [ ] Permettre de choisir une texture pour un matériau après création.
- [ ] Permettre d'ajouter ou remplacer une texture sur un nœud d'entité.
- [ ] Afficher miniature, dimensions, format, chemin et dépendances.
- [ ] Ajouter `Open in Resource Explorer` depuis chaque référence texture.
- [ ] Conserver la source PNG byte-for-byte.
- [ ] Garder crop, pivot, transform et filtrage non destructifs.
- [ ] Rendre tous les paramètres de `RasterView` éditables après import.
- [ ] Afficher simultanément source complète, crop actif et résultat final.
- [ ] Tester navigation avec beaucoup de textures et noms similaires.
- [ ] Tester changement de texture avec undo, redo, save et reload.

Gate : une texture peut être trouvée, ouverte, remplacée, recadrée et réutilisée
depuis chaque contexte sans saisir son identifiant.

## 6 — Matériaux entièrement éditables

- [ ] Ajouter `material` aux documents dirty supportés par ProjectSession.
- [ ] Ajouter le chemin courant, autosave et récupération des matériaux.
- [ ] Ajouter une commande de mutation validée du matériau sélectionné.
- [ ] Exposer après création :
  - [ ] nom ;
  - [ ] couleur ;
  - [ ] opacité ;
  - [ ] blend mode ;
  - [ ] texture ;
  - [ ] motif vectoriel ;
  - [ ] offset UV ;
  - [ ] échelle UV ;
  - [ ] rotation UV ;
  - [ ] pivot UV si le contrat le conserve.
- [ ] Prévisualiser chaque changement immédiatement.
- [ ] Afficher les entités et composants qui utilisent le matériau.
- [ ] Ajouter undo, redo, dirty, autosave, récupération et sauvegarde atomique.
- [ ] Tester chaque propriété individuellement et en combinaison.
- [ ] Tester le matériau dans Asset Studio, Map Studio et Preview Runtime.

Gate : aucun paramètre disponible à la création ne devient immuable après
publication.

## 7 — Entités et artworks dans le rail droit

- [ ] Remplacer la liste plate par un véritable arbre de nœuds d'entité.
- [ ] Ajouter les actions de nœud : Add, Duplicate, Reparent, Reorder et Delete.
- [ ] Demander confirmation avant suppression d'un nœud et afficher ses enfants.
- [ ] Exposer un inspecteur complet du drawable de chaque nœud.
- [ ] Permettre après création les kinds `none`, `texture`, `vector` et
  `visualComponent`.
- [ ] Permettre `Add`, `Replace`, `Clear` et `Open` sur la ressource visuelle.
- [ ] Permettre d'ajouter, remplacer ou retirer un matériau compatible.
- [ ] Exposer variante, ancre et overrides d'un composant visuel.
- [ ] Préserver les références compatibles lors d'un changement de kind.
- [ ] Demander confirmation avant d'effacer des overrides incompatibles.
- [ ] Exposer transform, pivot, Z, visibilité et verrouillage du nœud.
- [ ] Ajouter sélection et gizmos de nœud dans le canvas d'entité.
- [ ] Permettre glisser-déposer d'un artwork depuis le Resource Explorer vers :
  - [ ] un nœud existant ;
  - [ ] un nouveau nœud racine ;
  - [ ] un nouveau nœud enfant.
- [ ] Exposer les contraintes, IK, déformation, XPBD et state machine dans des
  sections avancées éditables plutôt que seulement dans le contrat JSON.
- [ ] Attacher explicitement un BehaviorGraph à l'entité.
- [ ] Tester une entité multi-nœuds combinant texture, vectoriel et composant.
- [ ] Tester duplication, reparentage, changement d'artwork et reload.

Gate : une entité vide peut être entièrement assemblée et modifiée depuis le
rail droit sans recréation ni édition JSON.

## 8 — Animations avec cible explicite

- [ ] Écrire un ADR remplaçant la cible implicite issue de l'ancienne sélection.
- [ ] Ajouter un sélecteur d'entité au prompt de création d'animation.
- [ ] Permettre une animation générique sans cible uniquement comme choix
  explicite.
- [ ] Persister une cible de preview ou une association versionnée adaptée.
- [ ] Afficher et modifier la cible dans l'inspecteur du clip.
- [ ] Ne jamais conserver silencieusement une entité sélectionnée précédemment.
- [ ] Lister les nœuds réels de l'entité cible au lieu de demander `Node id` en
  texte libre.
- [ ] Construire les propriétés depuis le registre de descripteurs de la cible.
- [ ] Exposer transform, matériau, fill, image fill et paramètres de composant.
- [ ] Signaler immédiatement les bindings devenus invalides.
- [ ] Ajouter une action pour réparer ou remplacer un binding invalide.
- [ ] Permettre de créer le clip depuis l'entité sélectionnée.
- [ ] Permettre d'ajouter un clip à l'entité depuis son rail.
- [ ] Implémenter le geste valeur A au temps A → valeur B au temps B.
- [ ] Ajouter sélection multiple, copier/coller et snapping de clés.
- [ ] Ajouter tangentes et easing après décision de contrat.
- [ ] Prévisualiser le clip sur l'entité dès sa création.
- [ ] Tester création ciblée, changement de cible et cible absente.
- [ ] Tester changements de fill et transform d'image dans la timeline.
- [ ] Tester sauvegarde, reload, state machine et runtime.

Gate : créer une animation avec une entité affiche immédiatement cette entité
et ne dépend jamais de l'ordre antérieur des clics.

## 9 — Inputs physiques réellement personnalisables

- [ ] Conserver `InputDocument` séparé du BehaviorGraph.
- [ ] Ajouter des noms lisibles de touches et boutons à la place des seuls codes.
- [ ] Permettre un nombre quelconque d'actions et de bindings.
- [ ] Ajouter Add, Duplicate, Reorder et Remove dans la modale de création.
- [ ] Générer des identifiants uniques au lieu de répéter `action`.
- [ ] Empêcher qu'un nouvel item invalide rende toute la modale incompréhensible.
- [ ] Ajouter capture interactive de la prochaine touche ou du prochain bouton.
- [ ] Ajouter axes, seuils et dead zones.
- [ ] Ajouter combinaisons et modificateurs.
- [ ] Ajouter contextes ou profils si leur besoin est confirmé par ADR.
- [ ] Afficher les BehaviorGraph qui consomment chaque action sémantique.
- [ ] Permettre le remapping sans changer le comportement de l'entité.
- [ ] Tester plusieurs bindings par action et plusieurs documents d'input.
- [ ] Tester clavier, gamepad, axes, duplication et suppression.

Gate : l'utilisateur définit librement ses actions physiques, puis les branche
sur un comportement sans modifier le runtime.

## 10 — Personnalisateur vectoriel complet

- [ ] Mettre à jour le C4 et écrire les ADR manquants avant les nouveaux
  contrats ou commandes.
- [ ] Ajouter un arbre ordonné de nœuds vectoriels.
- [ ] Ajouter les actions Add, Duplicate, Group, Reparent, Reorder et Delete.
- [ ] Ajouter les primitives rectangle, ellipse, ligne et path.
- [ ] Permettre de changer une primitive en path éditable lorsque possible.
- [ ] Ajouter un outil plume pour :
  - [ ] ajouter un point ;
  - [ ] insérer un point sur un segment ;
  - [ ] déplacer un point ;
  - [ ] supprimer un point ;
  - [ ] convertir ligne en courbe ;
  - [ ] convertir courbe en ligne ;
  - [ ] ouvrir un contour ;
  - [ ] fermer un contour.
- [ ] Afficher et modifier les poignées Bézier directement sur le canvas.
- [ ] Supporter poignées liées, symétriques et libres.
- [ ] Ajouter sélection simple et multiple de points.
- [ ] Ajouter déplacement, rotation et échelle d'une sélection de points.
- [ ] Exposer les bounds des primitives.
- [ ] Exposer le type de fill après création.
- [ ] Permettre fill `none`, couleur et image à tout moment.
- [ ] Permettre de changer texture, fit, offset, scale, rotation, pivot,
  opacité et déformation du fill image.
- [ ] Rendre la transform du fill indépendante de celle de la forme.
- [ ] Exposer ajout, retrait et modification du stroke.
- [ ] Exposer couleur, largeur, join et cap du stroke.
- [ ] Exposer parent, clip, visibilité, verrouillage et ordre des nœuds.
- [ ] Afficher les clips imbriqués fidèlement sur le canvas.
- [ ] Relier toutes les propriétés animables au registre de descripteurs.
- [ ] Faire passer chaque geste par CommandStack avec fusion continue.
- [ ] Tester chaque outil en undo, redo, autosave, récupération et reload.
- [ ] Ajouter des tests géométriques pour insertion et suppression de points.
- [ ] Ajouter des tests end-to-end des interactions canvas.
- [ ] Comparer les draw packets Asset Studio, Map Studio et Preview Runtime.

Gate : l'utilisateur crée et personnalise complètement un artwork natif,
incluant paths Bézier, fill et stroke, sans modifier le JSON ni préparer un SVG
externe.

## 11 — Cohérence générale de l'inspecteur

- [ ] Aucun paramètre proposé dans un prompt ne doit disparaître après création.
- [ ] Utiliser les mêmes composants de formulaire pour création et édition.
- [ ] Afficher la validation au niveau du champ concerné.
- [ ] Afficher la raison exacte d'un bouton désactivé.
- [ ] Conserver une barre de statut, mais ne pas y cacher une erreur bloquante.
- [ ] Ajouter focus et scroll automatiques vers le premier champ invalide.
- [ ] Ajouter tooltips pour les propriétés techniques.
- [ ] Afficher les unités pour toutes les valeurs numériques.
- [ ] Utiliser les noms visibles comme interaction principale et les IDs comme
  information secondaire.
- [ ] Ajouter raccourcis clavier et commandes de menu cohérents.
- [ ] Rendre les actions destructives visuellement distinctes.
- [ ] Vérifier le comportement aux tailles minimales de fenêtre.
- [ ] Vérifier navigation clavier et contraste.

Gate : les mêmes conventions de sélection, édition, validation et actions sont
utilisées pour tous les types de ressources.

## 12 — Tests UX et end-to-end

- [ ] Choisir et documenter un outil d'automatisation pour SDL2/Dear ImGui.
- [ ] Mettre à jour `docs/02-quality-strategy.md` avec la commande réelle.
- [ ] Ajouter un mode de test avec IDs de widgets stables.
- [ ] Ajouter une fixture de projet contenant plusieurs ressources de chaque
  type.
- [ ] Automatiser les parcours suivants :
  - [ ] parcourir et sélectionner une texture ;
  - [ ] modifier un crop puis créer une autre ressource ;
  - [ ] vérifier l'autosave de l'ancienne ressource ;
  - [ ] dupliquer et renommer une ressource ;
  - [ ] analyser puis annuler une suppression ;
  - [ ] ajouter un artwork à une entité existante ;
  - [ ] créer une animation ciblant cette entité ;
  - [ ] créer plusieurs actions et plusieurs bindings ;
  - [ ] programmer un comportement de joueur ;
  - [ ] programmer un comportement de monstre ;
  - [ ] transformer une entité A vers B ;
  - [ ] créer et éditer un path Bézier ;
  - [ ] changer fill, texture et stroke après création.
- [ ] Capturer diagnostics et screenshots lors d'un échec.
- [ ] Exécuter les parcours sur macOS, Windows et Linux.
- [ ] Garder les tests headless de contrats en complément, pas en remplacement.

Gate : les parcours qui ont échoué lors de l'audit sont exécutés
automatiquement sur les trois plateformes.

## 13 — Documentation et fermeture

- [ ] Mettre à jour `docs/00-project-brief.md` si le périmètre BehaviorGraph et
  transformations change les critères de succès.
- [ ] Mettre à jour C4 Context et Container avant chaque changement structurel.
- [ ] Ajouter ou mettre à jour les diagrammes de composants concernés.
- [ ] Ajouter un ADR par nouveau contrat ou politique persistante.
- [ ] Supprimer les affirmations de gate validé qui ne disposent pas d'une
  preuve UX reproductible.
- [ ] Réconcilier cette checklist avec `remaining-roadmap.md`.
- [ ] Réconcilier cette checklist avec `studio-first-vertical-slices.md`.
- [ ] Exécuter `npm run validate` après chaque étape fonctionnelle.
- [ ] Exécuter le smoke OpenGL après toute modification du renderer.
- [ ] Exécuter les benchmarks après toute modification significative du rendu
  ou du runtime.
- [ ] Auditer le diff, les voisins, les régressions et les effets de bord.
- [ ] Vérifier `AGENTS.md`, `CLAUDE.md`, `.codex/hooks.json` et les hooks modifiés.
- [ ] Vérifier qu'aucune suppression n'a été réalisée sans confirmation.
- [ ] Faire un commit conventionnel par étape fonctionnelle vérifiée.
- [ ] Confirmer le résultat visuel et fonctionnel avant de déclarer le chantier
  terminé.

## Ordre d'exécution recommandé

- [ ] Lot 1 : transition uniforme entre documents.
- [ ] Lot 2 : Resource Explorer et duplication générique.
- [ ] Lot 3 : textures, matériaux et édition complète des drawables d'entité.
- [ ] Lot 4 : cible explicite des animations.
- [ ] Lot 5 : BehaviorGraph et inputs physiques séparés.
- [ ] Lot 6 : transformation d'entité.
- [ ] Lot 7 : personnalisateur vectoriel complet.
- [ ] Lot 8 : tests end-to-end multiplateformes et réconciliation documentaire.

Le chantier est terminé uniquement lorsque les huit lots et tous leurs gates
sont cochés avec preuves de tests et commits associés.
