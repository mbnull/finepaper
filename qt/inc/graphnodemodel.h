#pragma once

#include <QtNodes/NodeDelegateModel>
#include "module.h"

class GraphNodeModel : public QtNodes::NodeDelegateModel {
    Q_OBJECT

public:
    GraphNodeModel() = default;

    QString caption() const override;
    QString name() const override { return "GraphNode"; }

    unsigned int nPorts(QtNodes::PortType portType) const override;
    QtNodes::NodeDataType dataType(QtNodes::PortType, QtNodes::PortIndex) const override;
    QString portCaption(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const override;

    std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex) override { return nullptr; }
    void setInData(std::shared_ptr<QtNodes::NodeData>, QtNodes::PortIndex) override {}

    QWidget* embeddedWidget() override { return nullptr; }

    void setModule(Module* module) { m_module = module; }
    Module* module() const { return m_module; }

private:
    Module* m_module = nullptr;
};
