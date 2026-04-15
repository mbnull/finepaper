// EditorGraphModel extends the QtNodes graph model with a global edit lock
// used by arranged layout modes.
#pragma once

#include <QtNodes/DataFlowGraphModel>

class EditorGraphModel : public QtNodes::DataFlowGraphModel {
public:
    using QtNodes::DataFlowGraphModel::DataFlowGraphModel;

    QtNodes::NodeFlags nodeFlags(QtNodes::NodeId nodeId) const override {
        QtNodes::NodeFlags flags = QtNodes::DataFlowGraphModel::nodeFlags(nodeId);
        if (m_editingLocked) {
            flags |= QtNodes::NodeFlag::Locked;
        }
        return flags;
    }

    bool connectionPossible(QtNodes::ConnectionId const connectionId) const override {
        if (m_editingLocked) {
            return false;
        }
        return QtNodes::DataFlowGraphModel::connectionPossible(connectionId);
    }

    bool deleteConnection(QtNodes::ConnectionId const connectionId) override {
        if (m_editingLocked) {
            return false;
        }
        return QtNodes::DataFlowGraphModel::deleteConnection(connectionId);
    }

    bool deleteNode(QtNodes::NodeId const nodeId) override {
        if (m_editingLocked) {
            return false;
        }
        return QtNodes::DataFlowGraphModel::deleteNode(nodeId);
    }

    void setEditingLocked(bool locked) {
        if (m_editingLocked == locked) {
            return;
        }

        m_editingLocked = locked;
        for (QtNodes::NodeId nodeId : allNodeIds()) {
            emit nodeFlagsUpdated(nodeId);
        }
    }

    bool isEditingLocked() const {
        return m_editingLocked;
    }

private:
    bool m_editingLocked = false;
};
