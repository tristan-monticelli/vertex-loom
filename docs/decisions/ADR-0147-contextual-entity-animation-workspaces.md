# ADR-0147 — Workspaces contextuels Entity et Animation

## Statut

Accepté — 2026-09-03

## Contexte

Les contrats Entity et Animation sont éditables, mais leur interface expose la
structure de données avant l'intention de l'utilisateur. La création d'une
Entity compose tous ses blocs dans une modale. La création d'un clip ne crée
que ses métadonnées ; l'utilisateur doit ensuite configurer nœud, composant,
propriété, type et valeur dans un long inspecteur avant de voir une première
clé. Cette organisation prouve la couverture fonctionnelle sans fournir un
parcours de production efficace.

## Décision

Asset Studio adopte deux workspaces liés par la sélection courante :

- `Entity` conserve ensemble l'arbre de nœuds, le canvas et l'inspecteur ;
- `Animation` conserve ensemble la même cible, le canvas au playhead, les
  propriétés et une timeline en dock bas.

Une Entity peut être créée depuis un ou plusieurs visuels sélectionnés. Les
nœuds correspondants existent dès l'ouverture du workspace. Le drag and drop
vise l'arbre et le canvas, avec parent et position de destination visibles.

`Animer…` depuis une Entity crée ou ouvre un clip ciblé sans perdre la sélection
du nœud. Chaque propriété animable porte une commande de clé. Cette commande
crée automatiquement la piste typée manquante et capture la valeur courante au
playhead. En auto-key, une modification par gizmo ou inspecteur produit le même
résultat. Le binding brut, les tangentes, la composition additive et le
formulaire A→B restent dans un volet avancé.

L'Animation Graph s'ouvre sur un canevas d'états, pas sur le schéma persistant.
Chaque état affiche son nom, son clip et son rôle initial ; chaque transition est
une flèche. `Relier depuis cet état`, puis la sélection d'une carte cible, crée
une transition valide avec un identifiant généré. Les onglets de formulaire
restent disponibles pour les conditions, priorités et temps de sortie. Le layout
automatique du canevas est un état de présentation et ne modifie aucun contrat.

La Timeline expose aussi `Ajouter un événement au playhead` dans sa barre
principale. Cette action génère un nom `event-N` unique et crée un marqueur sans
audio ; le volet avancé reste disponible pour renommer, choisir un temps exact
ou associer un cue audio. Lecture/pause et déplacement direct des losanges sont
des gestes nominaux, pas des preuves remplacées par des mutations de session.

Les dialogues modaux se limitent au nom, au template ou à la confirmation. Ils
ne contiennent plus l'arbre, les blocs ou les pistes.

## Alternatives rejetées

- Agrandir les modales actuelles : cela conserve la rupture entre création et
  édition et ne résout ni la sélection contextuelle ni la lecture temporelle.
- Ajouter seulement des raccourcis au formulaire de clés : cela accélère une
  saisie technique mais ne rend pas visibles les pistes et leur synchronisation.
- Fusionner Entity et Animation dans un document persistant unique : cela
  casserait la réutilisation des clips et imposerait une migration sans bénéfice
  nécessaire pour l'UX.

## Critères d'acceptation

- Depuis un visuel sélectionné, une Entity visible et éditable est obtenue sans
  re-sélectionner la ressource ni saisir un identifiant technique.
- Canvas, arbre et inspecteur désignent toujours le même nœud.
- Depuis une Entity et son nœud, `Animer…` ouvre le clip ciblé et garde ce nœud.
- Une clé de transform est créée depuis sa propriété en une action ; la piste
  apparaît immédiatement à la position du playhead.
- Scrub, lecture et déplacement d'une clé mettent à jour la preview sans changer
  de document.
- Sauvegarde, reload, undo/redo et Preview Runtime conservent les mêmes bindings
  typés ; aucun schéma persistant n'est modifié par cette réorganisation.
- Un scénario de workflow part d'un visuel déjà indexé et n'utilise que les
  coordonnées de widgets et des événements SDL pour créer l'Entity, ouvrir la
  création d'animation et poser la première clé. Les appels directs de session
  vérifient le résultat après reload, mais ne préparent pas les documents.
- Le même scénario active l'auto-key, déplace le playhead puis le gizmo par
  événements SDL. La piste Position doit contenir les poses initiale et finale
  après rechargement ; modifier directement l'état du document ou du playhead
  depuis le harnais ne satisfait pas ce critère.
- L'Animation Graph rend tous les états et toutes les transitions dans le
  canevas. Une liaison peut être créée en choisissant la source puis la cible,
  sans saisir les identifiants techniques ; elle reste éditable dans
  l'inspecteur et persiste après rechargement.
- Le workflow transversal lit effectivement le clip, le met en pause, déplace
  une clé sur l'axe temporel et ajoute un événement au playhead par les widgets
  réels. Le temps corrigé et le marqueur doivent persister après rechargement.

## Conséquences

Le travail porte d'abord sur l'orchestration et la présentation des commandes
existantes. Le modèle Entity v4, AnimationClip v3 et les commandes de session
restent la source de vérité. La timeline textuelle actuelle peut servir de
fallback avancé pendant la transition, mais ne constitue plus le parcours
nominal.

La sélection multiple Entity reste un état d'interface ; elle ne change aucun
schéma. Une transformation de groupe remplace l'Entity validée dans une seule
commande undoable. L'arbre utilise les identifiants persistés pour afficher la
parenté, mais toutes ses mutations passent par les indices résolus de la session
et sont refusées si elles créent un cycle. La création depuis plusieurs visuels
réutilise les `EntityCreationBlock` existants et ne copie pas les ressources.

L'action contextuelle d'animation préremplit un nom lisible à partir de l'Entity
et du nœud sélectionnés ; la résolution de destination produit un identifiant
persistant unique. L'utilisateur peut accepter ce défaut et créer le clip sans
comprendre ni saisir cet identifiant.
