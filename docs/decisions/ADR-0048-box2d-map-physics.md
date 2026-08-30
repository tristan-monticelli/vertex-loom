# ADR-0048 — Box2D v3.1.1 pour la physique de Map Studio

## Statut

Accepté.

## Décision

Map Studio utilise Box2D v3.1.1 épinglé comme moteur de dynamique et de
collisions runtime. Le projet expose un wrapper `fabric_physics` qui possède
le monde Box2D, applique un pas fixe et masque les identifiants natifs aux
documents d’authoring.

Les documents map restent la source de vérité et sont validés avant création
du monde. Le Testbed et les outils externes Box2D ne sont pas construits.

Le wrapper instancie les formes map validées sur un corps statique : cercle,
capsule, polygone convexe, segments de chaîne et capteurs. Un échec de forme
interrompt le chargement avant d’exposer le monde.
