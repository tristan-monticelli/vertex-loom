# ADR-0019 — Registre de ressources et validation du graphe

- Status: accepted
- Date: 2026-08-24

## Context

La validation fichier par fichier ne détecte pas deux documents portant le
même identifiant, une référence vers un document absent, une incompatibilité de
type ou une dépendance cyclique. Ces erreurs doivent être refusées avant toute
création de fenêtre et sans dépendre d’un éditeur.

## Decision

Ajouter à `fabric_project` un `ResourceRegistry` sans état global. Chaque entrée
contient un `DocumentHeader`, son chemin projet relatif et ses
`ResourceReference` sortantes. L’enregistrement refuse les chemins non
portables. La validation trie les entrées et diagnostics par identifiant afin
de rester déterministe.

Le registre signale tous les identifiants dupliqués, références absentes,
incompatibilités de type et cycles. La résolution réussit uniquement lorsqu’un
identifiant possède une entrée unique du type attendu.

Le validateur projet charge chaque document de ressource connu, l’ajoute au
registre puis valide le graphe complet après les contrôles propres au document.
Les contrats actuels texture et vecteur n’ont aucune dépendance sortante ; les
prochains contrats fourniront leurs références au même registre.

## Alternatives

Résoudre à la demande dans chaque consommateur répéterait les règles et
laisserait des erreurs atteindre l’interface ou le runtime. Un registre global
mutable compliquerait les tests et les sessions multiples. Accepter les cycles
avec un ordre implicite rendrait le chargement non déterministe.

## Consequences

Le CLI headless devient l’autorité de validation de la totalité des ressources
connues. L’API est prête pour matériaux, entités, animations et maps sans
changer les invariants de résolution.
