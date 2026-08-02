#include "features/domain/domain_assignment_task_bar.h"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QTextStream>
#include <QVector>

#include <QtTest/QTest>

#include <algorithm>

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
Widget* requiredChild(DomainAssignmentTaskBar& bar, const QString& name) {
    Widget* child = bar.findChild<Widget*>(name);
    check(child != nullptr, QStringLiteral("task bar exposes %1").arg(name));
    return child;
}

QRect mappedRect(QWidget& ancestor, QWidget* widget) {
    return widget
        ? QRect(widget->mapTo(&ancestor, QPoint{}), widget->size())
        : QRect{};
}

bool fullyVisibleWithin(QWidget& ancestor, QWidget* widget) {
    return widget && widget->isVisibleTo(&ancestor)
        && ancestor.rect().contains(mappedRect(ancestor, widget));
}

void inactiveTaskKeepsItsStableEndingVisible() {
    DomainAssignmentTaskBar bar;
    bar.resize(240, 320);
    bar.show();
    QApplication::processEvents();

    auto* title = requiredChild<QLabel>(
        bar, QStringLiteral("finepaper.domainAssignmentTaskBar.title"));
    auto* status = requiredChild<QLabel>(
        bar, QStringLiteral("finepaper.domainAssignmentTaskBar.status"));
    auto* apply = requiredChild<QPushButton>(
        bar, QStringLiteral("finepaper.domainManager.applyAssignment"));
    auto* discard = requiredChild<QPushButton>(
        bar, QStringLiteral("finepaper.domainManager.discardAssignment"));
    if (!title || !status || !apply || !discard) {
        return;
    }

    check(bar.isVisible() && title->isVisible() && status->isVisible()
              && apply->isVisible() && discard->isVisible(),
          QStringLiteral(
              "inactive mode retains the title, status, Apply, and Discard landmarks"));
    check(!apply->isEnabled() && !discard->isEnabled()
              && !bar.property("finepaperTaskActive").toBool()
              && bar.preferredFocusTarget() == nullptr,
          QStringLiteral(
              "inactive mode disables both endings and exposes no unavailable focus target"));
    check(!bar.accessibleName().trimmed().isEmpty()
              && !bar.accessibleDescription().trimmed().isEmpty()
              && !apply->accessibleName().trimmed().isEmpty()
              && !apply->accessibleDescription().trimmed().isEmpty()
              && !discard->accessibleName().trimmed().isEmpty()
              && !discard->accessibleDescription().trimmed().isEmpty(),
          QStringLiteral(
              "inactive task state remains understandable to assistive technology"));
}

void activeTaskForwardsKeyboardActions() {
    DomainAssignmentTaskBar bar;
    DomainAssignmentTaskBarState state;
    state.taskActive = true;
    state.title = QStringLiteral("Assign the selected Router and Endpoint nodes");
    state.status = QStringLiteral(
        "3 selected nodes will join the shared security boundary.");
    state.discardText = QStringLiteral("Done");
    state.discardAccessibleDescription = QStringLiteral(
        "Finish this Domain assignment task.");
    state.applyEnabled = true;
    state.discardEnabled = true;
    bar.setState(state);
    bar.resize(360, 300);
    bar.show();
    QApplication::processEvents();

    auto* apply = requiredChild<QPushButton>(
        bar, QStringLiteral("finepaper.domainManager.applyAssignment"));
    auto* discard = requiredChild<QPushButton>(
        bar, QStringLiteral("finepaper.domainManager.discardAssignment"));
    if (!apply || !discard) {
        return;
    }

    int applyCount = 0;
    int discardCount = 0;
    bar.applyRequested = [&] { ++applyCount; };
    bar.discardRequested = [&] { ++discardCount; };

    apply->setFocus(Qt::TabFocusReason);
    QTest::keyClick(apply, Qt::Key_Space);
    discard->setFocus(Qt::TabFocusReason);
    QTest::keyClick(discard, Qt::Key_Space);
    QApplication::processEvents();

    check(bar.property("finepaperTaskActive").toBool()
              && apply->isEnabled() && discard->isEnabled()
              && applyCount == 1 && discardCount == 1,
          QStringLiteral(
              "active mode enables and forwards both task endings from the keyboard"));
    check(apply->focusPolicy() == Qt::StrongFocus
              && discard->focusPolicy() == Qt::StrongFocus
              && discard->accessibleDescription()
                  == state.discardAccessibleDescription
              && bar.preferredFocusTarget() == apply,
          QStringLiteral(
              "keyboard focus follows the visual Apply then Discard order"));
}

void narrowAndLargeTextLayoutsRemainReadable() {
    const QFont originalFont = QApplication::font();
    QFont largeFont = originalFont;
    if (largeFont.pointSizeF() > 0.0) {
        largeFont.setPointSizeF(largeFont.pointSizeF() * 2.0);
    } else if (largeFont.pixelSize() > 0) {
        largeFont.setPixelSize(largeFont.pixelSize() * 2);
    }

    for (const int width : {240, 300, 360}) {
        DomainAssignmentTaskBar bar;
        bar.setFont(largeFont);
        DomainAssignmentTaskBarState state;
        state.taskActive = true;
        // Roughly 40% expansion over the production copy exercises both
        // localisation growth and verbose Package-defined Domain names.
        state.title = QStringLiteral(
            "Assign the currently selected Router and Endpoint nodes to this Domain");
        state.status = QStringLiteral(
            "3 of 4 selected semantic nodes are eligible; review their shared assignment before continuing.");
        state.applyText = QStringLiteral("Apply now");
        state.discardText = QStringLiteral("Discard now");
        state.applyEnabled = true;
        state.discardEnabled = true;
        bar.setState(state);

        const int layoutHeight = bar.layout()->hasHeightForWidth()
            ? bar.layout()->heightForWidth(width)
            : bar.sizeHint().height();
        bar.resize(width, (std::max)(layoutHeight, bar.sizeHint().height()));
        bar.show();
        QApplication::processEvents();

        auto* title = requiredChild<QLabel>(
            bar, QStringLiteral("finepaper.domainAssignmentTaskBar.title"));
        auto* status = requiredChild<QLabel>(
            bar, QStringLiteral("finepaper.domainAssignmentTaskBar.status"));
        auto* apply = requiredChild<QPushButton>(
            bar, QStringLiteral("finepaper.domainManager.applyAssignment"));
        auto* discard = requiredChild<QPushButton>(
            bar, QStringLiteral("finepaper.domainManager.discardAssignment"));
        if (!title || !status || !apply || !discard) {
            continue;
        }

        const QVector<QWidget*> content{title, status, apply, discard};
        bool allVisible = true;
        bool noOverlap = true;
        for (qsizetype index = 0; index < content.size(); ++index) {
            QWidget* widget = content.at(index);
            allVisible = allVisible && widget->width() > 0
                && widget->height() > 0
                && fullyVisibleWithin(bar, widget);
            const QRect current = mappedRect(bar, widget);
            for (qsizetype other = index + 1;
                 other < content.size(); ++other) {
                noOverlap = noOverlap
                    && !current.intersects(
                        mappedRect(bar, content.at(other)));
            }
        }
        check(allVisible && noOverlap,
              QStringLiteral(
                  "%1 px task bar keeps 2x, expanded text and both actions fully visible without overlap")
                  .arg(width));
        check(title->text() == state.title && status->text() == state.status
                  && apply->text() == state.applyText
                  && discard->text() == state.discardText,
              QStringLiteral(
                  "%1 px task bar preserves complete expanded visible copy")
                  .arg(width));
        const QRect applyRect = mappedRect(bar, apply);
        const QRect discardRect = mappedRect(bar, discard);
        const bool actionsFollowReadingOrder =
            applyRect.bottom() < discardRect.top()
            || (applyRect.top() == discardRect.top()
                && applyRect.right() < discardRect.left());
        check(actionsFollowReadingOrder,
              QStringLiteral(
                  "%1 px task bar lays out actions in reading order")
                  .arg(width));
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);

    inactiveTaskKeepsItsStableEndingVisible();
    activeTaskForwardsKeyboardActions();
    narrowAndLargeTextLayoutsRemainReadable();

    QTextStream(stdout)
        << (failures == 0
                ? "domain-assignment-task-bar tests passed"
                : "domain-assignment-task-bar tests failed")
        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
