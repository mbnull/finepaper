#pragma once

#include "features/attachment/endpoint_attachment_rules.h"
#include "features/domain/domain_presentation.h"
#include "features/topology/drafts/endpoint_canvas_draft_state.h"
#include "features/topology/topology_workspace_store.h"
#include "noc/model.h"

#include <QtNodes/Definitions>

#include <QHash>
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

namespace QtNodes {
class ConnectionGraphicsObject;
class DataFlowGraphModel;
class DataFlowGraphicsScene;
class NodeDelegateModelRegistry;
struct ConnectionId;
using NodeId = unsigned int;
}

class QEvent;
class QGraphicsItem;
class QGraphicsPathItem;
class QLabel;

namespace finepaper {

class AnimatedGraphicsView;
enum class EndpointDragTarget;
enum class NocCanvasCommand;

enum class NocCanvasInteractionMode {
    Select,
    Pan
};

enum class TopologyWorkspaceDiagnosticKind : quint8 {
    LoadFailed,
    SaveFailed,
    SaveRecovered,
    LegacyImportSkipped,
    RepairSucceeded,
};

enum class TopologyWorkspaceRegularizeResult : quint8 {
    Saved,
    Repaired,
    SaveFailed,
};

struct TopologyWorkspaceDiagnostic {
    TopologyWorkspaceDiagnosticKind kind =
        TopologyWorkspaceDiagnosticKind::LoadFailed;
    QString designId;
    QString details;
};

struct NocEditorSelection {
    enum class Kind {
        None,
        Router,
        Endpoint,
        RouterLink,
        EndpointAttachment,
        PendingEndpoint,
    };

    Kind kind = Kind::None;
    QString id;
    std::optional<RouterPosition> router = std::nullopt;
    EndpointCanvasDraftId canvasDraftId;

    [[nodiscard]] std::optional<ElementRef> element() const {
        switch (kind) {
        case Kind::Router:
            return ElementRef{ElementKind::Router, id};
        case Kind::Endpoint:
            return ElementRef{ElementKind::Endpoint, id};
        case Kind::RouterLink:
            return ElementRef{ElementKind::RouterLink, id};
        case Kind::EndpointAttachment:
            return ElementRef{ElementKind::EndpointAttachment, id};
        case Kind::None:
        case Kind::PendingEndpoint:
            return std::nullopt;
        }
        return std::nullopt;
    }
};

struct NocEditorSelectionSet {
    QVector<NocEditorSelection> items;

    bool empty() const { return items.isEmpty(); }
    qsizetype size() const { return items.size(); }

    [[nodiscard]] QVector<ElementRef> elements() const {
        QVector<ElementRef> references;
        references.reserve(items.size());
        for (const NocEditorSelection& item : items) {
            if (const std::optional<ElementRef> reference = item.element()) {
                references.append(*reference);
            }
        }
        return references;
    }
};

struct NocEndpointTypeItem {
    QString id;
    QString label;

    bool operator==(const NocEndpointTypeItem&) const = default;
};

using NocAttachmentTarget = attachment::AttachmentTarget;
using NocDetachedEndpointSnapshot = attachment::DetachedEndpointSnapshot;

class NocNodeEditor final : public QWidget {
public:
    explicit NocNodeEditor(QWidget* parent = nullptr);
    ~NocNodeEditor() override;

    void setDesign(const NocDesign* design);
    void setDesign(const NocDesign* design, attachment::Policy policy);
    // Separates transient canvas state from persistent workspace identity.
    // MainWindow calls this once for each newly created or opened document.
    void beginDocumentSession(QString sessionToken);
    // Updates non-projected semantic state (for example Domain assignments or
    // sparse element configurations) without rebuilding the graph or losing
    // the current selection and Workspace layout.
    void syncDesignState(const NocDesign& design);
    void setEndpointTypes(QVector<NocEndpointTypeItem> endpointTypes);
    void setAttachmentPolicy(attachment::Policy policy);
    void setEditingEnabled(bool enabled);
    bool editingEnabled() const;
    void setCanvasInteractionMode(NocCanvasInteractionMode mode);
    NocCanvasInteractionMode canvasInteractionMode() const {
        return m_canvasInteractionMode;
    }
    void setReducedMotion(bool reduced);
    bool reducedMotion() const;
    [[nodiscard]] attachment::SlotResolution endpointAttachmentAvailability(
        RouterPosition router) const;
    void selectElements(const QVector<ElementRef>& elements);
    bool setRouterVisualPosition(const QString& routerId, QPointF position);
    std::optional<QPointF> routerVisualPosition(const QString& routerId) const;
    std::optional<QPointF> endpointVisualPosition(const QString& endpointId) const;
    bool setRouterCollapsed(const QString& routerId, bool collapsed);
    bool routerCollapsed(const QString& routerId) const;
    [[nodiscard]] TopologyWorkspaceRegularizeResult regularizeLayout();
    void zoomToFit();
    void focusCanvas(Qt::FocusReason reason = Qt::ShortcutFocusReason);
    void setDomainPresentation(DomainPresentationSnapshot presentation);
    [[nodiscard]] const DomainPresentationSnapshot& domainPresentation() const;
    [[nodiscard]] const EndpointCanvasDraftState&
    endpointCanvasDraftState() const;

    // Mutation callbacks are synchronous and run on this widget's GUI thread.
    // They may synchronously replace the design and rebuild the graph; callers
    // therefore copy semantic values before invocation and never retain a
    // NodeId, iterator, or graphics pointer across the callback boundary.
    std::function<attachment::CreateEndpointResult(
        const QString&, NocAttachmentTarget)> endpointTypeDropped;
    std::function<bool(const QString&, NocAttachmentTarget)> endpointMoveRequested;
    std::function<bool(const NocDetachedEndpointSnapshot&, NocAttachmentTarget)>
        detachedEndpointDropped;
    // Disconnect temporarily removes the durable attachment while retaining
    // a recoverable canvas draft. Permanent deletion is a distinct lifecycle
    // event so Inspector parameter drafts are not lost on disconnect.
    std::function<bool(const QString&)> endpointRemovalRequested;
    std::function<bool(const QString&)> endpointDeletionRequested;
    std::function<void(const QString&)> detachedEndpointDeletionRequested;
    std::function<void()> endpointCanvasDraftStateChanged;
    std::function<void(const NocEditorSelection&)> selectionChanged;
    std::function<void(const NocEditorSelectionSet&)> semanticSelectionChanged;
    std::function<void(const TopologyWorkspaceDiagnostic&)>
        workspaceDiagnosticRaised;
    std::function<void(attachment::Rejection, std::optional<RouterPosition>)>
        attachmentRejected;
    std::function<void(NocCanvasCommand)> canvasCommandUnavailable;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    struct NodeMetadata {
        NocEditorSelection::Kind kind = NocEditorSelection::Kind::None;
        QString id;
        std::optional<RouterPosition> router = std::nullopt;
        QPointF projectedPosition;
        QString endpointType;
    };

    struct SelectionIdentity {
        NocEditorSelection::Kind kind = NocEditorSelection::Kind::None;
        QString id;

        bool operator==(const SelectionIdentity&) const = default;
    };

    struct PendingEndpoint {
        QString id;
        QString type;
        QPointF scenePosition;
        std::optional<NocDetachedEndpointSnapshot> detached = std::nullopt;
    };

    struct RouterEndpointDraft {
        QtNodes::NodeId routerNode = 0;
        RouterPosition router;
        unsigned int portIndex = 0;
        QPointF startScenePosition;
        QGraphicsPathItem* graphicsItem = nullptr;
    };

    struct EndpointAttachmentDraft {
        QtNodes::NodeId endpointNode = 0;
        QPointF startScenePosition;
        QGraphicsPathItem* graphicsItem = nullptr;
    };

    void rebuildGraph(bool zoomToContents = true);
    void discardTransientProjectionHistory();
    void applyDesign(const NocDesign* design, attachment::Policy policy);
    void beginSemanticMutation();
    void endSemanticMutation(bool refreshProjection);
    void loadWorkspaceState();
    bool saveWorkspaceState();
    void deferForCurrentGraph(std::function<void()> operation);
    void reportWorkspaceDiagnostic(
        TopologyWorkspaceDiagnosticKind kind,
        const QString& details = {});
    void handleSceneSelectionChanged();
    void handlePointerReleased(const QPoint& viewportPosition);
    void handleConnectionCreated(QtNodes::ConnectionId connectionId);
    void handleConnectionDeleted(QtNodes::ConnectionId connectionId);
    bool isEndpointAttachmentConnection(QtNodes::ConnectionId connectionId) const;
    bool tryCompleteDraftConnection(const QPoint& viewportPosition);
    QtNodes::ConnectionGraphicsObject* findDraftConnection() const;
    bool beginRouterEndpointDraft(const QPoint& viewportPosition);
    void updateRouterEndpointDraft(const QPoint& viewportPosition);
    bool completeRouterEndpointDraft(const QPoint& viewportPosition);
    void clearRouterEndpointDraft();
    bool beginEndpointAttachmentDraft(const QPoint& viewportPosition);
    void updateEndpointAttachmentDraft(const QPoint& viewportPosition);
    bool completeEndpointAttachmentDraft(const QPoint& viewportPosition);
    void clearEndpointAttachmentDraft();
    bool handleEndpointDrop(const QString& endpointType, const QPoint& viewportPosition);
    void addPendingEndpoint(const QString& endpointType, QPointF scenePosition);
    void storeEndpointCanvasDraft(PendingEndpoint draft);
    bool removeEndpointCanvasDraft(const QString& draftId);
    void clearEndpointCanvasDrafts();
    void rebuildEndpointCanvasDraftState();
    void notifyEndpointCanvasDraftStateChanged();
    void updateEndpointCanvasDraftNotice();
    bool attachNodeToRouter(QtNodes::NodeId nodeId, NocAttachmentTarget target);
    bool detachEndpoint(QtNodes::NodeId nodeId,
                        bool restoreProjectionOnFailure = false);
    bool deleteEndpointNode(QtNodes::NodeId nodeId);
    bool deleteSelectedSemanticItem();
    bool selectAdjacentCanvasItem(bool backwards);
    bool showSelectedContextMenuFromKeyboard();
    std::optional<QtNodes::NodeId> nodeIdForSelection(
        const SelectionIdentity& selection) const;
    QGraphicsItem* graphicsItemForSelection(
        const SelectionIdentity& selection);
    void ensureSelectionVisible(const SelectionIdentity& selection);
    void showContextMenu(const QPoint& viewportPosition, const QPoint& globalPosition);
    void showConnectionContextMenu(QtNodes::ConnectionId connectionId,
                                   const QPoint& globalPosition);
    void showCanvasCreateMenu(QPointF scenePosition, const QPoint& globalPosition);
    void showNodeContextMenu(QtNodes::NodeId nodeId, const QPoint& globalPosition);
    std::optional<QtNodes::NodeId> routerNodeAt(
        const QPointF& scenePosition) const;
    std::optional<QtNodes::NodeId> nodeAt(const QPoint& viewportPosition) const;
    std::optional<QtNodes::NodeId> nodeAtScene(
        const QPointF& scenePosition,
        std::optional<QtNodes::NodeId> ignoredNode = std::nullopt) const;
    QtNodes::ConnectionGraphicsObject* connectionAt(
        const QPoint& viewportPosition) const;
    bool blockedPortAt(const QPoint& viewportPosition) const;
    bool isRouterAttachmentPort(unsigned int portIndex) const;
    attachment::ConnectionHandleKind outputConnectionHandle(
        QtNodes::NodeId nodeId,
        unsigned int portIndex) const;
    attachment::ConnectionHandleKind inputConnectionHandle(
        QtNodes::NodeId nodeId,
        unsigned int portIndex) const;
    attachment::TargetResolution resolveAttachmentTarget(
        QtNodes::NodeId routerNode,
        attachment::RouterHitKind hitKind,
        std::optional<unsigned int> portIndex = std::nullopt,
        std::optional<QtNodes::NodeId> ignoredEndpoint = std::nullopt) const;
    attachment::TargetResolution resolveAttachmentTargetAt(
        QtNodes::NodeId routerNode,
        const QPointF& scenePosition,
        std::optional<QtNodes::NodeId> ignoredEndpoint = std::nullopt) const;
    EndpointDragTarget endpointDragTargetAt(
        const QPoint& viewportPosition) const;
    void reportAttachmentRejection(
        attachment::Rejection rejection,
        std::optional<RouterPosition> router = std::nullopt) const;
    bool attachmentPortAvailable(QtNodes::NodeId routerNode,
                                 unsigned int portIndex,
                                 std::optional<QtNodes::NodeId> ignoredEndpoint = std::nullopt) const;
    QString pendingEndpointCaption(const PendingEndpoint& pending) const;
    QString pendingEndpointToolTip(const PendingEndpoint& pending) const;
    void refreshPendingEndpointPresentation();
    QString endpointTypeLabel(const QString& endpointType) const;
    std::optional<ElementRef> elementForConnection(
        QtNodes::ConnectionId connectionId) const;
    std::optional<NocEditorSelection> selectionForIdentity(
        const SelectionIdentity& identity) const;
    void emitSelectionChanged();
    void applyNodeStacking();
    void applyDomainPresentation();
    void updateCanvasAccessibleDescription();
    void restoreSelection();
    void highlightNeighborhood(QtNodes::NodeId nodeId);
    void setNodeRelatedHighlight(QtNodes::NodeId nodeId);
    void setConnectionRelatedHighlight(QtNodes::ConnectionId connectionId);
    void clearNeighborhoodHighlight();

    std::optional<NocDesign> m_design = std::nullopt;
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    std::unique_ptr<QtNodes::DataFlowGraphModel> m_graphModel;
    QtNodes::DataFlowGraphicsScene* m_scene = nullptr;
    AnimatedGraphicsView* m_view = nullptr;
    QLabel* m_endpointCanvasDraftNotice = nullptr;
    EndpointCanvasDraftState m_endpointCanvasDraftState;
    QHash<QtNodes::NodeId, NodeMetadata> m_metadata;
    QHash<QString, QtNodes::NodeId> m_pendingEndpointNodes;
    QHash<QString, QtNodes::NodeId> m_routerNodes;
    QHash<ElementRef, QtNodes::NodeId> m_elementNodes;
    QHash<ElementRef, QtNodes::ConnectionId> m_elementConnections;
    QSet<QtNodes::NodeId> m_stackingNodeIds;
    QSet<QtNodes::NodeId> m_highlightedNodeIds;
    QVector<QtNodes::ConnectionId> m_highlightedConnectionIds;
    TopologyWorkspaceState m_workspaceState;
    TopologyWorkspaceStore m_workspaceStore;
    QVector<NocEndpointTypeItem> m_endpointTypes;
    attachment::Policy m_attachmentPolicy =
        attachment::policyFromPackage(AttachmentDefinition{});
    attachment::DesignIndex m_attachmentIndex;
    QHash<QString, PendingEndpoint> m_pendingEndpoints;
    std::optional<RouterEndpointDraft> m_routerEndpointDraft = std::nullopt;
    std::optional<EndpointAttachmentDraft> m_endpointAttachmentDraft = std::nullopt;
    QSet<QString> m_pendingConnectionDetachments;
    int m_nextPendingEndpoint = 0;
    QVector<SelectionIdentity> m_selectedItems;
    DomainPresentationSnapshot m_domainPresentation;
    bool m_editingEnabled = true;
    bool m_workspacePersistenceBlocked = false;
    bool m_workspaceReloadPending = false;
    bool m_canvasSelectionGesture = false;
    bool m_canvasItemGesture = false;
    int m_semanticMutationDepth = 0;
    bool m_projectionRefreshDeferred = false;
    bool m_projectionZoomDeferred = false;
    NocCanvasInteractionMode m_canvasInteractionMode =
        NocCanvasInteractionMode::Pan;
    std::optional<TopologyWorkspaceIdentity> m_workspaceIdentity = std::nullopt;
    QString m_documentSessionToken;
    QString m_lastWorkspaceDiagnostic;
    std::optional<TopologyWorkspaceDiagnosticKind>
        m_lastWorkspaceDiagnosticKind = std::nullopt;
    quint64 m_graphRevision = 0;
};

} // namespace finepaper
