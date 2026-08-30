# ADR-0103 — Tranches studio-first et publication centrée sur la map

## Statut

Accepté — 2026-08-25.

## Contexte

Le moteur possède déjà de nombreux contrats et comportements headless. Une
fonctionnalité uniquement présente dans le runtime ne suffit cependant pas au
produit : Vertex Loom doit servir à développer le jeu, à fabriquer ses assets,
à tester ses mécaniques et, plus tard, à permettre aux créateurs de publier des
maps depuis le même noyau d'authoring.

## Décision

Toute nouvelle fonctionnalité suit une tranche verticale : contrat partagé,
commande réversible, interface dans Asset Studio ou Map Studio, preview
graphique et headless, sauvegarde/rechargement, intégration au Preview Runtime,
puis création d'une fixture réelle avec l'outil.

Le gate du studio propriétaire précède le gate runtime. Une capacité runtime
sans chemin d'authoring reste incomplète.

La map est l'unité de publication du catalogue. L'export rassemble le
`MapDocument`, sa fermeture transitive de ressources et sa version minimale de
runtime dans un paquet portable. Le catalogue et son transport distant ne
deviennent pas un backend obligatoire.

Les futurs créateurs réutilisent les mêmes contrats, validateurs et commandes
que les studios de développement. L'intégration au jeu peut changer la
présentation, mais pas créer un second format d'authoring.

## Alternatives

Livrer d'abord les fonctions runtime accélérerait une démonstration, mais
laisserait les maps dépendre de code inaccessible au studio. Publier chaque
asset séparément compliquerait la compatibilité et la résolution des
dépendances ; la map reste donc la racine du contenu partagé.

## Conséquences

- Les roadmaps sont ordonnées par expérience éditable complète, pas par
  sous-système moteur isolé.
- Chaque fixture de référence doit être produite par les documents que le
  studio sait réellement sauvegarder.
- Preview Runtime charge les mêmes documents que la preview des studios.
- La publication refuse les dépendances manquantes, cycles, chemins absolus et
  versions incompatibles avant de produire un paquet.
