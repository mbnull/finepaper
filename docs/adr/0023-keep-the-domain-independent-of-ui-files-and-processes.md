# Keep the Domain independent of UI, files, and external processes

Workbench UI calls Application use cases, which coordinate the NoC Domain. Persistence, Provider processes, and IP tools implement ports owned by the Application and are composed at startup; Domain code does not depend on Qt widgets, Graph projections, project-file APIs, QProcess, or Provider implementations even though runtime responses flow back through the Application into the DesignSession.
