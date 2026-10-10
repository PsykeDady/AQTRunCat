#pragma once

#include "runner/runner.h"
#include "runner/runneranimator.h"

#include <QLabel>
#include <QWidget>

class RunnerPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    static constexpr int PreviewFps = 20;

    explicit RunnerPreviewWidget(QWidget *parent = nullptr);

    void setRunner(const aqtruncat::Runner &runner);
    void clearRunner();
    QString runnerId() const { return m_runnerId; }
    int currentFrame() const { return m_currentFrame; }
    QSize renderedPixmapSize() const;

signals:
    void framePresented(int frameIndex);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void rebuildFrames();
    void showFrame(int frameIndex);
    void updatePixmap();

    QLabel *m_view = nullptr;
    aqtruncat::RunnerAnimator m_animator;
    QString m_runnerId;
    QList<QImage> m_sourceFrames;
    QList<QImage> m_presentedFrames;
    QList<int> m_sequence;
    bool m_monochrome = true;
    int m_currentFrame = -1;
};

