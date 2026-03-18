#include "graphnodemodel.h"

QtNodes::NodeDataType GraphNodeModel::dataType(QtNodes::PortType, QtNodes::PortIndex) const {
    return {"default", "Data"};
}
