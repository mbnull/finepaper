#include "ui/components/operation_task_strip.h"

#include "ui/theme/ui_tokens.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>

#include <algorithm>

namespace finepaper::ui {
namespace {

constexpr int kProgressDelayMilliseconds = 350;
constexpr int kElapsedRevealMilliseconds = 10'000;
constexpr int kElapsedRefreshMilliseconds = 1'000;
constexpr int kOperationLabelMaximumEms = 28;
constexpr int kOperationLabelMinimumEms = 4;
constexpr int kProgressWidthEms = 4;
constexpr int kAnimatedProgressMinimumWidthEms = 40;

int emWidth(const QWidget& widget, int count) {
    return (std::max)(
        1, widget.fontMetrics().horizontalAdvance(QLatin1Char('M')) * count);
}

class ElidingLabel final : public QLabel {
public:
    using QLabel::QLabel;

    void setFullText(const QString& text) {
        m_fullText = text;
        setToolTip(m_fullText);
        setAccessibleName(m_fullText);
        refreshElidedText();
    }

    [[nodiscard]] QSize minimumSizeHint() const override {
        return QSize(
            emWidth(*this, kOperationLabelMinimumEms),
            QLabel::minimumSizeHint().height());
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        refreshElidedText();
    }

private:
    void refreshElidedText() {
        QLabel::setText(fontMetrics().elidedText(
            m_fullText,
            Qt::ElideMiddle,
            (std::max)(0, contentsRect().width())));
    }

    QString m_fullText;
};

} // namespace

OperationTaskStrip::OperationTaskStrip(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finepaper.operationTaskStrip"));
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    auto* stripLayout = new QHBoxLayout(this);
    stripLayout->setContentsMargins(0, 0, 0, 0);
    stripLayout->setSpacing(UiMetrics::spacing8);

    m_stateLabel = new QLabel(this);
    m_stateLabel->setObjectName(
        QStringLiteral("finepaper.operationTaskState"));
    m_stateLabel->setProperty(
        "finepaperRole", QStringLiteral("warning"));

    m_operationLabel = new ElidingLabel(this);
    m_operationLabel->setObjectName(
        QStringLiteral("finepaper.operationTaskName"));
    m_operationLabel->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_elapsedLabel = new QLabel(this);
    m_elapsedLabel->setObjectName(
        QStringLiteral("finepaper.operationElapsed"));
    m_elapsedLabel->setProperty(
        "finepaperRole", QStringLiteral("muted"));

    m_progress = new QProgressBar(this);
    m_progress->setObjectName(
        QStringLiteral("finepaper.operationProgress"));
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFocusPolicy(Qt::NoFocus);
    m_progress->setAccessibleName(
        QStringLiteral("Operation is still running"));

    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelButton->setObjectName(
        QStringLiteral("finepaper.cancelOperation"));
    m_cancelButton->setProperty(
        "finepaperRole", QStringLiteral("danger"));
    m_cancelButton->setFocusPolicy(Qt::StrongFocus);

    stripLayout->addWidget(m_stateLabel);
    stripLayout->addWidget(m_operationLabel, 1);
    stripLayout->addWidget(m_elapsedLabel);
    stripLayout->addWidget(m_progress);
    stripLayout->addWidget(m_cancelButton);

    m_elapsedTimer = new QTimer(this);
    m_elapsedTimer->setInterval(kElapsedRefreshMilliseconds);
    connect(m_elapsedTimer, &QTimer::timeout,
            this, &OperationTaskStrip::refreshElapsedTime);

    m_progressDelay = new QTimer(this);
    m_progressDelay->setSingleShot(true);
    m_progressDelay->setInterval(kProgressDelayMilliseconds);
    connect(m_progressDelay, &QTimer::timeout,
            this, &OperationTaskStrip::showDelayedProgress);

    connect(m_cancelButton, &QPushButton::clicked, this, [this] {
        if (!m_running || m_cancellationRequested) {
            return;
        }
        emit cancelRequested();
    });

    finish();
}

void OperationTaskStrip::begin(
    const QString& operationName,
    const QString& cancelAccessibleName) {
    m_operationName = operationName.trimmed();
    if (m_operationName.isEmpty()) {
        m_operationName = QStringLiteral("Background operation");
    }
    m_cancelAccessibleName = cancelAccessibleName.trimmed();
    m_running = true;
    m_cancellationRequested = false;
    m_elapsed.start();
    m_elapsedTimer->start();
    m_progressDelay->start();
    refreshPresentation();
    setVisible(true);
}

void OperationTaskStrip::setCancellationRequested() {
    if (!m_running || m_cancellationRequested) {
        return;
    }
    m_cancellationRequested = true;
    m_cancelButton->setEnabled(false);
    m_progressDelay->stop();
    refreshProgressVisibility();
    refreshPresentation();
}

void OperationTaskStrip::finish() {
    m_running = false;
    m_cancellationRequested = false;
    m_elapsed.invalidate();
    if (m_elapsedTimer) {
        m_elapsedTimer->stop();
    }
    if (m_progressDelay) {
        m_progressDelay->stop();
    }
    if (m_elapsedLabel) {
        m_elapsedLabel->clear();
        m_elapsedLabel->hide();
    }
    if (m_progress) {
        m_progress->hide();
    }
    if (m_cancelButton) {
        m_cancelButton->setText(QStringLiteral("Cancel"));
        m_cancelButton->setEnabled(true);
    }
    hide();
}

void OperationTaskStrip::setReducedMotion(bool reduced) {
    if (m_reducedMotion == reduced) {
        return;
    }
    m_reducedMotion = reduced;
    if (!m_running) {
        return;
    }
    if (m_reducedMotion) {
        m_progressDelay->stop();
        refreshProgressVisibility();
    } else if (m_cancellationRequested
               || (m_elapsed.isValid()
                   && m_elapsed.elapsed() >= kProgressDelayMilliseconds)) {
        refreshProgressVisibility();
    } else {
        m_progressDelay->start(
            kProgressDelayMilliseconds
            - static_cast<int>(m_elapsed.elapsed()));
    }
}

bool OperationTaskStrip::isRunning() const {
    return m_running;
}

bool OperationTaskStrip::cancellationRequested() const {
    return m_cancellationRequested;
}

void OperationTaskStrip::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event && event->type() == QEvent::FontChange) {
        refreshOperationText();
    }
}

void OperationTaskStrip::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refreshProgressVisibility();
}

void OperationTaskStrip::refreshPresentation() {
    m_stateLabel->setText(
        m_cancellationRequested
            ? QStringLiteral("Cancel requested")
            : QStringLiteral("Running"));

    m_cancelButton->setText(
        m_cancellationRequested
            ? QStringLiteral("Cancelling…")
            : QStringLiteral("Cancel"));
    m_cancelButton->setEnabled(!m_cancellationRequested);
    const QString cancelName = m_cancelAccessibleName.isEmpty()
        ? QStringLiteral("Cancel current operation")
        : m_cancelAccessibleName;
    m_cancelButton->setAccessibleName(cancelName);
    m_cancelButton->setAccessibleDescription(
        QStringLiteral(
            "Requests cooperative cancellation; no new result will be published."));

    refreshOperationText();
    refreshElapsedTime();
    setAccessibleName(
        QStringLiteral("%1: %2")
            .arg(m_stateLabel->text(), m_operationName));
    setAccessibleDescription(
        m_cancellationRequested
            ? QStringLiteral(
                  "Cancellation has been requested. Waiting for the running process to stop.")
            : QStringLiteral(
                  "The operation is running. A text Cancel button is available."));
}

void OperationTaskStrip::refreshElapsedTime() {
    if (!m_running || !m_elapsed.isValid()
        || m_elapsed.elapsed() < kElapsedRevealMilliseconds) {
        m_elapsedLabel->hide();
        return;
    }
    const qint64 elapsedSeconds = m_elapsed.elapsed() / 1'000;
    m_elapsedLabel->setText(
        QStringLiteral("%1 s elapsed").arg(elapsedSeconds));
    m_elapsedLabel->show();
}

void OperationTaskStrip::refreshOperationText() {
    if (!m_operationLabel) {
        return;
    }
    const int maximumWidth = emWidth(*m_operationLabel,
                                     kOperationLabelMaximumEms);
    m_operationLabel->setMaximumWidth(maximumWidth);
    static_cast<ElidingLabel*>(m_operationLabel)->setFullText(
        m_operationName);
    if (m_progress) {
        m_progress->setFixedWidth(emWidth(*m_progress, kProgressWidthEms));
    }
}

void OperationTaskStrip::showDelayedProgress() {
    // A coarse timer is allowed to fire slightly early.  If that happens,
    // preserve the intended delay instead of hiding the progress indicator
    // permanently after the one-shot timer has expired.
    if (m_running && !m_reducedMotion && !m_cancellationRequested
        && m_elapsed.isValid()
        && m_elapsed.elapsed() < kProgressDelayMilliseconds) {
        m_progressDelay->start(
            (std::max)(1,
                       kProgressDelayMilliseconds
                           - static_cast<int>(m_elapsed.elapsed())));
        return;
    }
    refreshProgressVisibility();
}

void OperationTaskStrip::refreshProgressVisibility() {
    if (!m_progress) {
        return;
    }
    const bool delayElapsed = m_cancellationRequested
        || (m_elapsed.isValid()
            && m_elapsed.elapsed() >= kProgressDelayMilliseconds);
    const bool hasPresentationRoom = width()
        >= emWidth(*this, kAnimatedProgressMinimumWidthEms);
    m_progress->setVisible(
        m_running && !m_reducedMotion && delayElapsed
        && hasPresentationRoom);
}

} // namespace finepaper::ui
