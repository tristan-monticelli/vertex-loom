# ADR-0024 — Prompts de création typés et opérations explicites

- Status: accepted
- Date: 2026-08-24

## Context

La coquille actuelle juxtapose plusieurs formulaires proches où chemin, nom et
identifiant sont saisis après un sélecteur de fichier. Elle ne distingue pas la
création d’un document éditable, l’import d’une source externe et l’ajout d’une
ressource existante. Cette ambiguïté masque les conséquences de publication et
laisse des réglages importants hors du prompt, notamment les unités du projet.

## Decision

Asset Studio et Map Studio exposent trois familles d’action :

- `Create…` crée un nouveau document éditable ;
- `Import…` copie ou convertit une source externe après aperçu ;
- `Add existing…` référence une ressource déjà enregistrée sans la dupliquer.

Chaque type possède un modèle de prompt distinct, testable sans Dear ImGui.
Projet, artwork vectoriel, fill ou matériau, entité, animation, prefab, map et
scène déclarent leurs champs, valeurs par défaut, validations et résumé de
sortie. Aucun buffer ou état de tentative n’est partagé entre deux types.

Un prompt valide en direct, affiche les erreurs au niveau du champ, le chemin
final et le document produit, puis demande une confirmation de publication.
Annuler ne modifie ni la session, ni l’historique, ni le système de fichiers.
Confirmer produit une commande réversible pour tout document éditable. Les
imports immuables conservent leur publication sans remplacement.

`Create project` demande au minimum destination, nom, identifiant, unités,
pixels par unité et preset explicite, puis affiche un résumé avant création.
`Create vector artwork` demande nom, identifiant, taille de travail, origine,
unités, première forme et fill initial.

## Alternatives

Un formulaire générique piloté par des chaînes réduit le code visible mais
perd les invariants de type et mélange les états. Créer immédiatement après le
sélecteur natif ne permet ni aperçu ni correction. Conserver les réglages dans
un inspecteur après création produit des documents transitoirement incomplets.

## Consequences

`main.cpp` ne doit rendre que les widgets et router les intentions. Les modèles
de prompt, leurs validations et leurs résultats vivent dans `fabric_editor`.
Chaque nouveau type de document doit livrer son prompt et ses tests dans le
même incrément fonctionnel.
