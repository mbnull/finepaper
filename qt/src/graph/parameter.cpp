// Parameter value object implementation.
#include "graph/parameter.h"

Parameter::Parameter(const QString& name, Value value)
    : m_name(name), m_value(value) {
}
