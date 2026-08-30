# ADR-0123 — Actions sémantiques et contextes d’input

## Décision

Les documents d’input restent des registres d’actions sémantiques globaux dans
la version actuelle. Une action est identifiée par son `id` et peut porter
plusieurs bindings clavier ou manette ; les `BehaviorGraph` la consomment via
un nœud `action_source` et sa propriété `semantic_id`.

Les contextes ou profils ne sont pas ajoutés tant qu’un besoin de priorité,
d’activation ou de remapping par écran n’est pas confirmé. Ils devront alors
être introduits comme un changement de schéma versionné, et non comme une
convention implicite dans l’éditeur.

## Conséquences

- Le contrat reste compatible avec le runtime et les fichiers d’input v2.
- Asset Studio affiche les `BehaviorGraph` qui consomment chaque action.
- Une future notion de contexte devra conserver la résolution déterministe des
  actions et documenter ses règles de conflit.
