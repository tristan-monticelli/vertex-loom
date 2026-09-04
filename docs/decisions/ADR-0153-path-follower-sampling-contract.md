# ADR-0153 — Contrat d'échantillonnage pour les trajectoires

## Statut

Accepté — 2026-09-04

## Décision

Le guidage d'une instance ou d'une caméra sur un chemin sera fondé sur une
brique `fabric_project` indépendante du rendu texturé. Elle expose une
progression normalisée, une position et une tangente ; la vitesse est exprimée
en unités monde par seconde et peut boucler ou être bornée. L'orientation
optionnelle et son décalage angulaire restent des paramètres du futur composant
Map/Scene, pas de la géométrie visuelle.

La brique accepte les segments ligne et cubique, échantillonne les courbes avec
une résolution bornée et ne modifie jamais le `TexturedPath` source. Le rail de
déplacement, la spline géométrique et la contrainte physique restent donc des
contrats distincts. L'attachement persistant à `MapInstance` est optionnel et
additif : les cartes existantes restent équivalentes.

Le Preview Runtime applique ce contrat à chaque pas fixe, puis accepte une
piste d'animation liant `pathFollower.progress` pour piloter une progression
artistique sans dupliquer la géométrie. Map Studio expose le même composant dans
l'inspecteur, avec sélection de ressource, validation, aperçu du rail et
repositionnement direct du marqueur ; les valeurs sont sauvegardées dans la
carte et incluses dans le paquet publié. La géométrie source ne sera jamais
modifiée.

## Conséquences

Le sampler peut être testé sans fenêtre ni renderer et sera réutilisé par Map
Studio, Scene et Preview Runtime. Toute erreur de référence ou de document
reste diagnostiquée par le propriétaire de la session qui consomme le sampler.
