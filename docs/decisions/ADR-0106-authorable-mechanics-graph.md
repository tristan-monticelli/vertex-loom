# ADR-0106 — Graphe de mécaniques physiques éditable

## Statut

Accepté — 2026-08-25.

## Contexte

Les collisions statiques de `MapDocument v1` et le wrapper Box2D ne suffisent
pas à construire dans Map Studio une mécanique réutilisable telle qu'une
plateforme tournante activée par un personnage. Ajouter ces cas directement au
runtime rendrait le jeu plus capable que son outil de création.

## Décision

`MechanicGraph v1` compose des nœuds corps, pivot, joint, moteur, capteur,
contrainte et événement. Les ports et propriétés sont typés, les identifiants
stables et l'ordre d'évaluation déterministe. Les références invalides, cycles
de contrôle interdits et configurations physiques non finies sont refusés avant
la création du monde Box2D.

Une mécanique expose des paramètres d'instance sans code propre à la map. Map
Studio construit, inspecte, démarre, met en pause et réinitialise sa simulation.
Chaque modification passe par `CommandStack` et reconstruit la preview depuis
le document validé.

L'édition du graphe utilise une session de document dédiée : elle ne rend pas
la map dirty et ne mélange pas leurs historiques. La simulation conserve le
plan compilé mais aucun handle Box2D dans les fichiers ; chaque ouverture,
mutation ou reset reconstruit ces handles. Le pas manuel est fixé à `1/60 s` et
le mode lecture accumule uniquement du temps de frame borné.

Le parcours nominal de Map Studio est un canevas de cartes, ports et flèches.
Une commande part d'un port de sortie ; cliquer une carte cible choisit son
premier port d'entrée de même type et soumet la connexion à
`MechanicSession`. Les listes de nœuds, propriétés et extrémités restent des
inspecteurs avancés. Sélection, layout et connexion en cours sont éphémères :
`MechanicGraph v1` reste inchangé.

Le workspace Mechanics expose aussi un canevas spatial dans les mêmes unités
monde que la map. Les corps et capteurs y sont dessinés par leur rectangle ;
les pivots et joints reliés par leur poignée. Un glisser déplace directement
la propriété `position` du corps/pivot ou `center` du capteur au travers de
`MechanicSession`, en une mutation validée et undoable. La sélection du canevas
spatial et celle du graphe partagent l'identifiant stable du nœud. Cet état de
vue reste éphémère et n'ajoute aucun champ à `MechanicGraph v1`.

La première mécanique de référence est une plateforme tournante avec pivot,
vitesse, direction, accélération, limites optionnelles, capteur de présence,
activation événementielle, collision et transport du personnage.

La preview matérialise chaque nœud capteur comme une forme Box2D sensorielle.
Un personnage de test dynamique peut être créé et déplacé depuis Map Studio ;
il reste un état éphémère de preview et n'est jamais sérialisé dans le graphe.
Les contacts et la friction Box2D assurent son transport par la plateforme,
sans parentage visuel ni correction de position propre au preset.

Chaque moteur expose un état d'activation générique associé à sa source. Le
passage inactif vers actif produit `begin`, le passage inverse produit `end`,
et l'état courant reste consultable. La preview conserve un journal borné et
ordonné par pas fixe pour les overlays ; reset reconstruit le monde, le
personnage et le journal depuis leur configuration initiale.

Le nœud événement distingue deux modes rétrocompatibles : `emit` consomme un
signal booléen et publie l'événement map ; `listen` produit un signal booléen
piloté par cet événement. Le port de sortie et la propriété de mode restent
optionnels dans le schéma v1 afin que les graphes v1 déjà publiés conservent
leur sens `emit`. Le moteur accepte aussi les propriétés optionnelles
`direction` (`-1` ou `1`) et `acceleration`; leur absence conserve la vitesse
signée instantanée de la première version.

Map Studio fournit une factory `RotatingPlatform` qui assemble uniquement les
sept nœuds génériques. Elle expose taille, vitesse, direction,
accélération, couple, zone capteur et limites comme paramètres liés ; le mode
d'activation choisit un capteur ou un écouteur d'événement, sans type Box2D ou
code gameplay propre au preset.
Le corps peut référencer une entité visuelle créée dans Asset Studio ; cette
référence reste distincte de sa forme de collision et sera composée par la map
sans convertir l'image ou le composant visuel en géométrie physique.

Preview Runtime crée une `MechanicSimulation` indépendante pour chaque
instance de prefab mécanique. Il charge le graphe publié, applique les
overrides du prefab et le transform uniforme de l'instance avec le même
compilateur que Map Studio, puis avance la simulation au pas fixe `1/60 s`.
Un échec de chargement, compilation ou création Box2D refuse la map avant la
création de la fenêtre.

Dans le contrat v1, un corps dont `visual_entity` référence l'entité du prefab
pilote les paquets visuels de cette instance. Le runtime applique aux paquets
la translation et la rotation relatives entre la pose physique initiale et la
pose courante du corps. Une entité sans corps visuel correspondant conserve le
transform statique de la map ; plusieurs corps correspondants sont refusés car
une instance v1 ne possède qu'une racine visuelle.

## Alternatives

Un prefab codé par mécanique serait rapide mais non composable. Un langage de
script textuel élargirait fortement la surface de sécurité et de validation.
Le graphe typé couvre d'abord les mécaniques courantes et reste sérialisable et
inspectable sans exécuter de code arbitraire.

## Conséquences

- `fabric_physics` reste propriétaire des objets Box2D éphémères.
- Les formes de capteur, le personnage de test et les transitions de debug ne
  modifient aucun document persistant.
- Le graphe et la map restent les sources de vérité persistantes.
- Preview Runtime instancie exactement le graphe validé par Map Studio.
- Les simulations mécaniques d'instances distinctes ne partagent aucun handle
  Box2D et leurs pas exécutés sont observables dans les métriques runtime.
- Un prefab peut référencer une mécanique et exposer certains paramètres comme
  overrides typés.
- La validation visuelle doit partir de nœuds non reliés, connecter deux ports
  par gestes UI, sauvegarder/recharger puis démarrer la simulation compilée.
