# Composants — BehaviorGraph

```mermaid
flowchart LR
    UI[Asset Studio\néditeur de graphe] --> Session[BehaviorSession\nCommandStack + autosave]
    Session --> Contract[BehaviorGraph v1\nparseur + validation]
    Contract --> Registry[Resource registry\npackage closure]
    Input[InputDocument\nactions physiques] --> Eval[BehaviorEvaluator\nétat par instance]
    Input --> Mapping[Character action mapping\nidentifiants explicites]
    Mapping --> Frame[CharacterControlFrame\naxe + impulsion normalisés]
    Frame --> Controller[CharacterController\nsans nom d'action]
    AI[Décisions IA] --> Eval
    Events[Événements / triggers / timers] --> Eval
    Props[Propriétés d'instance] --> Eval
    Contract --> Eval
    Eval --> Actions[Actions typées\npropriété, événement, animation,\nmouvement, mécanique, transformation]
    Actions --> Runtime[Preview Runtime]
    Actions --> Journal[Journal borné de preview]
```

Le contrat ne contient aucun handle runtime. Les identifiants de nœuds, ports,
paramètres et connexions sont stables dans le document. Les sources convergent
vers le même type `signal`; les valeurs de données gardent un type explicite.
Les cycles de flux sont refusés. Les nœuds `state`, `transition`, `delay` et
`cooldown` portent leur mémoire dans l'évaluateur d'instance.

Le contrôleur physique CLI optionnel ne déroge pas à ce découplage :
`PreviewRuntimeOptions.character_actions` choisit trois actions du document
Input, puis le contrôleur ne reçoit que leurs valeurs normalisées.
