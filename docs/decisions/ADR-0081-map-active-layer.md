# ADR-0081 — Calque actif et transfert d’instances

- Statut : accepté
- Date : 2026-08-25

## Décision

Map Studio expose un calque actif parmi les `LayerDefinition` existants et
permet d’y transférer une sélection d’instances. `MapSession` valide la cible,
refuse les instances ou calques verrouillés et enregistre le transfert groupé
comme une seule commande.

## Conséquences

- Le calque actif est un état d’interface, pas une donnée supplémentaire dans
  `MapDocument`.
- Le transfert conserve la transformation et l’index de chunk de l’instance.
- Les calques restent l’unité de visibilité, de profondeur et de verrouillage.
