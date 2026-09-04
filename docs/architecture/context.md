# C4 Context

```mermaid
C4Context
    title Vertex Loom — contexte système
    Person(creator, "Créateur", "Développe le jeu et crée les assets")
    Person(mapmaker, "Créateur de maps", "Compose, teste et publie du contenu depuis les outils intégrés au jeu")
    System(fabric, "Vertex Loom", "Runtime 2D et outils de création textile, profils shader et marqueurs de collision")
    System_Ext(files, "Système de fichiers local", "Projets, textures, sons et exports")
    System_Ext(catalog, "Catalogue du jeu", "Référence des maps portables validées ; le transport en ligne reste hors périmètre")
    Rel(creator, fabric, "Crée assets, comportements et transformations, puis teste")
    Rel(mapmaker, fabric, "Compose et prévisualise avec le même noyau d'authoring")
    Rel(fabric, files, "Lit et écrit les projets et ressources")
Rel(fabric, catalog, "Publie et charge des maps validées")
```

## Parcours utilisateur de référence

Le premier écran doit répondre à une seule question : « que puis-je faire
maintenant et sur quelle ressource ? ». Le créateur ouvre ou crée un projet,
parcourt une ressource dans le rail Project à gauche, l'édite dans l'Inspector
à droite, la
prévisualise immédiatement, puis compose une map avant validation et
publication. Toute transition conserve le document actif jusqu'à `Save`,
`Discard` ou `Cancel`.

```mermaid
flowchart LR
    Start[Ouvrir ou créer un projet] --> Explore[Explorer les ressources]
    Explore --> Create{Créer ou ouvrir}
    Create --> Asset[Éditer asset / entité / comportement]
    Asset --> Preview[Prévisualiser et corriger]
    Preview --> Map[Composer map / scène]
    Map --> Validate[Valider les références et paramètres]
    Validate -->|erreurs| Asset
    Validate --> Publish[Publier un paquet]
    Publish --> Runtime[Tester dans Preview Runtime]
    Runtime --> Iterate[Revenir à la ressource concernée]
    Iterate --> Explore
```

Les paramètres suivent le même cycle dans chaque surface : `Create` expose les
valeurs initiales, `Inspector` expose toutes les valeurs persistées, `Preview`
montre leur effet, et `Validate` explique l'erreur au champ concerné. Un
paramètre disponible à la création ne peut donc pas devenir immuable après
publication.

## Scope and assumptions

Le système couvre le runtime, Asset Studio et Map Studio. Asset Studio et Map
Studio restent les outils de développement de référence ; leurs contrats et
commandes réutilisables pourront être exposés dans le jeu aux créateurs de
maps. Aucun service distant ni compte utilisateur n'est requis : le catalogue
désigne l'interface de publication et de chargement de maps portables, pas un
backend imposé.

La logique d'une entité est authorée comme un `BehaviorGraph` générique. Les
bindings physiques produisent des actions sémantiques, tandis que le même graphe
peut aussi recevoir une décision IA, un événement de map, un trigger, un timer
ou une propriété. Une transformation est une action explicite du comportement
qui remplace une instance selon une ressource de politique versionnée ; aucun de
ces contrats ne distingue joueur et monstre.
