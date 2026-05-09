#pragma once

#include <memory>

#include <QString>

class Graph;
class Module;

namespace NodeEditorEntityFactory {

QString generateEntityId();
std::unique_ptr<Module> createModule(Graph* graph,
                                     const QString& moduleId,
                                     const QString& moduleType,
                                     const QString& ipcoreId);

} // namespace NodeEditorEntityFactory
