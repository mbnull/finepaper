#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

QString resolveRepositoryPath(const QString& path) {
    const QStringList candidates{
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../") + path),
        QDir::current().absoluteFilePath(path),
        path
    };

    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists()) {
            return info.absoluteFilePath();
        }
    }

    throw std::runtime_error(("missing path: " + path).toStdString());
}

QString repositoryRootPath() {
    const QFileInfo xmake(resolveRepositoryPath(QStringLiteral("qt/xmake.lua")));
    QDir root(xmake.dir());
    root.cdUp();
    return root.absolutePath();
}

bool repositoryRelativePathExists(const QString& path) {
    return QFileInfo(QDir(repositoryRootPath()).absoluteFilePath(path)).exists();
}

QString readText(const QString& path) {
    QFile source(resolveRepositoryPath(path));
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("cannot read " + path).toStdString());
    }
    return QString::fromUtf8(source.readAll());
}

void requireContains(const QString& text,
                     const QString& needle,
                     const QString& context) {
    require(text.contains(needle), context + QStringLiteral(" should contain ") + needle);
}

void requireContainsOneOf(const QString& text,
                          const QStringList& needles,
                          const QString& context) {
    for (const QString& needle : needles) {
        if (text.contains(needle)) {
            return;
        }
    }
    require(false,
            context + QStringLiteral(" should contain one of: ") +
                needles.join(QStringLiteral(", ")));
}

void requireNotContains(const QString& text,
                        const QString& needle,
                        const QString& context) {
    require(!text.contains(needle), context + QStringLiteral(" should not contain ") + needle);
}

void requireNoMatch(const QString& text,
                    const QRegularExpression& pattern,
                    const QString& context) {
    const QRegularExpressionMatch match = pattern.match(text);
    require(!match.hasMatch(),
            context + QStringLiteral(" should not match ") + pattern.pattern());
}

QString whitespaceStripped(const QString& text) {
    QString result = text;
    result.remove(QRegularExpression(QStringLiteral("\\s+")));
    return result;
}

bool isPackageIdToken(const QString& token) {
    return token.contains(QLatin1Char('.')) &&
           !token.contains(QLatin1Char(' ')) &&
           token == token.toLower();
}

QStringList stringLiteralForms(const QString& value) {
    return {
        QStringLiteral("\"%1\"").arg(value),
        QStringLiteral("QStringLiteral(\"%1\")").arg(value),
        QStringLiteral("QString::fromLatin1(\"%1\")").arg(value),
        QStringLiteral("QLatin1String(\"%1\")").arg(value),
        QStringLiteral("QLatin1StringView(\"%1\")").arg(value),
        QStringLiteral("QLatin1StringLiteral(\"%1\")").arg(value),
    };
}

QStringList splitStringLiteralForms(const QString& token) {
    const qsizetype split = token.lastIndexOf(QLatin1Char('.'));
    if (split <= 0 || split == token.size() - 1) {
        return {};
    }

    const QString prefix = token.left(split + 1);
    const QString suffix = token.mid(split + 1);
    QStringList patterns;
    for (const QString& left : stringLiteralForms(prefix)) {
        for (const QString& right : stringLiteralForms(suffix)) {
            patterns.append(left + QLatin1Char('+') + right);
        }
    }
    return patterns;
}

void requirePackageIdNotConstructed(const QString& source,
                                    const QString& compactSource,
                                    const QString& token,
                                    const QString& path) {
    for (const QString& literalForm : stringLiteralForms(token)) {
        requireNotContains(compactSource, literalForm, path);
    }
    for (const QString& splitForm : splitStringLiteralForms(token)) {
        requireNotContains(compactSource, splitForm, path);
    }
    requireNotContains(source, QStringLiteral("'%1'").arg(token), path);
}

void requireFileDoesNotContain(const QString& path, const QStringList& tokens) {
    const QString source = readText(path);
    const QString compactSource = whitespaceStripped(source);
    for (const QString& token : tokens) {
        if (isPackageIdToken(token)) {
            requirePackageIdNotConstructed(source, compactSource, token, path);
        } else {
            requireNotContains(source, token, path);
        }
    }
}

QString structBody(const QString& source, const QString& structName, const QString& context) {
    const int start = source.indexOf(QStringLiteral("struct ") + structName);
    require(start >= 0, context + QStringLiteral(" should define ") + structName);

    const int end = source.indexOf(QStringLiteral("};"), start);
    require(end > start, context + QStringLiteral(" should close ") + structName);

    return source.mid(start, end - start);
}

QString functionBody(const QString& source, const QString& signature, const QString& context) {
    const int signatureStart = source.indexOf(signature);
    require(signatureStart >= 0, context + QStringLiteral(" should define ") + signature);

    const int bodyStart = source.indexOf(QLatin1Char('{'), signatureStart);
    require(bodyStart >= 0, context + QStringLiteral(" should open a function body"));

    int depth = 0;
    for (int index = bodyStart; index < source.size(); ++index) {
        const QChar ch = source.at(index);
        if (ch == QLatin1Char('{')) {
            ++depth;
        } else if (ch == QLatin1Char('}')) {
            --depth;
            if (depth == 0) {
                return source.mid(bodyStart, index - bodyStart + 1);
            }
        }
    }

    require(false, context + QStringLiteral(" should close a function body"));
    return {};
}

QString sectionBetween(const QString& source,
                       const QString& startNeedle,
                       const QString& endNeedle,
                       const QString& context) {
    const int start = source.indexOf(startNeedle);
    require(start >= 0, context + QStringLiteral(" should contain ") + startNeedle);

    const int end = source.indexOf(endNeedle, start + startNeedle.size());
    require(end > start, context + QStringLiteral(" should contain ") + endNeedle);

    return source.mid(start, end - start);
}

QStringList runtimeSourceFiles(const QStringList& roots) {
    QStringList files;
    const QStringList patterns{
        QStringLiteral("*.cpp"),
        QStringLiteral("*.h")
    };

    for (const QString& root : roots) {
        QDirIterator iterator(resolveRepositoryPath(root),
                              patterns,
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString absolutePath = iterator.next();
            files.append(QDir(QDir::currentPath()).relativeFilePath(absolutePath));
        }
    }

    files.sort();
    return files;
}

void testMainWindowHasNoConcreteIpBehaviorTokens() {
    const QStringList concreteIpTokens{
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("finepaper.opennoc"),
        QStringLiteral("finepaper.noc"),
        QStringLiteral("RaveTile"),
        QStringLiteral("OpenNoCXP")
    };

    requireFileDoesNotContain(QStringLiteral("qt/inc/app/mainwindow.h"), concreteIpTokens);
    requireFileDoesNotContain(QStringLiteral("qt/src/app/mainwindow.cpp"), concreteIpTokens);
    requireFileDoesNotContain(QStringLiteral("qt/src/app/mainwindow.ui"), concreteIpTokens);
}

void testPackagePluginDoesNotKnowNoCPluginOrNoCSchema() {
    const QStringList forbiddenTokens{
        QStringLiteral("nocplugin"),
        QStringLiteral("NoCPlugin"),
        QStringLiteral("noc.v1")
    };

    requireFileDoesNotContain(QStringLiteral("qt/inc/package/packageplugin.h"), forbiddenTokens);
    requireFileDoesNotContain(QStringLiteral("qt/src/package/packageplugin.cpp"), forbiddenTokens);
}

void testNoCPluginDoesNotKnowConcreteIpPackagesOrModules() {
    const QStringList concreteIpTokens{
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("finepaper.opennoc"),
        QStringLiteral("finepaper.noc"),
        QStringLiteral("vendor.meshnoc"),
        QStringLiteral("RaveTile"),
        QStringLiteral("OpenNoCXP"),
        QStringLiteral("VendorSwitch")
    };

    requireFileDoesNotContain(QStringLiteral("qt/inc/noc/nocplugin.h"), concreteIpTokens);
    requireFileDoesNotContain(QStringLiteral("qt/src/noc/nocplugin.cpp"), concreteIpTokens);
}

void testProjectGenerationRequestHasNoGraphPointer() {
    const QString header = readText(QStringLiteral("qt/inc/app/projectgenerationrunner.h"));
    const QString request =
        structBody(header, QStringLiteral("ProjectGenerationRequest"), QStringLiteral("ProjectGenerationRequest"));

    const QStringList graphPointerTokens{
        QStringLiteral("const Graph* graph"),
        QStringLiteral("const Graph *graph"),
        QStringLiteral("Graph* graph"),
        QStringLiteral("Graph *graph")
    };
    for (const QString& token : graphPointerTokens) {
        requireNotContains(request, token, QStringLiteral("ProjectGenerationRequest"));
    }
}

void testProjectGenerationRunnerDoesNotReadRequestInstancesSideChannel() {
    const QString header = readText(QStringLiteral("qt/inc/app/projectgenerationrunner.h"));
    const QString request =
        structBody(header, QStringLiteral("ProjectGenerationRequest"), QStringLiteral("ProjectGenerationRequest"));
    requireContains(request,
                    QStringLiteral("ProjectDesign"),
                    QStringLiteral("ProjectGenerationRequest"));
    const QStringList requestSideChannelTokens{
        QStringLiteral("ProjectIpInstanceRecord"),
        QStringLiteral("instances"),
        QStringLiteral("QVector<ProjectIpInstanceRecord>")
    };
    for (const QString& token : requestSideChannelTokens) {
        requireNotContains(request, token, QStringLiteral("ProjectGenerationRequest"));
    }

    const QString runner = readText(QStringLiteral("qt/src/app/projectgenerationrunner.cpp"));
    const QString generate = functionBody(
        runner,
        QStringLiteral("ProjectGenerationResult ProjectGenerationRunner::generate"),
        QStringLiteral("ProjectGenerationRunner::generate"));
    requireNotContains(generate,
                       QStringLiteral("request.instances"),
                       QStringLiteral("ProjectGenerationRunner::generate"));

    const QString mainWindow = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));
    const QString generateVerilog = functionBody(mainWindow,
                                                 QStringLiteral("void MainWindow::generateVerilog"),
                                                 QStringLiteral("MainWindow::generateVerilog"));
    requireNotContains(generateVerilog,
                       QStringLiteral("request.instances"),
                       QStringLiteral("MainWindow::generateVerilog"));
}

void testMainWindowDispatchesTopologyInteractionsWithoutPresetSemantics() {
    const QString mainWindow = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));
    const QString header = readText(QStringLiteral("qt/inc/app/mainwindow.h"));
    const QString createTopologyPreset =
        functionBody(mainWindow,
                     QStringLiteral("void MainWindow::createTopologyPreset"),
                     QStringLiteral("MainWindow::createTopologyPreset"));

    requireContains(createTopologyPreset,
                    QStringLiteral("executeWorkspaceInteractionFor"),
                    QStringLiteral("MainWindow::createTopologyPreset"));
    const QString combined = header + mainWindow;
    const QStringList forbiddenTokens{
        QStringLiteral("TopologyPresetRequest"),
        QStringLiteral("TopologyPresetCommand"),
        QStringLiteral("TopologyPresetParameterDescriptor"),
        QStringLiteral("topology/topologypresetbuilder.h"),
        QStringLiteral("legacy/graphcommands/topologypresetcommand.h"),
        QStringLiteral("QInputDialog::getInt"),
        QStringLiteral("createTopologyPresetFor")
    };
    for (const QString& token : forbiddenTokens) {
        requireNotContains(combined, token, QStringLiteral("MainWindow topology interaction path"));
    }
}

void testProjectValidationRunnerUsesProjectDesignNotGraphOrBasicValidator() {
    const QString header = readText(QStringLiteral("qt/inc/validation/projectvalidationrunner.h"));
    const QString source = readText(QStringLiteral("qt/src/validation/projectvalidationrunner.cpp"));
    const QString combined = header + source;

    requireContains(combined,
                    QStringLiteral("ProjectDesign"),
                    QStringLiteral("ProjectValidationRunner"));
    requireContains(source,
                    QStringLiteral("validateProjectDesign"),
                    QStringLiteral("ProjectValidationRunner"));

    const QStringList forbiddenTokens{
        QStringLiteral("Graph*"),
        QStringLiteral("Graph *"),
        QStringLiteral("const Graph"),
        QStringLiteral("BasicValidator"),
        QStringLiteral("\"graph/graph.h\""),
        QStringLiteral("\"validation/validator.h\"")
    };
    for (const QString& token : forbiddenTokens) {
        requireNotContains(combined, token, QStringLiteral("ProjectValidationRunner"));
    }
}

void testValidationInputApisHaveNoInstanceSideChannel() {
    const QString externalHeader =
        readText(QStringLiteral("qt/inc/validation/projectexternalvalidationrunner.h"));
    const QString externalRequest = structBody(externalHeader,
                                               QStringLiteral("ProjectExternalValidationRequest"),
                                               QStringLiteral("ProjectExternalValidationRequest"));
    requireContains(externalRequest,
                    QStringLiteral("ProjectDesign"),
                    QStringLiteral("ProjectExternalValidationRequest"));
    const QStringList externalRequestSideChannelTokens{
        QStringLiteral("ProjectIpInstanceRecord"),
        QStringLiteral("QVector<ProjectIpInstanceRecord>"),
        QStringLiteral("instances")
    };
    for (const QString& token : externalRequestSideChannelTokens) {
        requireNotContains(externalRequest,
                           token,
                           QStringLiteral("ProjectExternalValidationRequest"));
    }

    const QString validationRunnerHeader =
        readText(QStringLiteral("qt/inc/validation/projectvalidationrunner.h"));
    const QStringList validationRunnerSideChannelTokens{
        QStringLiteral("const QVector<ProjectIpInstanceRecord>& instances"),
        QStringLiteral("QVector<ProjectIpInstanceRecord> instances"),
        QStringLiteral("ProjectIpInstanceRecord>& instances")
    };
    for (const QString& token : validationRunnerSideChannelTokens) {
        requireNotContains(validationRunnerHeader,
                           token,
                           QStringLiteral("ProjectValidationRunner public API"));
    }

    const QString externalSource =
        readText(QStringLiteral("qt/src/validation/projectexternalvalidationrunner.cpp"));
    requireNotContains(externalSource,
                       QStringLiteral("request.instances"),
                       QStringLiteral("ProjectExternalValidationRunner"));

    const QString validationManager =
        readText(QStringLiteral("qt/src/validation/validationmanager.cpp"));
    requireNotContains(validationManager,
                       QStringLiteral("ipInstanceRecords()"),
                       QStringLiteral("ValidationManager"));
    requireNotContains(validationManager,
                       QStringLiteral("validateDetailed(m_projectDesign, entries, instances)"),
                       QStringLiteral("ValidationManager"));
}

void testLegacyGraphDrcRunnerIsNotInProductValidationRuntime() {
    const QString xmake = readText(QStringLiteral("qt/xmake.lua"));
    const QString qtTarget = sectionBetween(xmake,
                                            QStringLiteral("target(\"qt\")"),
                                            QStringLiteral("local function add_qt_test_target"),
                                            QStringLiteral("qt product target"));
    requireNotContains(qtTarget,
                       QStringLiteral("drcrunner"),
                       QStringLiteral("qt product target"));

    const QStringList legacyDrcTokens{
        QStringLiteral("DRCRunner"),
        QStringLiteral("IpCoreGraphExporter"),
        QStringLiteral("IpCoreGraphExportRequest"),
        QStringLiteral("Duplicate XP"),
        QStringLiteral("Duplicate endpoint"),
        QStringLiteral("XP\\\\s+"),
        QStringLiteral("Endpoint\\\\s+"),
        QStringLiteral("XP .+:"),
        QStringLiteral("Endpoint .+:")
    };

    const QStringList roots{
        QStringLiteral("qt/inc/validation"),
        QStringLiteral("qt/src/validation")
    };
    for (const QString& path : runtimeSourceFiles(roots)) {
        requireFileDoesNotContain(path, legacyDrcTokens);
    }
}

void testEditorMutationTargetCommandResultsAreChecked() {
    const QStringList commandFiles{
        QStringLiteral("qt/legacy/graphcommands/addconnectioncommand.cpp"),
        QStringLiteral("qt/legacy/graphcommands/addmodulecommand.cpp"),
        QStringLiteral("qt/legacy/graphcommands/arrangecommand.cpp"),
        QStringLiteral("qt/legacy/graphcommands/removeconnectioncommand.cpp"),
        QStringLiteral("qt/legacy/graphcommands/removemodulecommand.cpp"),
        QStringLiteral("qt/legacy/graphcommands/setconnectionclasscommand.cpp"),
        QStringLiteral("qt/legacy/graphcommands/setparametercommand.cpp"),
        QStringLiteral("qt/legacy/graphcommands/topologypresetcommand.cpp")
    };
    const QStringList ignoredPrefixes{
        QStringLiteral("m_editorMutationTarget->"),
        QStringLiteral("editorMutationTarget->"),
        QStringLiteral("target->")
    };

    for (const QString& path : commandFiles) {
        const QStringList lines = readText(path).split(QLatin1Char('\n'));
        for (int index = 0; index < lines.size(); ++index) {
            const QString trimmed = lines.at(index).trimmed();
            for (const QString& prefix : ignoredPrefixes) {
                require(!trimmed.startsWith(prefix),
                        QStringLiteral("%1:%2 must branch on EditorMutationTarget result")
                            .arg(path)
                            .arg(index + 1));
            }
        }
    }
}

void testRuntimeHasNoConcreteVendorModuleHardcoding() {
    const QStringList forbiddenTokens{
        QStringLiteral("vendor.meshnoc"),
        QStringLiteral("RaveTile"),
        QStringLiteral("OpenNoCXP"),
        QStringLiteral("VendorSwitch")
    };
    const QStringList roots{
        QStringLiteral("qt/inc/app"),
        QStringLiteral("qt/src/app"),
        QStringLiteral("qt/inc/package"),
        QStringLiteral("qt/src/package"),
        QStringLiteral("qt/inc/project"),
        QStringLiteral("qt/src/project"),
        QStringLiteral("qt/inc/validation"),
        QStringLiteral("qt/src/validation")
    };

    for (const QString& path : runtimeSourceFiles(roots)) {
        requireFileDoesNotContain(path, forbiddenTokens);
    }
}

void testCompletionReportUsesHardCutoverVerdict() {
    const QString completion =
        readText(QStringLiteral("docs/architecture/plugin-architecture-completion-report.md"));

    requireNotContains(completion,
                       QStringLiteral("go-with-debt"),
                       QStringLiteral("completion report"));
    requireContainsOneOf(completion,
                         QStringList{
                             QStringLiteral("Final verdict: hard pass"),
                             QStringLiteral("Final verdict: blocked")
                         },
                         QStringLiteral("completion report"));
}

void testFinalReportsAndReadmeRegisterHardCutoverGate() {
    const QString completion =
        readText(QStringLiteral("docs/architecture/plugin-architecture-completion-report.md"));
    const QString hardening =
        readText(QStringLiteral("docs/architecture/plugin-architecture-hardening-report.md"));
    const QString readme = readText(QStringLiteral("docs/architecture/README.md"));

    requireContains(completion,
                    QStringLiteral("plugin_hard_cutover_scan_test"),
                    QStringLiteral("completion report"));
    requireContains(hardening,
                    QStringLiteral("plugin_hard_cutover_scan_test"),
                    QStringLiteral("hardening report"));
    requireContains(readme,
                    QStringLiteral("plugin_hard_cutover_scan_test"),
                    QStringLiteral("architecture README"));
}

void testNormalSaveDoesNotSyncDurableProjectFromGraphProjection() {
    const QString mainWindow = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));
    const QString saveDocument = functionBody(mainWindow,
                                              QStringLiteral("bool MainWindow::saveDocument"),
                                              QStringLiteral("MainWindow::saveDocument"));
    requireNotContains(saveDocument,
                       QStringLiteral("syncProjectFromProjection"),
                       QStringLiteral("MainWindow::saveDocument"));
    requireNotContains(saveDocument,
                       QStringLiteral("GraphProjectSerializer::toProject"),
                       QStringLiteral("MainWindow::saveDocument"));
    requireNotContains(saveDocument,
                       QStringLiteral("replaceDocumentFromProjection"),
                       QStringLiteral("MainWindow::saveDocument"));

    const QString projectionService =
        readText(QStringLiteral("qt/src/project/editorprojectionservice.cpp"));
    const QString projectionSync = functionBody(
        projectionService,
        QStringLiteral("EditorProjectionResult EditorProjectionService::syncProjectFromProjection"),
        QStringLiteral("EditorProjectionService::syncProjectFromProjection"));
    requireContains(projectionSync,
                    QStringLiteral("legacy"),
                    QStringLiteral("EditorProjectionService::syncProjectFromProjection"));
}

void testNodeEditorUiMutationsUseProjectOwnedMutationTarget() {
    const QString mainWindow = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));
    const QString nodeEditor = readText(QStringLiteral("qt/src/nodeeditor/nodeeditorwidget.cpp"));
    const QString propertyPanel = readText(QStringLiteral("qt/src/panels/propertypanel.cpp"));

    requireContains(mainWindow,
                    QStringLiteral("m_projectService.get()"),
                    QStringLiteral("MainWindow UI construction"));
    requireContains(nodeEditor,
                    QStringLiteral("m_editorMutationTarget"),
                    QStringLiteral("NodeEditorWidget mutation path"));
    requireContains(propertyPanel,
                    QStringLiteral("m_editorMutationTarget"),
                    QStringLiteral("PropertyPanel mutation path"));

    requireNotContains(mainWindow,
                       QStringLiteral("NodeEditor module graph commands (AddModuleCommand/TopologyPresetCommand)"),
                       QStringLiteral("MainWindow save blocker"));
    requireNotContains(mainWindow,
                       QStringLiteral("NodeEditor graph parameter/layout commands (SetParameterCommand/ArrangeCommand)"),
                       QStringLiteral("MainWindow save blocker"));
    requireNotContains(mainWindow,
                       QStringLiteral("NodeEditor module graph commands (RemoveModuleCommand)"),
                       QStringLiteral("MainWindow save blocker"));
    requireNotContains(mainWindow,
                       QStringLiteral("NodeEditor connection graph commands (AddConnectionCommand)"),
                       QStringLiteral("MainWindow save blocker"));
    requireNotContains(mainWindow,
                       QStringLiteral("NodeEditor connection graph commands (SetConnectionClassCommand)"),
                       QStringLiteral("MainWindow save blocker"));
    requireNotContains(mainWindow,
                       QStringLiteral("NodeEditor connection graph commands (RemoveConnectionCommand)"),
                       QStringLiteral("MainWindow save blocker"));
}

void testAppContextContainsOnlyRegistries() {
    const QString context = readText(QStringLiteral("qt/inc/app/appcontext.h"));

    const QStringList requiredRegistryPointers{
        QStringLiteral("ServiceRegistry* services"),
        QStringLiteral("ExtensionPointRegistry* extensionPoints"),
        QStringLiteral("CapabilityRegistry* capabilities"),
        QStringLiteral("PluginInteractionRegistry* interactions")
    };
    for (const QString& token : requiredRegistryPointers) {
        requireContains(context, token, QStringLiteral("AppContext registry contract"));
    }

    requireNoMatch(context,
                   QRegularExpression(QStringLiteral(
                       "\\b(WorkbenchService|ProjectService|PackageService|ToolPipelineService)"
                       "\\s*\\*\\s*(workbench|projectService|packageService|toolPipelineService)\\b")),
                   QStringLiteral("AppContext business service pointer contract"));
    requireNoMatch(context,
                   QRegularExpression(QStringLiteral(
                       "\\b(workbench|projectService|packageService|toolPipelineService)"
                       "\\s*=\\s*nullptr\\b")),
                   QStringLiteral("AppContext business service initializer contract"));
}

void testPluginsDoNotFallbackToDirectAppContextServices() {
    const QStringList pluginFiles{
        QStringLiteral("qt/src/project/projectplugin.cpp"),
        QStringLiteral("qt/src/package/packageplugin.cpp"),
        QStringLiteral("qt/src/app/toolpipelineplugin.cpp"),
        QStringLiteral("qt/src/app/pluginhost.cpp"),
        QStringLiteral("qt/src/noc/nocplugin.cpp")
    };
    const QRegularExpression directContextBusinessService(QStringLiteral(
        "\\b(?:context|m_context)\\s*\\.\\s*"
        "(packageService|projectService|toolPipelineService|workbench)\\b"));

    for (const QString& file : pluginFiles) {
        requireNoMatch(readText(file), directContextBusinessService, file);
    }
}

void testFlowRunnerCommandParsingRejectsPermissiveQtJsonConversions() {
    const QString flowRunner = readText(QStringLiteral("qt/src/ipcraft/flowrunner.cpp"));
    const QString flowRunnerTest = readText(QStringLiteral("qt/test/ipcraft_flowrunner_test.cpp"));

    const QStringList forbiddenParsingTokens{
        QStringLiteral("step.value(QStringLiteral(\"command\")).toObject()"),
        QStringLiteral("command.value(QStringLiteral(\"capture\")).toObject()"),
        QStringLiteral("command.value(QStringLiteral(\"env\")).toObject()"),
        QStringLiteral("expandedArguments(command.value(QStringLiteral(\"args\"))"),
        QStringLiteral("sanitizedEnvironment(command)"),
        QStringLiteral("stringArray(env.value(QStringLiteral(\"allow\")))")
    };
    for (const QString& token : forbiddenParsingTokens) {
        requireNotContains(flowRunner, token, QStringLiteral("FlowRunner command parser"));
    }

    requireContains(flowRunnerTest,
                    QStringLiteral("testMalformedCommandFieldsFailClosedBeforeProcessStart"),
                    QStringLiteral("FlowRunner malformed command gate"));
    requireContains(flowRunnerTest,
                    QStringLiteral("flow.command_policy_violation"),
                    QStringLiteral("FlowRunner malformed command gate"));
    requireContains(flowRunnerTest,
                    QStringLiteral("QFileInfo::exists(markerPath)"),
                    QStringLiteral("FlowRunner marker-file fail-closed gate"));
}

void testProjectPatchBoundaryDocumentsMissingDesignEditingOperations() {
    const QString patchHeader = readText(QStringLiteral("qt/inc/ipcraft/core/project_patch.h"));
    const QStringList requiredBacklogTokens{
        QStringLiteral("remove component"),
        QStringLiteral("add/remove connection"),
        QStringLiteral("connection metadata/class/config"),
        QStringLiteral("layout/view state"),
        QStringLiteral("topology preset output")
    };

    for (const QString& token : requiredBacklogTokens) {
        requireContains(patchHeader, token, QStringLiteral("ProjectPatch P1/P2 backlog"));
    }
}

QStringList oldGraphCommandClassNames() {
    return {
        QStringLiteral("AddModuleCommand"),
        QStringLiteral("AddConnectionCommand"),
        QStringLiteral("RemoveModuleCommand"),
        QStringLiteral("RemoveConnectionCommand"),
        QStringLiteral("RemoveIpInstanceCommand"),
        QStringLiteral("SetParameterCommand"),
        QStringLiteral("SetConnectionClassCommand"),
        QStringLiteral("ArrangeCommand"),
        QStringLiteral("TopologyPresetCommand")
    };
}

QStringList oldGraphCommandIncludeNames() {
    return {
        QStringLiteral("addmodulecommand"),
        QStringLiteral("addconnectioncommand"),
        QStringLiteral("removemodulecommand"),
        QStringLiteral("removeconnectioncommand"),
        QStringLiteral("removeipinstancecommand"),
        QStringLiteral("setparametercommand"),
        QStringLiteral("setconnectionclasscommand"),
        QStringLiteral("arrangecommand"),
        QStringLiteral("topologypresetcommand")
    };
}

void requireNoOldGraphCommandConstruction(const QString& file) {
    const QString source = readText(file);
    for (const QString& includeName : oldGraphCommandIncludeNames()) {
        requireNotContains(source,
                           QStringLiteral("commands/%1.h").arg(includeName),
                           file);
        requireNotContains(source,
                           QStringLiteral("legacy/graphcommands/%1.h").arg(includeName),
                           file);
    }
    for (const QString& className : oldGraphCommandClassNames()) {
        requireNoMatch(source,
                       QRegularExpression(QStringLiteral(
                           "std::make_unique\\s*<\\s*%1\\b").arg(className)),
                       file);
        requireNoMatch(source,
                       QRegularExpression(QStringLiteral(
                           "\\bnew\\s+%1\\b").arg(className)),
                       file);
    }
}

void testProductRuntimeDoesNotConstructOldGraphCommands() {
    const QStringList files{
        QStringLiteral("qt/src/nodeeditor/nodeeditorwidget.cpp"),
        QStringLiteral("qt/src/nodeeditor/events/nodeeditorwidget_events.cpp"),
        QStringLiteral("qt/src/panels/propertypanel.cpp"),
        QStringLiteral("qt/src/app/topologypresetinteractionhandler.cpp")
    };
    for (const QString& file : files) {
        requireNoOldGraphCommandConstruction(file);
    }
    const QString topologyHandler =
        readText(QStringLiteral("qt/src/app/topologypresetinteractionhandler.cpp"));
    requireNotContains(topologyHandler,
                       QStringLiteral("TopologyPresetCommand"),
                       QStringLiteral("TopologyPresetInteractionHandler"));
}

void testCurrentDurableCommandDirectoriesAreGraphFree() {
    const QStringList roots{
        QStringLiteral("qt/inc/commands"),
        QStringLiteral("qt/src/commands")
    };
    const QStringList forbiddenTokens{
        QStringLiteral("EditorMutationTarget"),
        QStringLiteral("project/editormutationtarget.h"),
        QStringLiteral("graph/graph.h")
    };
    const QRegularExpression graphPointer(QStringLiteral("\\b(?:const\\s+)?Graph\\s*\\*"));
    const QRegularExpression graphForwardDeclaration(QStringLiteral("\\bclass\\s+Graph\\s*;"));

    for (const QString& file : runtimeSourceFiles(roots)) {
        const QString source = readText(file);
        for (const QString& token : forbiddenTokens) {
            requireNotContains(source, token, file);
        }
        requireNoMatch(source, graphPointer, file);
        requireNoMatch(source, graphForwardDeclaration, file);
    }
}

void testOldGraphCommandSourcesAreLegacyOnly() {
    for (const QString& includeName : oldGraphCommandIncludeNames()) {
        const QString headerPath =
            QStringLiteral("qt/inc/commands/%1.h").arg(includeName);
        const QString sourcePath =
            QStringLiteral("qt/src/commands/%1.cpp").arg(includeName);
        require(!repositoryRelativePathExists(headerPath),
                headerPath + QStringLiteral(" must stay out of the current durable command include directory"));
        require(!repositoryRelativePathExists(sourcePath),
                sourcePath + QStringLiteral(" must stay out of the current durable command source directory"));
    }
}

void testProductTargetDoesNotCompileOldGraphCommandSources() {
    const QString xmake = readText(QStringLiteral("qt/xmake.lua"));
    const QString qtTarget = sectionBetween(xmake,
                                            QStringLiteral("target(\"qt\")"),
                                            QStringLiteral("local function add_qt_test_target"),
                                            QStringLiteral("qt product target"));
    for (const QString& includeName : oldGraphCommandIncludeNames()) {
        requireNotContains(qtTarget,
                           QStringLiteral("src/commands/%1.cpp").arg(includeName),
                           QStringLiteral("qt product target old graph command source exclusion"));
        requireNotContains(qtTarget,
                           QStringLiteral("legacy/graphcommands/%1.cpp").arg(includeName),
                           QStringLiteral("qt product target legacy graph command source exclusion"));
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testMainWindowHasNoConcreteIpBehaviorTokens();
    testPackagePluginDoesNotKnowNoCPluginOrNoCSchema();
    testNoCPluginDoesNotKnowConcreteIpPackagesOrModules();
    testProjectGenerationRequestHasNoGraphPointer();
    testProjectGenerationRunnerDoesNotReadRequestInstancesSideChannel();
    testMainWindowDispatchesTopologyInteractionsWithoutPresetSemantics();
    testProjectValidationRunnerUsesProjectDesignNotGraphOrBasicValidator();
    testValidationInputApisHaveNoInstanceSideChannel();
    testLegacyGraphDrcRunnerIsNotInProductValidationRuntime();
    testEditorMutationTargetCommandResultsAreChecked();
    testRuntimeHasNoConcreteVendorModuleHardcoding();
    testCompletionReportUsesHardCutoverVerdict();
    testFinalReportsAndReadmeRegisterHardCutoverGate();
    testNormalSaveDoesNotSyncDurableProjectFromGraphProjection();
    testNodeEditorUiMutationsUseProjectOwnedMutationTarget();
    testAppContextContainsOnlyRegistries();
    testPluginsDoNotFallbackToDirectAppContextServices();
    testFlowRunnerCommandParsingRejectsPermissiveQtJsonConversions();
    testProjectPatchBoundaryDocumentsMissingDesignEditingOperations();
    testProductRuntimeDoesNotConstructOldGraphCommands();
    testCurrentDurableCommandDirectoriesAreGraphFree();
    testOldGraphCommandSourcesAreLegacyOnly();
    testProductTargetDoesNotCompileOldGraphCommandSources();
    std::cout << "plugin_hard_cutover_scan_test passed\n";
    return 0;
}
