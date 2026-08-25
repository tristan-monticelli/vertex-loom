# C4 Context

```mermaid
C4Context
    title Vertex Loom — contexte système
    Person(creator, "Créateur", "Développe le jeu et crée les assets")
    Person(mapmaker, "Créateur de maps", "Compose, teste et publie du contenu depuis les outils intégrés au jeu")
    System(fabric, "Vertex Loom", "Runtime 2D et outils de création textile")
    System_Ext(files, "Système de fichiers local", "Projets, textures, sons et exports")
    System_Ext(catalog, "Catalogue du jeu", "Référence des maps portables validées ; le transport en ligne reste hors périmètre")
    Rel(creator, fabric, "Crée, configure et teste")
    Rel(mapmaker, fabric, "Compose et prévisualise avec le même noyau d'authoring")
    Rel(fabric, files, "Lit et écrit les projets et ressources")
    Rel(fabric, catalog, "Publie et charge des maps validées")
```

## Scope and assumptions

Le système couvre le runtime, Asset Studio et Map Studio. Asset Studio et Map
Studio restent les outils de développement de référence ; leurs contrats et
commandes réutilisables pourront être exposés dans le jeu aux créateurs de
maps. Aucun service distant ni compte utilisateur n'est requis : le catalogue
désigne l'interface de publication et de chargement de maps portables, pas un
backend imposé.
