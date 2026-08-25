# ADR-0076 — Résolution des propriétés effectives d’instances prefab

## Décision

`MapSession::effective_instance_properties` part des overrides du prefab puis
applique les propriétés locales de l’instance par identifiant. Une propriété
locale remplace donc sa valeur héritée, tandis qu’une propriété nouvelle est
ajoutée à la liste effective.

Map Studio affiche cette liste pour l’instance sélectionnée et permet d’écrire
une propriété locale typée via `set_instance_property`. La résolution ne
modifie pas le document ; seule la commande d’écriture le rend dirty.

## Conséquences

L’éditeur rend visible la distinction entre définition de prefab et variation
locale. L’affichage de la provenance exacte de chaque valeur et la propagation
interactive aux instances restent à compléter.
