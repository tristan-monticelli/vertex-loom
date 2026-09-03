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

## Conséquences

Le travail porte d'abord sur l'orchestration et la présentation des commandes
existantes. Le modèle Entity v4, AnimationClip v3 et les commandes de session
restent la source de vérité. La timeline textuelle actuelle peut servir de
fallback avancé pendant la transition, mais ne constitue plus le parcours
nominal.
