#pragma once

#include <QtNodes/NodeDelegateModel>

class GraphNodeModel : public QtNodes::NodeDelegateModel {
    Q_OBJECT

public:
    GraphNodeModel() = default;

    QString caption() const override { return "Node"; }
    QString name() const override { return "GraphNode"; }

    unsigned int nPorts(QtNodes::PortType) const override { return 2; }
    QtNodes::NodeDataType dataType(QtNodes::PortType, QtNodes::PortIndex) const override;

    std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex) override { return nullptr; }
    void setInData(std::shared_ptr<QtNodes::NodeData>, QtNodes::PortIndex) override {}

    QWidget* embeddedWidget() override { return nullptr; }
};
