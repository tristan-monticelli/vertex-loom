# ADR-0124 — Remplacement typé des références

## Décision

Le Resource Explorer propose le remplacement d’une ressource référencée par
une autre ressource du même type. L’opération ne modifie que les objets JSON
portant simultanément l’identifiant ciblé et le champ `expectedType` attendu ;
les identifiants de documents, de nœuds et de propriétés restent inchangés.

Les documents entrants sont tous préparés et validés avant la première
écriture. Les écritures utilisent la publication atomique existante ; si une
écriture ultérieure échoue, les documents déjà remplacés sont restaurés avec
leur contenu validé d’origine. La suppression reste bloquée tant que les
références n’ont pas été remplacées ou que l’utilisateur n’a pas annulé.

## Conséquences

- Le remplacement est disponible pour les ressources indexées par le rail
  commun et conserve les types de référence.
- Une cascade automatique n’est pas implicite ; elle devra faire l’objet d’une
  confirmation et d’un plan de dépendances séparé.
- L’opération efface l’historique de commande de la session après publication,
  afin d’éviter un historique qui ne représenterait plus les fichiers chargés.
