# ADR-0114 — BehaviorGraph v1 générique

## Statut

Accepté — 2026-08-26.

## Contexte

Le runtime reliait directement trois actions nommées `move_left`,
`move_right` et `jump` au personnage. Cette convention empêchait d'authorer un
nombre libre d'actions et ne fournissait aucune logique équivalente aux
monstres, événements ou transformations.

## Décision

`InputDocument` reste limité aux périphériques et produit des actions
sémantiques libres. La logique est persistée séparément dans un
`BehaviorGraph v1`, attachable à une entité par référence typée `behavior`.

Chaque nœud, port, paramètre et connexion possède un identifiant stable. Les
ports sont strictement typés (`signal`, `bool`, `integer`, `float`, `text`,
`vec2`, `resource`) et la validation refuse doublons, références absentes,
connexions incompatibles et cycles de flux.

Les sources intégrées sont action, décision IA, événement map, trigger, timer
et propriété. Les contrôles sont condition, branche, séquence, délai, cooldown,
état et transition. Les sorties sont écriture de propriété, émission
d'événement, animation, mouvement/impulsion, activation de mécanique et demande
de transformation.

Un évaluateur stateful est instancié par instance d'entité. Il reçoit un signal
normalisé avec sa source et son identifiant, puis produit un lot ordonné
d'actions typées et une trace bornée. Il ne contient aucune branche `player` ou
`monster`. Les cycles de flux sont interdits en v1 ; les transitions d'état
utilisent le stockage explicite de l'évaluateur.

Asset Studio présente ce contrat sous forme de canevas. Chaque carte affiche le
type du nœud et ses ports ; les connexions sont des flèches. L'utilisateur
choisit un port de sortie puis une carte cible compatible : l'interface résout
le port d'entrée, génère l'identifiant de connexion et soumet la commande à la
validation existante. Les listes de propriétés et le formulaire de connexion
restent des inspecteurs avancés. Le layout du canevas est éphémère et ne change
pas `BehaviorGraph v1`.

Une palette recherchable expose les types de nœuds au-dessus du canevas. Un
clic crée le type choisi avec un identifiant lisible dérivé du type et rendu
unique ; le champ d'identifiant libre n'appartient plus au parcours nominal.

## Conséquences

- Une action identique peut provenir du joueur, d'une IA ou d'une map.
- Le remapping physique ne change pas le comportement.
- Le contrôleur physique optionnel consomme un frame normalisé et ne contient
  plus les noms `move_left`, `move_right` ou `jump`.
- Le graphe est fermé transitivement dans les paquets avec l'entité.
- Les temporisations et cooldowns ne sont jamais persistés dans le document
  d'auteur ; replay et pas fixe fournissent leur déterminisme.
- Une migration future doit augmenter `schemaVersion`; les champs inconnus ou
  versions non supportées sont refusés.
- Un scénario E2E doit partir d'un seul nœud, ajouter la cible depuis la palette,
  utiliser les coordonnées des widgets du canevas pour les relier,
  sauvegarder/recharger, puis exécuter le signal attendu. Un nœud cible ou une
  connexion injectés par `BehaviorSession` avant affichage ne prouvent pas le
  parcours d'auteur.
