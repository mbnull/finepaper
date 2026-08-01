#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QTimer;

namespace finepaper::ui {

// Compact, text-first feedback for one cancellable background operation.
// The caller owns the operation lifecycle; this widget only presents that
// lifecycle and emits a cooperative cancellation request.
class OperationTaskStrip final : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(OperationTaskStrip)

public:
    explicit OperationTaskStrip(QWidget* parent = nullptr);

    void begin(const QString& operationName,
               const QString& cancelAccessibleName = {});
    void setCancellationRequested();
    void finish();
    void setReducedMotion(bool reduced);

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool cancellationRequested() const;

signals:
    void cancelRequested();

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void refreshPresentation();
    void refreshElapsedTime();
    void refreshOperationText();
    void refreshProgressVisibility();
    void showDelayedProgress();

    QLabel* m_stateLabel = nullptr;
    QLabel* m_operationLabel = nullptr;
    QLabel* m_elapsedLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QTimer* m_elapsedTimer = nullptr;
    QTimer* m_progressDelay = nullptr;
    QElapsedTimer m_elapsed;
    QString m_operationName;
    QString m_cancelAccessibleName;
    bool m_running = false;
    bool m_cancellationRequested = false;
    bool m_reducedMotion = false;
};

} // namespace finepaper::ui
