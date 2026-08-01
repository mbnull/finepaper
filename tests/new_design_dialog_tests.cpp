#include "features/design_creation/new_design_dialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QScrollArea>
#include <QSpinBox>
#include <QTextStream>

#include <QtTest/QTest>

#include <algorithm>
#include <utility>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& message) {
    if (condition) {
        return;
    }
    QTextStream(stderr) << "FAILED: " << message << Qt::endl;
    ++failures;
}

DesignCreationPackageOption packageOption(
    const QString& id,
    const QString& version,
    const QString& name,
    int minimumRows,
    int maximumRows,
    int defaultRows,
    int minimumColumns,
    int maximumColumns,
    int defaultColumns,
    QStringList endpointTypes,
    QStringList domainTypes) {
    DesignCreationPackageOption option;
    option.reference = PackageReference{id, version};
    option.name = name;
    option.defaultTopology = TopologySpec{
        QStringLiteral("mesh"), defaultRows, defaultColumns};
    option.minimumRows = minimumRows;
    option.maximumRows = maximumRows;
    option.minimumColumns = minimumColumns;
    option.maximumColumns = maximumColumns;
    option.endpointTypes = std::move(endpointTypes);
    option.domainTypes = std::move(domainTypes);
    option.endpointTypeCount = option.endpointTypes.size();
    option.domainTypeCount = option.domainTypes.size();
    option.elementPropertySetCount = 2;
    option.designExtensionCount = 1;
    return option;
}

QVector<DesignCreationPackageOption> packageOptions() {
    return {
        packageOption(
            QStringLiteral("example.fabric"),
            QStringLiteral("1.2.0"),
            QStringLiteral("Example Fabric"),
            1, 8, 2,
            1, 9, 3,
            {QStringLiteral("CPU port (cpu)"),
             QStringLiteral("Memory port (memory)")},
            {QStringLiteral("Voltage island (voltage-island)"),
             QStringLiteral("Security region (security-region)")}),
        packageOption(
            QStringLiteral("example.fabric-pro"),
            QStringLiteral("4.0.1"),
            QStringLiteral("Example Fabric Pro"),
            2, 12, 4,
            3, 14, 5,
            {QStringLiteral("Accelerator (accelerator)")},
            {QStringLiteral("Thermal region (thermal-region)")})
    };
}

template <typename Widget>
Widget* requiredChild(NewDesignDialog& dialog, const QString& objectName) {
    Widget* child = dialog.findChild<Widget*>(objectName);
    check(child != nullptr,
          QStringLiteral("dialog exposes %1").arg(objectName));
    return child;
}

bool ownsKeyboardFocus(QWidget* widget) {
    QWidget* focused = QApplication::focusWidget();
    return widget && focused
        && (focused == widget || widget->isAncestorOf(focused));
}

void packageProjectionAndTypedDraftRoundTrip() {
    NewDesignDialog dialog(
        packageOptions(),
        QStringLiteral("example.fabric-pro@4.0.1"),
        QStringLiteral("Initial design"));

    auto* selector = requiredChild<QComboBox>(
        dialog, QStringLiteral("finepaper.newDesignPackageSelector"));
    auto* name = requiredChild<QLineEdit>(
        dialog, QStringLiteral("finepaper.newDesignName"));
    auto* rows = requiredChild<QSpinBox>(
        dialog, QStringLiteral("finepaper.newDesignRows"));
    auto* columns = requiredChild<QSpinBox>(
        dialog, QStringLiteral("finepaper.newDesignColumns"));
    auto* topology = requiredChild<QLabel>(
        dialog, QStringLiteral("finepaper.newDesignTopologyType"));
    auto* details = requiredChild<QLabel>(
        dialog, QStringLiteral("finepaper.newDesignPackageDetails"));
    if (!selector || !name || !rows || !columns || !topology || !details) {
        return;
    }

    check(selector->count() == 2
              && selector->currentData().toString()
                     == QStringLiteral("example.fabric-pro@4.0.1"),
          QStringLiteral("preferred Package version is selected by exact key"));
    check(rows->minimum() == 2 && rows->maximum() == 12 && rows->value() == 4
              && columns->minimum() == 3 && columns->maximum() == 14
              && columns->value() == 5,
          QStringLiteral("selected Package owns topology bounds and defaults"));
    check(topology->text() == QStringLiteral("Mesh"),
          QStringLiteral("the current topology type is visible as text"));
    check(details->textFormat() == Qt::PlainText
              && details->text().contains(QStringLiteral("Example Fabric Pro"))
              && details->text().contains(QStringLiteral("example.fabric-pro@4.0.1"))
              && details->text().contains(QStringLiteral("Accelerator"))
              && details->text().contains(QStringLiteral("Thermal region")),
          QStringLiteral("Package capabilities use safe plain text and include arbitrary declarations"));

    name->setText(QStringLiteral("  Package-driven NoC  "));
    rows->setValue(10);
    columns->setValue(11);
    const DesignCreationRequest request = dialog.draft();
    check(request.name == QStringLiteral("Package-driven NoC")
              && request.package
                     == PackageReference{QStringLiteral("example.fabric-pro"),
                                         QStringLiteral("4.0.1")}
              && request.topology.type == QStringLiteral("mesh")
              && request.topology.rows == 10
              && request.topology.columns == 11
              && !request.domainConfiguration,
          QStringLiteral("dialog returns a typed initial-design request without JSON protocol fields"));

    PackageDefinition package;
    package.id = QStringLiteral("projection.test");
    package.version = QStringLiteral("3.1.4");
    package.name = QStringLiteral("Projection Test");
    package.mesh = MeshDefinition{1, 6, 2, 2, 7, 3};
    package.endpointTypes = {
        EndpointTypeDefinition{QStringLiteral("io"), QStringLiteral("I/O")}};
    DomainTypeDefinition customDomain;
    customDomain.id = QStringLiteral("thermal-zone");
    customDomain.label = QStringLiteral("Thermal zone");
    package.domainTypes = {customDomain};
    for (int index = 1; index <= 6; ++index) {
        DomainTypeDefinition extraDomain;
        extraDomain.id = QStringLiteral("extra-domain-%1").arg(index);
        extraDomain.label = QStringLiteral("Extra domain %1").arg(index);
        package.domainTypes.append(std::move(extraDomain));
    }
    const DesignCreationPackageOption projected =
        designCreationPackageOption(package);
    check(projected.key() == QStringLiteral("projection.test@3.1.4")
              && projected.defaultTopology.rows == 2
              && projected.defaultTopology.columns == 3
              && projected.endpointTypes
                     == QStringList{QStringLiteral("I/O (io)")}
              && projected.domainTypeCount == 7
              && projected.domainTypes.size() == 4
              && projected.domainTypes.constFirst()
                     == QStringLiteral("Thermal zone (thermal-zone)"),
          QStringLiteral("Package metadata is projected into a small value-only UI model"));

    NewDesignDialog projectedDialog(
        {projected}, projected.key(), QStringLiteral("Bounded summary"));
    auto* projectedDetails = projectedDialog.findChild<QLabel*>(
        QStringLiteral("finepaper.newDesignPackageDetails"));
    check(projectedDetails
              && projectedDetails->text().contains(QStringLiteral("+3 more"))
              && !projectedDetails->text().contains(
                     QStringLiteral("Extra domain 4")),
          QStringLiteral(
              "large Package capability lists use a bounded preview with an exact remaining count"));
}

void packageSwitchPreservesIndependentTopologyDrafts() {
    NewDesignDialog dialog(
        packageOptions(),
        QStringLiteral("example.fabric@1.2.0"),
        QStringLiteral("Switch test"));
    auto* selector = requiredChild<QComboBox>(
        dialog, QStringLiteral("finepaper.newDesignPackageSelector"));
    auto* rows = requiredChild<QSpinBox>(
        dialog, QStringLiteral("finepaper.newDesignRows"));
    auto* columns = requiredChild<QSpinBox>(
        dialog, QStringLiteral("finepaper.newDesignColumns"));
    if (!selector || !rows || !columns) {
        return;
    }

    rows->setValue(7);
    columns->setValue(8);
    selector->setCurrentIndex(1);
    check(rows->value() == 4 && columns->value() == 5,
          QStringLiteral("a newly selected Package starts with its own defaults"));
    rows->setValue(11);
    columns->setValue(13);

    selector->setCurrentIndex(0);
    check(rows->value() == 7 && columns->value() == 8,
          QStringLiteral("returning to Package A restores its topology draft"));
    selector->setCurrentIndex(1);
    check(rows->value() == 11 && columns->value() == 13,
          QStringLiteral("returning to Package B restores its independent topology draft"));
}

void validationAndKeyboardContract() {
    NewDesignDialog dialog(
        packageOptions(),
        QStringLiteral("example.fabric@1.2.0"),
        QStringLiteral("Keyboard test"));
    dialog.show();
    QApplication::processEvents();

    auto* selector = requiredChild<QComboBox>(
        dialog, QStringLiteral("finepaper.newDesignPackageSelector"));
    auto* name = requiredChild<QLineEdit>(
        dialog, QStringLiteral("finepaper.newDesignName"));
    auto* rows = requiredChild<QSpinBox>(
        dialog, QStringLiteral("finepaper.newDesignRows"));
    auto* columns = requiredChild<QSpinBox>(
        dialog, QStringLiteral("finepaper.newDesignColumns"));
    auto* validation = requiredChild<QLabel>(
        dialog, QStringLiteral("finepaper.newDesignValidation"));
    auto* buttons = requiredChild<QDialogButtonBox>(
        dialog, QStringLiteral("finepaper.newDesignButtons"));
    auto* create = requiredChild<QPushButton>(
        dialog, QStringLiteral("finepaper.newDesignCreate"));
    if (!selector || !name || !rows || !columns || !validation
        || !buttons || !create) {
        return;
    }

    check(buttons->button(QDialogButtonBox::Ok) == create
              && create->text() == QStringLiteral("Create Design")
              && create->isDefault() && create->isEnabled(),
          QStringLiteral("the text-first Create action is the enabled default action"));
    name->setText(QStringLiteral("   "));
    QApplication::processEvents();
    check(!create->isEnabled() && validation->isVisibleTo(&dialog)
              && validation->text().contains(QStringLiteral("design name"),
                                             Qt::CaseInsensitive),
          QStringLiteral("blank names immediately expose an inline recovery message"));
    name->setText(QStringLiteral("Valid name"));
    QApplication::processEvents();
    check(create->isEnabled() && !validation->isVisibleTo(&dialog),
          QStringLiteral("valid input immediately restores the Create action"));

    check(!selector->accessibleName().isEmpty()
              && !name->accessibleName().isEmpty()
              && !rows->accessibleName().isEmpty()
              && !columns->accessibleName().isEmpty()
              && !create->accessibleName().isEmpty(),
          QStringLiteral("every primary input and action has an accessible name"));

    selector->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    QTest::keyClick(selector, Qt::Key_Tab);
    check(ownsKeyboardFocus(name),
          QStringLiteral("Tab moves from Package to design name"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    check(ownsKeyboardFocus(rows),
          QStringLiteral("Tab moves from design name to rows"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    check(ownsKeyboardFocus(columns),
          QStringLiteral("Tab moves from rows to columns"));

    NewDesignDialog emptyDialog({}, {}, QStringLiteral("No Package"));
    emptyDialog.show();
    QApplication::processEvents();
    auto* emptyCreate = requiredChild<QPushButton>(
        emptyDialog, QStringLiteral("finepaper.newDesignCreate"));
    auto* emptyValidation = requiredChild<QLabel>(
        emptyDialog, QStringLiteral("finepaper.newDesignValidation"));
    check(emptyCreate && !emptyCreate->isEnabled()
              && emptyValidation && emptyValidation->isVisibleTo(&emptyDialog)
              && emptyValidation->text().contains(QStringLiteral("Package")),
          QStringLiteral("an empty Package list fails closed with visible recovery guidance"));
}

void largeFontFitsMinimumDesktop() {
    NewDesignDialog dialog(
        packageOptions(),
        QStringLiteral("example.fabric@1.2.0"),
        QStringLiteral("Accessible design"));
    dialog.resize(800, 600);
    dialog.show();
    QApplication::processEvents();

    auto* scroll = requiredChild<QScrollArea>(
        dialog, QStringLiteral("finepaper.newDesignScroll"));
    auto* buttons = requiredChild<QDialogButtonBox>(
        dialog, QStringLiteral("finepaper.newDesignButtons"));
    check(dialog.minimumSizeHint().width() <= 800
              && dialog.minimumSizeHint().height() <= 600
              && dialog.width() <= 800 && dialog.height() <= 600,
          QStringLiteral("the dialog remains usable within an 800 × 600 desktop"));
    check(scroll
              && scroll->horizontalScrollBarPolicy()
                     == Qt::ScrollBarAlwaysOff,
          QStringLiteral("responsive content wraps instead of requiring horizontal scrolling"));
    if (!scroll || !buttons) {
        return;
    }

    const QRect dialogContents = dialog.contentsRect();
    const QRect buttonRect(
        buttons->mapTo(&dialog, QPoint(0, 0)), buttons->size());
    check(dialogContents.contains(buttonRect)
              && !buttonRect.intersects(
                     QRect(scroll->mapTo(&dialog, QPoint(0, 0)),
                           scroll->size())),
          QStringLiteral("Create and Cancel remain pinned, visible, and separate from scrolling content"));

    const QStringList scrollableControls = {
        QStringLiteral("finepaper.newDesignPackageSelector"),
        QStringLiteral("finepaper.newDesignName"),
        QStringLiteral("finepaper.newDesignRows"),
        QStringLiteral("finepaper.newDesignColumns"),
        QStringLiteral("finepaper.newDesignPackageDetails")
    };
    for (const QString& objectName : scrollableControls) {
        QWidget* widget = dialog.findChild<QWidget*>(objectName);
        check(widget != nullptr,
              QStringLiteral("large-font dialog exposes %1").arg(objectName));
        if (!widget) {
            continue;
        }
        scroll->ensureWidgetVisible(widget, 0, 8);
        QApplication::processEvents();
        const QRect widgetRect(
            widget->mapTo(scroll->viewport(), QPoint(0, 0)), widget->size());
        check(scroll->viewport()->rect().contains(widgetRect),
              QStringLiteral("%1 can be brought fully into the visible settings viewport")
                  .arg(objectName));
    }

    const QString screenshotPath = qEnvironmentVariable(
        "FINEPAPER_NEW_DESIGN_SCREENSHOT").trimmed();
    if (!screenshotPath.isEmpty()) {
        check(dialog.grab().save(screenshotPath, "PNG"),
              QStringLiteral("new design dialog screenshot is saved to %1")
                  .arg(screenshotPath));
    }
}

void applyRequestedFontScale(QApplication& application) {
    bool ok = false;
    const double scale = qEnvironmentVariable(
        "FINEPAPER_NEW_DESIGN_FONT_SCALE").toDouble(&ok);
    if (!ok || scale <= 0.0 || qFuzzyCompare(scale, 1.0)) {
        return;
    }
    QFont font = application.font();
    if (font.pointSizeF() > 0.0) {
        font.setPointSizeF(font.pointSizeF() * scale);
    } else if (font.pixelSize() > 0) {
        font.setPixelSize((std::max)(1, qRound(font.pixelSize() * scale)));
    }
    application.setFont(font);
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    applyRequestedFontScale(application);

    packageProjectionAndTypedDraftRoundTrip();
    packageSwitchPreservesIndependentTopologyDrafts();
    validationAndKeyboardContract();
    largeFontFitsMinimumDesktop();

    QTextStream(stdout)
        << (failures == 0
                ? "All new design dialog tests passed."
                : "New design dialog tests failed.")
        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
