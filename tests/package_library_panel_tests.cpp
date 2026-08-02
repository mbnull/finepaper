#include "features/package_library/package_library_panel.h"

#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTextStream>

#include <QtTest/QTest>

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

template <typename Widget>
Widget* requiredChild(PackageLibraryPanel& panel, const QString& name) {
    Widget* child = panel.findChild<Widget*>(name);
    check(child != nullptr, QStringLiteral("panel exposes %1").arg(name));
    return child;
}

CreationPackageItem creationPackage(
    const QString& id,
    const QString& version,
    const QString& name) {
    return CreationPackageItem{
        PackageReference{id, version},
        name,
        QStringLiteral("%1@%2\nMesh: 1–16 × 1–16")
            .arg(id, version)};
}

EndpointLibraryItem endpointType(
    const QString& id, const QString& label) {
    return EndpointLibraryItem{
        id,
        label,
        QStringLiteral("%1\nDrag onto the canvas.").arg(id)};
}

PackageLibraryViewState noDesignState() {
    PackageLibraryViewState state;
    state.runnablePackages = {
        creationPackage(
            QStringLiteral("example.fabric"),
            QStringLiteral("1.0.0"),
            QStringLiteral("Example Fabric")),
        creationPackage(
            QStringLiteral("example.fabric-pro"),
            QStringLiteral("2.0.0"),
            QStringLiteral("Example Fabric Pro"))};
    state.fallbackCreationPackageKey =
        QStringLiteral("example.fabric@1.0.0");
    return state;
}

PackageLibraryViewState activeState(
    ActivePackageAvailability availability =
        ActivePackageAvailability::RuntimeReady) {
    PackageLibraryViewState state = noDesignState();
    state.activePackage.availability = availability;
    state.activePackage.reference = PackageReference{
        QStringLiteral("active.fabric"), QStringLiteral("7.1.0")};
    state.activePackage.name = QStringLiteral("Active Fabric");
    state.activePackage.metadataRoot = QStringLiteral("/packages/active");
    state.fallbackCreationPackageKey =
        QStringLiteral("example.fabric-pro@2.0.0");
    state.endpoints.availability = EndpointLibraryAvailability::Ready;
    state.endpoints.types = {
        endpointType(QStringLiteral("cpu"), QStringLiteral("CPU port")),
        endpointType(QStringLiteral("memory"), QStringLiteral("Memory port"))};
    state.endpoints.selectedRouterId = QStringLiteral("r-2-3");
    return state;
}

bool fullyVisibleWithin(QWidget* ancestor, QWidget* widget) {
    if (!ancestor || !widget || !widget->isVisibleTo(ancestor)) {
        return false;
    }
    const QRect mapped(
        widget->mapTo(ancestor, QPoint{}), widget->size());
    return ancestor->rect().contains(mapped);
}

bool fullyVisibleInScroll(QScrollArea* scroll, QWidget* widget) {
    if (!scroll || !widget || !widget->isVisibleTo(scroll)) {
        return false;
    }
    const QRect mapped(
        widget->mapTo(scroll->viewport(), QPoint{}), widget->size());
    return scroll->viewport()->rect().contains(mapped);
}

QWidget* nextFocusOutside(QWidget* owner) {
    if (!owner) {
        return nullptr;
    }
    QWidget* candidate = owner->nextInFocusChain();
    for (int step = 0; step < 32 && candidate
         && candidate != owner
         && owner->isAncestorOf(candidate); ++step) {
        candidate = candidate->nextInFocusChain();
    }
    return candidate;
}

void showPanel(PackageLibraryPanel& panel) {
    const QString sizeText = qEnvironmentVariable(
        "FINEPAPER_PACKAGE_LIBRARY_SIZE", QStringLiteral("320x480"));
    const QStringList parts = sizeText.split(QLatin1Char('x'));
    panel.resize(
        parts.size() == 2 ? parts.at(0).toInt() : 320,
        parts.size() == 2 ? parts.at(1).toInt() : 480);
    panel.show();
    QApplication::processEvents();
}

void noDesignPrimaryTaskAndMaintenanceFit() {
    PackageLibraryPanel panel;
    panel.setState(noDesignState());
    showPanel(panel);

    auto* scroll = requiredChild<QScrollArea>(
        panel, QStringLiteral("finepaper.packageLibraryScroll"));
    auto* selector = requiredChild<QComboBox>(
        panel, QStringLiteral("finepaper.packageSelector"));
    auto* details = requiredChild<QLabel>(
        panel, QStringLiteral("finepaper.creationPackageDetails"));
    auto* create = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.createDesign"));
    auto* install = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.installPackage"));
    auto* reload = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.reloadPackages"));
    auto* endpointSection = requiredChild<QWidget>(
        panel, QStringLiteral("finepaper.endpointLibrarySection"));

    check(selector && selector->isEnabled() && selector->count() == 2
              && selector->currentData().toString()
                     == QStringLiteral("example.fabric@1.0.0")
              && selector->currentText()
                     == QStringLiteral(
                         "Example Fabric — example.fabric@1.0.0")
              && selector->itemText(1)
                     == QStringLiteral(
                         "Example Fabric Pro — example.fabric-pro@2.0.0")
              && details && details->text().contains(
                     QStringLiteral("example.fabric@1.0.0")),
          QStringLiteral(
              "no-design state exposes an independent, visibly exact Package preference"));
    const bool selectorVisible = fullyVisibleInScroll(scroll, selector);
    const bool createVisible = fullyVisibleWithin(&panel, create);
    const bool installVisible = fullyVisibleWithin(&panel, install);
    const bool reloadVisible = fullyVisibleWithin(&panel, reload);
    check(scroll && selectorVisible && createVisible
              && installVisible && reloadVisible,
          QStringLiteral(
              "Package selection and pinned maintenance actions fit the initial viewport (%1/%2/%3/%4)")
              .arg(selectorVisible)
              .arg(createVisible)
              .arg(installVisible)
              .arg(reloadVisible)
              + (scroll && selector
                     ? QStringLiteral(
                           " selector=%1,%2 %3x%4 viewport=%5x%6")
                           .arg(selector->mapTo(
                                    scroll->viewport(), QPoint{}).x())
                           .arg(selector->mapTo(
                                    scroll->viewport(), QPoint{}).y())
                           .arg(selector->width())
                           .arg(selector->height())
                           .arg(scroll->viewport()->width())
                           .arg(scroll->viewport()->height())
                     : QString()));
    check(create && create->isVisible()
              && endpointSection && !endpointSection->isVisible(),
          QStringLiteral(
              "the no-design panel keeps a pinned creation fallback and hides Endpoint controls"));
    check(panel.preferredFocusTarget() == selector,
          QStringLiteral(
              "Package selection is the primary no-design Library task"));
}

void activeAndCreationPackagesStayIndependent() {
    PackageLibraryPanel panel;
    PackageLibraryViewState state = activeState(
        ActivePackageAvailability::MetadataOnly);
    panel.setState(state);
    showPanel(panel);

    auto* active = requiredChild<QLabel>(
        panel, QStringLiteral("finepaper.activePackage"));
    auto* warning = requiredChild<QLabel>(
        panel, QStringLiteral("finepaper.activePackageAvailability"));
    auto* selector = requiredChild<QComboBox>(
        panel, QStringLiteral("finepaper.packageSelector"));
    auto* details = requiredChild<QLabel>(
        panel, QStringLiteral("finepaper.creationPackageDetails"));
    auto* palette = requiredChild<QListWidget>(
        panel, QStringLiteral("finepaper.endpointPalette"));
    QSignalSpy changed(
        &panel, &PackageLibraryPanel::creationPackageChanged);

    check(active && active->text().contains(
                     QStringLiteral("active.fabric@7.1.0"))
              && warning && warning->isVisible()
              && warning->text().contains(
                     QStringLiteral("Runtime unavailable"))
              && selector && selector->currentData().toString()
                     == QStringLiteral("example.fabric-pro@2.0.0")
              && details && details->text().contains(
                     QStringLiteral("example.fabric-pro@2.0.0"))
              && palette && palette->isEnabled()
              && palette->count() == 2,
          QStringLiteral(
              "metadata-only active Package remains editable while new-design selection stays independent"));

    check(panel.selectCreationPackage(
              QStringLiteral("example.fabric@1.0.0"))
              && changed.size() == 1
              && active->text().contains(
                     QStringLiteral("active.fabric@7.1.0"))
              && palette->item(0)->data(Qt::UserRole).toString()
                     == QStringLiteral("cpu"),
          QStringLiteral(
              "changing the creation preference does not mutate active Package or Endpoint state"));

    state.runnablePackages.removeFirst();
    state.fallbackCreationPackageKey =
        QStringLiteral("example.fabric-pro@2.0.0");
    panel.setState(state);
    check(selector->currentData().toString()
              == QStringLiteral("example.fabric-pro@2.0.0"),
          QStringLiteral(
              "a disappeared preference falls back to an explicit runnable Package key"));
}

void endpointFilterAndKeyboardActivationUseTypedIds() {
    PackageLibraryPanel panel;
    panel.setState(activeState());
    showPanel(panel);
    auto* filter = requiredChild<QLineEdit>(
        panel, QStringLiteral("finepaper.endpointPaletteFilter"));
    auto* palette = requiredChild<QListWidget>(
        panel, QStringLiteral("finepaper.endpointPalette"));
    auto* add = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.addEndpointToRouter"));
    auto* hint = requiredChild<QLabel>(
        panel, QStringLiteral("finepaper.endpointPaletteHint"));
    QSignalSpy addRequested(
        &panel, &PackageLibraryPanel::endpointAddRequested);
    if (!filter || !palette || !add) {
        return;
    }

    filter->setText(QStringLiteral("memory"));
    QApplication::processEvents();
    check(palette->item(0)->isHidden()
              && !palette->item(1)->isHidden(),
          QStringLiteral(
              "visible Endpoint filter matches labels and preserves the full source list"));
    filter->setText(QStringLiteral("no-such-type"));
    QApplication::processEvents();
    check(hint && hint->text().contains(
                     QStringLiteral("No Endpoint types match"))
              && hint->accessibleDescription() == hint->text()
              && !add->isEnabled(),
          QStringLiteral(
              "an empty filter result explains how to recover instead of asking for an unavailable type"));
    filter->clear();
    palette->setCurrentRow(1);
    palette->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(palette, Qt::Key_Return);
    QApplication::processEvents();
    check(add->isEnabled() && addRequested.size() == 1
              && addRequested.takeFirst().at(0).toString()
                     == QStringLiteral("memory"),
          QStringLiteral(
              "Enter activates the selected Endpoint using its typed ID"));
}

void focusRoutingRevealsTheCurrentTask() {
    PackageLibraryPanel panel;
    panel.setState(activeState());
    showPanel(panel);
    auto* scroll = requiredChild<QScrollArea>(
        panel, QStringLiteral("finepaper.packageLibraryScroll"));
    auto* filter = requiredChild<QLineEdit>(
        panel, QStringLiteral("finepaper.endpointPaletteFilter"));
    auto* selector = requiredChild<QComboBox>(
        panel, QStringLiteral("finepaper.packageSelector"));
    if (!scroll || !filter || !selector) {
        return;
    }

    check(panel.preferredFocusTarget() == filter,
          QStringLiteral(
              "an editable active design routes Library navigation to Endpoint work"));
    QApplication::processEvents();
    check(fullyVisibleInScroll(scroll, filter),
          QStringLiteral(
              "routing to Endpoint work scrolls its filter fully into view"));

    check(panel.selectCreationPackage(
              QStringLiteral("example.fabric@1.0.0")),
          QStringLiteral("creation Package can be selected by exact key"));
    QApplication::processEvents();
    check(fullyVisibleInScroll(scroll, selector),
          QStringLiteral(
              "switching back to design creation reveals the Package selector"));
}

void missingAndInterlockedStatesRemainSpecific() {
    PackageLibraryPanel panel;
    PackageLibraryViewState state = noDesignState();
    state.activePackage.availability = ActivePackageAvailability::Missing;
    state.activePackage.reference = PackageReference{
        QStringLiteral("missing.fabric"), QStringLiteral("9.0.0")};
    state.endpoints.availability =
        EndpointLibraryAvailability::PackageMissing;
    state.interlocks.cleanupUnresolved = true;
    state.interlocks.cleanupBlockedReason =
        QStringLiteral("Restart before changing Packages.");
    panel.setState(state);
    showPanel(panel);

    auto* selector = requiredChild<QComboBox>(
        panel, QStringLiteral("finepaper.packageSelector"));
    auto* create = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.createDesign"));
    auto* install = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.installPackage"));
    auto* reload = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.reloadPackages"));
    auto* palette = requiredChild<QListWidget>(
        panel, QStringLiteral("finepaper.endpointPalette"));
    auto* active = requiredChild<QLabel>(
        panel, QStringLiteral("finepaper.activePackage"));

    check(selector && selector->isEnabled()
              && create && create->isEnabled()
              && install && !install->isEnabled()
              && reload && !reload->isEnabled()
              && install->toolTip().contains(QStringLiteral("Restart"))
              && palette && !palette->isEnabled()
              && active && active->text().contains(
                     QStringLiteral("missing.fabric@9.0.0")),
          QStringLiteral(
              "cleanup failure blocks Package mutation without conflating creation or missing active metadata"));

    state.interlocks.cleanupUnresolved = false;
    state.interlocks.cleanupBlockedReason.clear();
    state.interlocks.endpointDraftsUnresolved = true;
    state.interlocks.endpointDraftBlockedReason = QStringLiteral(
        "Package maintenance is unavailable while an Endpoint draft remains unresolved.");
    panel.setState(state);
    check(install && !install->isEnabled()
              && reload && !reload->isEnabled()
              && install->toolTip().contains(
                  QStringLiteral("Endpoint draft"))
              && reload->toolTip() == install->toolTip(),
          QStringLiteral(
              "Endpoint drafts consistently interlock both Package maintenance routes with a recovery reason"));
}

void emptyCatalogAndZeroEndpointTypesExplainRecovery() {
    PackageLibraryPanel emptyPanel;
    emptyPanel.setState({});
    showPanel(emptyPanel);
    auto* emptySelector = requiredChild<QComboBox>(
        emptyPanel, QStringLiteral("finepaper.packageSelector"));
    auto* emptyDetails = requiredChild<QLabel>(
        emptyPanel, QStringLiteral("finepaper.creationPackageDetails"));
    auto* install = requiredChild<QPushButton>(
        emptyPanel, QStringLiteral("finepaper.installPackage"));
    check(emptySelector && !emptySelector->isEnabled()
              && emptySelector->currentData().toString().isEmpty()
              && emptyDetails && emptyDetails->text().contains(
                     QStringLiteral("Install or repair"))
              && install && install->isEnabled()
              && emptyPanel.preferredFocusTarget() == install,
          QStringLiteral(
              "an empty catalog names the recovery action and routes focus to Install"));

    PackageLibraryPanel noTypesPanel;
    PackageLibraryViewState noTypes = activeState();
    noTypes.endpoints.availability =
        EndpointLibraryAvailability::NoTypes;
    noTypes.endpoints.types.clear();
    noTypesPanel.setState(noTypes);
    showPanel(noTypesPanel);
    auto* palette = requiredChild<QListWidget>(
        noTypesPanel, QStringLiteral("finepaper.endpointPalette"));
    auto* hint = requiredChild<QLabel>(
        noTypesPanel, QStringLiteral("finepaper.endpointPaletteHint"));
    check(palette && !palette->isEnabled() && palette->count() == 0
              && hint && hint->text().contains(
                     QStringLiteral("does not declare")),
          QStringLiteral(
              "a Package with zero Endpoint types is distinct from a missing Package or busy operation"));
}

void accessibilityAndReadingOrderAreExplicit() {
    PackageLibraryPanel panel;
    panel.setState(activeState());
    showPanel(panel);
    auto* selector = requiredChild<QComboBox>(
        panel, QStringLiteral("finepaper.packageSelector"));
    auto* details = requiredChild<QLabel>(
        panel, QStringLiteral("finepaper.creationPackageDetails"));
    auto* filter = requiredChild<QLineEdit>(
        panel, QStringLiteral("finepaper.endpointPaletteFilter"));
    auto* palette = requiredChild<QListWidget>(
        panel, QStringLiteral("finepaper.endpointPalette"));
    auto* add = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.addEndpointToRouter"));
    auto* warning = requiredChild<QLabel>(
        panel, QStringLiteral("finepaper.activePackageAvailability"));
    auto* create = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.createDesign"));
    auto* install = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.installPackage"));
    auto* reload = requiredChild<QPushButton>(
        panel, QStringLiteral("finepaper.reloadPackages"));
    if (!selector || !details || !filter || !palette || !add
        || !warning || !create || !install || !reload) {
        return;
    }

    check(!selector->accessibleName().isEmpty()
              && selector->accessibleDescription().contains(
                     QStringLiteral("does not change"))
              && !details->accessibleName().isEmpty()
              && details->focusPolicy() == Qt::NoFocus
              && warning->focusPolicy() == Qt::NoFocus
              && !filter->accessibleName().isEmpty()
              && !palette->accessibleDescription().isEmpty(),
          QStringLiteral(
              "Package and Endpoint controls expose their distinct accessible purpose without tab-stopping on static text"));
    const bool readingOrder = selector->nextInFocusChain() == filter
              && filter->nextInFocusChain() == palette
              && nextFocusOutside(palette) == add
              && add->nextInFocusChain() == create
              && create->nextInFocusChain() == install
              && install->nextInFocusChain() == reload;
    check(readingOrder,
          QStringLiteral(
              "explicit Tab order follows the interactive Package-to-Endpoint-to-footer reading order (%1 -> %2 -> %3 -> %4)")
              .arg(selector->nextInFocusChain()->objectName(),
                   filter->nextInFocusChain()->objectName(),
                   nextFocusOutside(palette)
                       ? nextFocusOutside(palette)->objectName()
                       : QStringLiteral("none"),
                   add->nextInFocusChain()->objectName()));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    bool fontScaleOk = false;
    const double fontScale = qEnvironmentVariable(
        "FINEPAPER_PACKAGE_LIBRARY_FONT_SCALE").toDouble(&fontScaleOk);
    if (fontScaleOk && fontScale > 0.0) {
        QFont font = application.font();
        if (font.pointSizeF() > 0.0) {
            font.setPointSizeF(font.pointSizeF() * fontScale);
        } else {
            font.setPixelSize(qRound(font.pixelSize() * fontScale));
        }
        application.setFont(font);
    }

    noDesignPrimaryTaskAndMaintenanceFit();
    activeAndCreationPackagesStayIndependent();
    endpointFilterAndKeyboardActivationUseTypedIds();
    focusRoutingRevealsTheCurrentTask();
    missingAndInterlockedStatesRemainSpecific();
    emptyCatalogAndZeroEndpointTypesExplainRecovery();
    accessibilityAndReadingOrderAreExplicit();

    QTextStream(stdout)
        << (failures == 0 ? "package-library-panel tests passed"
                          : "package-library-panel tests failed")
        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
