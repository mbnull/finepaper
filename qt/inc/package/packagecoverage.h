#pragma once

#include "app/capabilityregistry.h"
#include "ipcraft/packagespec.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

enum class PackageFeatureCoverageStatus {
    Handled,
    Visible,
    Unsupported,
    Blocking,
    Invalid,
};

struct PackageFeatureCoverageItem {
    QString id;
    QString label;
    PackageFeatureCoverageStatus status = PackageFeatureCoverageStatus::Visible;
    QString message;
    QJsonObject descriptor;
};

struct PackageCoverageReport {
    QString packageId;
    QVector<PackageFeatureCoverageItem> items;

    bool hasBlockingItems() const;
    PackageFeatureCoverageItem item(const QString& id) const;
};

PackageCoverageReport buildPackageCoverageReport(const QJsonObject& descriptor,
                                                 const CapabilityRegistry& capabilities);
PackageCoverageReport buildPackageCoverageReport(const ipcraft::PackageSpec& spec,
                                                 const CapabilityRegistry& capabilities);
