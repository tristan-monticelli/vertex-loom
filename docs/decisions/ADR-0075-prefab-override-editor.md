# ADR-0075 — Formulaire typé d’overrides de prefab

## Décision

Map Studio propose un formulaire d’override associé au prefab sélectionné. Les
types disponibles correspondent exactement à `MapPropertyValue` : booléen,
entier, réel, texte, `Vec2` écrit `x,y` et référence de ressource. Le parsing
échoue explicitement sur les valeurs mal formées, puis la mutation passe par
`MapSession::set_prefab_override`.

## Conséquences

Les overrides peuvent être créés depuis l’interface sans modifier la définition
d’entité et restent undoables. La validation métier de types de propriétés et
la visualisation des overrides hérités par instance restent à compléter.
