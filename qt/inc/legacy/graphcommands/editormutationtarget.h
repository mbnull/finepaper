// Legacy graph commands use this test-era mutation target to keep durable
// records in sync while those commands remain isolated from production paths.
#pragma once

#include <QString>

class Connection;
class Module;

class EditorMutationTarget {
public:
    virtual ~EditorMutationTarget() = default;

    virtual bool upsertEditorModuleRecord(const Module& module) = 0;
    virtual bool removeEditorModuleRecord(const QString& moduleId) = 0;
    virtual bool upsertEditorConnectionRecord(const Connection& connection) = 0;
    virtual bool removeEditorConnectionRecord(const QString& connectionId) = 0;
};
