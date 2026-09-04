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
contrats distincts. L'attachement persistant à `MapInstance` sera traité dans
une tranche ultérieure avec migration additive et preview/runtime de bout en
bout.

## Conséquences

Le sampler peut être testé sans fenêtre ni renderer et sera réutilisé par Map
Studio, Scene et Preview Runtime. Toute erreur de référence ou de document
reste diagnostiquée par le propriétaire de la session qui consomme le sampler.
