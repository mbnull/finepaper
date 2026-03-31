// GraphNodeModel adapts Module to QtNodes framework for visual node editor
#pragma once

#include <QtNodes/NodeDelegateModel>
#include "module.h"

class GraphNodeModel : public QtNodes::NodeDelegateModel {
    Q_OBJECT

public:
    GraphNodeModel() = default;

    QString caption() const override;
    QString name() const override { return "GraphNode"; }
    bool portCaptionVisible(QtNodes::PortType, QtNodes::PortIndex) const override { return false; }

    unsigned int nPorts(QtNodes::PortType portType) const override;
    QtNodes::NodeDataType dataType(QtNodes::PortType, QtNodes::PortIndex) const override;
    QString portCaption(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const override;
    QtNodes::ConnectionPolicy portConnectionPolicy(QtNodes::PortType, QtNodes::PortIndex) const override;

    std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex) override { return nullptr; }
    void setInData(std::shared_ptr<QtNodes::NodeData>, QtNodes::PortIndex) override {}

    QWidget* embeddedWidget() override { return nullptr; }

    void setModule(Module* module);
    Module* module() const { return m_module; }
    bool isCollapsed() const;
    const Port* portAt(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const;
    QtNodes::PortIndex portIndex(const QString& portId, QtNodes::PortType portType) const;

private:
    void applyTypeStyle();

    Module* m_module = nullptr;
};
