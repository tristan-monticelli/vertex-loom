# C4 Container

<!-- Guide: replace this document after discovery.
Describe only the applications, services, interfaces, data stores, and queues actually selected. -->

```mermaid
C4Container
    title System containers — to be defined
    Person(user, "User", "Primary actor")
    System_Boundary(system, "System to be defined") {
        Container(application, "Application to be defined", "To be selected", "Responsibility to be defined")
    }
    Rel(user, application, "Uses")
```

## Discovery questions

- What are the deployment and runtime boundaries?
- Which flows require a component, sequence, or state diagram?
- Which storage and contracts are required?
