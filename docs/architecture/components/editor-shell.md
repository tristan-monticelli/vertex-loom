# C4 Component — coquille Asset Studio

```mermaid
C4Component
    title Vertex Loom — coquille et authoring Asset Studio
    Container_Boundary(asset, "Asset Studio") {
        Component(shell, "Desktop shell", "SDL2 / OpenGL / Dear ImGui", "Fenêtre, événements, frames et panneaux de l'atelier")
        Component(project_ui, "Creation hub", "Dear ImGui + NFD", "Route Create, Import et Add existing vers des prompts propres à chaque type")
        Component(customizer, "Vector customizer", "Dear ImGui + OpenGL", "Édite forme, fill, contour, clip, hiérarchie et propriétés animables")
    }
    Container_Boundary(editor, "fabric_editor") {
        Component(session, "ProjectSession", "C++20", "Conserve les documents validés et orchestre création, import, commandes et diagnostics")
        Component(prompts, "Typed prompt models", "C++20", "Valide champs, valeurs par défaut et résumé sans dépendre de Dear ImGui")
        Component(history, "CommandStack", "C++20", "Exécute, fusionne, annule et réapplique les modifications réversibles")
        Component(scheduler, "AutosaveScheduler", "C++20", "Déclenche après 2 s d’inactivité ou 30 s au maximum")
    }
    Container(project, "fabric_project", "C++20", "Crée, valide et charge le manifeste partagé")
    System_Ext(dialogs, "Dialogues natifs", "Cocoa, Win32 ou GTK via NFD Extended")
    ContainerDb(files, "Project Files", "JSON + assets", "Dossier projet local")
    Rel(shell, project_ui, "Affiche")
    Rel(shell, customizer, "Affiche")
    Rel(project_ui, session, "Demande la création ou l'ouverture")
    Rel(project_ui, prompts, "Construit le prompt du type choisi")
    Rel(prompts, session, "Publie une intention validée")
    Rel(project_ui, dialogs, "Sélectionne dossiers et fichiers")
    Rel(session, project, "Crée ou charge")
    Rel(session, history, "Porte les mutations éditables")
    Rel(session, scheduler, "Signale les modifications")
    Rel(scheduler, project, "Demande un autosave validé")
    Rel(project, files, "Lit et écrit")
```

## Contract

- Le chemin peut être fourni au démarrage ; les actions interactives utilisent
  le sélecteur natif de dossier ou de fichier.
- La création demande un nom et un dossier de destination absent ou vide.
  L’identifiant interne est généré depuis le nom et rendu unique sans saisie
  utilisateur. Son modèle typé porte unités monde,
  `pixelsPerUnit`, preset d’échelle, erreurs par champ, destination exacte et
  résumé avant publication.
- `Create`, `Import` et `Add existing` sont trois intentions distinctes. Chaque
  type possède son état et sa validation ; annuler ne modifie aucun document.
- Le hub affiche séparément `Create`, `Import` et `Add existing`. Projet,
  artwork et chaque source d’import possèdent un état isolé ; les actions
  matériau, entité, animation et ajout existant restent désactivées jusqu’à
  l’arrivée de leur contrat.
- L’assistant d’artwork valide taille de travail, origine, unités, première
  forme et fill ; il résout seul les conflits d’identifiant. Un fill image référence une texture
  locale et expose cadrage, offset, rotation, échelle, opacité et suivi de la
  déformation. La confirmation publie un `VectorAsset v2 native` atomique.
- Une ouverture échouée expose les erreurs structurées et ne remplace pas la
  dernière session valide.
- SDL2 possède la fenêtre et les événements ; Dear ImGui possède uniquement
  l'interface d'outil ; OpenGL efface et présente la surface.
- Un import réussi conserve le `TextureAsset` et les pixels décodés pour
  l'aperçu ; un échec conserve le dernier import réussi.
- Un import SVG réussi conserve le `VectorAsset` et son aperçu RGBA8 borné ;
  un échec conserve le dernier import vectoriel réussi.
- Asset Studio n’expose aucun import sprite ; PNG alimente les textures et SVG
  les ressources vectorielles liées conformément à ADR-0025.
- Toute mutation d’un document éditable passe par `CommandStack`. Les imports,
  qui créent des ressources immuables sans remplacement, restent hors de cet
  historique de document.
- La session expose undo, redo et dirty et ne marque clean qu’après une
  sauvegarde principale réussie.
- `CommandStack::execute` accepte une commande possédée par la pile ;
  `can_undo`, `can_redo`, `undo`, `redo`, `mark_clean` et `dirty` exposent son
  état sans dépendance à Dear ImGui.
- Une récupération plus récente est proposée à l’ouverture ; accepter charge
  son contenu en mémoire, refuser conserve le principal, sans écriture implicite.
- Le manifeste constitue le premier document éditable intégré : son nom et ses
  unités passent par commandes, Save remplace `project.json`, et son autosave
  sert de preuve headless du flux commun avant les futurs documents d’asset.
