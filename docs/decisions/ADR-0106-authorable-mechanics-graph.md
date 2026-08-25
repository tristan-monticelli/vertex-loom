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

La première mécanique de référence est une plateforme tournante avec pivot,
vitesse, direction, accélération, limites optionnelles, capteur de présence,
activation événementielle, collision et transport du personnage.

## Alternatives

Un prefab codé par mécanique serait rapide mais non composable. Un langage de
script textuel élargirait fortement la surface de sécurité et de validation.
Le graphe typé couvre d'abord les mécaniques courantes et reste sérialisable et
inspectable sans exécuter de code arbitraire.

## Conséquences

- `fabric_physics` reste propriétaire des objets Box2D éphémères.
- Le graphe et la map restent les sources de vérité persistantes.
- Preview Runtime instancie exactement le graphe validé par Map Studio.
- Un prefab peut référencer une mécanique et exposer certains paramètres comme
  overrides typés.
