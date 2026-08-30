# ADR-0109 — Explorateur de ressources du Studio

- Statut : accepté
- Date : 2026-08-26

## Contexte

Les prompts de matériau et d'entité demandaient de saisir manuellement des
identifiants internes. Le bouton de création restait désactivé tant que le
texte ne correspondait pas exactement à un document existant du bon type,
sans montrer les choix disponibles.

## Décision

Les références entre documents utilisent un explorateur des ressources du
projet. Le contrôle affiche le nom visible, permet une recherche sur le nom ou
l'identifiant, filtre les résultats par type attendu et montre le chemin du
document sélectionné. L'identifiant reste la valeur persistée mais n'est plus
le principal moyen de saisie.

Les références optionnelles disposent d'une action Clear. Changer le type de
drawable efface une sélection devenue incompatible. Un composant visuel masque
le matériau externe puisqu'il possède déjà sa composition de matériaux.

Le sélecteur natif du système reste réservé aux fichiers externes à importer
ou aux dossiers de projet ; les références internes ne quittent pas le graphe
de ressources validé.

## Conséquences

- Une entité avec drawable ne peut sélectionner qu'une ressource compatible.
- Les noms peuvent évoluer sans changer les identifiants persistés.
- Les références manquantes ne sont plus introduites par saisie libre dans les
  prompts concernés.
