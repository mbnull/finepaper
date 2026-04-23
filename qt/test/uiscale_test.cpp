#include "app/uiscale.h"

#include <QProcessEnvironment>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

} // namespace

int main() {
    QProcessEnvironment unsetEnvironment;
    const auto defaultScale = UiScale::defaultScaleFactor(unsetEnvironment);
    require(defaultScale.has_value(), "expected default scale when QT_SCALE_FACTOR is unset");
    require(defaultScale.value() == QByteArray("1.25"), "expected default scale factor to be 1.25");

    QProcessEnvironment userEnvironment;
    userEnvironment.insert(QStringLiteral("QT_SCALE_FACTOR"), QStringLiteral("1.5"));
    const auto userScale = UiScale::defaultScaleFactor(userEnvironment);
    require(!userScale.has_value(), "expected user QT_SCALE_FACTOR to be preserved");

    std::cout << "uiscale_test passed" << std::endl;
    return 0;
}
