# ADR-0067 — Culling runtime par index de chunks

## Décision

Le Preview Runtime réutilise `MapChunkIndex` pour obtenir les instances dont
le point de placement intersecte les bounds monde de la caméra. Les draw
packets sont ensuite récupérés par identifiant d’instance et soumis au culling
géométrique existant.

Le tri des packets reste inchangé. Si l’index ne peut pas être construit, le
runtime conserve le chemin complet comme repli. Les instances restent donc
validées avant rendu et aucun streaming disque n’est introduit.

## Conséquences

Le coût de la recherche des candidats dépend des chunks visibles plutôt que du
nombre total d’instances. La garantie de 60 FPS p95 sur 10 000 éléments doit
encore être mesurée avec une scène de référence réelle.
