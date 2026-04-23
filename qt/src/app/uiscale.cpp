// UiScale centralizes the process-wide Qt scale default for startup.
#include "app/uiscale.h"

#include <QString>
#include <QtGlobal>

namespace {

constexpr auto kQtScaleFactorName = "QT_SCALE_FACTOR";
constexpr auto kDefaultScaleFactor = "1.25";

} // namespace

namespace UiScale {

std::optional<QByteArray> defaultScaleFactor(const QProcessEnvironment& environment) {
    if (environment.contains(QString::fromLatin1(kQtScaleFactorName))) {
        return std::nullopt;
    }

    return QByteArray(kDefaultScaleFactor);
}

void applyDefaultScaleFactor() {
    const auto scaleFactor = defaultScaleFactor(QProcessEnvironment::systemEnvironment());
    if (scaleFactor.has_value()) {
        qputenv(kQtScaleFactorName, scaleFactor.value());
    }
}

} // namespace UiScale
