# ADR-0040 — Registre de descripteurs de propriétés

## Statut

Accepté.

## Décision

Les bindings d’animation restent des identifiants stables, tandis qu’un
`PropertyDescriptorRegistry` décrit les propriétés exposées par les composants.
Un descripteur porte son type, son chemin d’affichage, ses capacités de lecture
et d’écriture, son animabilité, ses bornes et son mode de composition.

Le registre refuse les doublons, les identifiants vides et les bornes inversées.
Il résout un binding par `componentId + propertyId` et expose uniquement les
propriétés animables et inscriptibles pour la future timeline.
