# ADR-0045 — State machine d’animation persistée par entité

## Statut

Acceptée

## Décision

Une `EntityDefinition` peut contenir une `animationStateMachine` composée d’un
état initial, d’états référençant des clips d’animation et de transitions
déterministes. Les transitions sont sélectionnées par priorité, conditions
booléennes ou numériques et `exitTime` normalisé.

Les paramètres propres à une instance de map sont fournis par les propriétés
`animationParameter.<id>` et restent limités aux valeurs booléennes et réelles.
Le Preview Runtime résout l’état et le temps local du clip avant d’évaluer les
pistes ; cette résolution est inspectable headless par `state_id`, `clip_id` et
`local_time`. Les animations directes par propriété `animation` restent
supportées. Pour ces animations directes, les markers franchis pendant les
pas fixes sont publiés comme événements runtime avec l’instance, le clip et
les temps local/absolu du marker.

## Conséquences

- Les références de clips font partie du graphe de ressources de l’entité.
- Le `previewEntity` facultatif de chacun de ces clips est un contexte Studio
  souple, pas une dépendance runtime inverse ; une entité peut donc jouer un
  clip créé depuis elle-même sans former de cycle de packaging.
- Le format reste déterministe et ne nécessite aucun script.
- Une entité ne doit pas mélanger une state machine et une intention différente
  portée par la propriété `animation` ; si les deux sont présents, la state
  machine est prioritaire dans le Preview Runtime.
