#include "runnerpreviewwidget.h"

#include "runnerpresentation.h"

#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

RunnerPreviewWidget::RunnerPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("runnerPreview"));
    setAccessibleName(tr("Runner preview"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    m_view = new QLabel(tr("Preview unavailable"), this);
    m_view->setObjectName(QStringLiteral("runnerPreviewImage"));
    m_view->setAlignment(Qt::AlignCenter);
    m_view->setMinimumHeight(90);
    layout->addWidget(m_view);
    connect(&m_animator, &aqtruncat::RunnerAnimator::frameChanged,
            this, &RunnerPreviewWidget::showFrame);
    m_animator.setPaused(true);
    m_animator.setFps(PreviewFps);
}

void RunnerPreviewWidget::setRunner(const aqtruncat::Runner &runner)
{
    m_runnerId = runner.id;
    m_sourceFrames = runner.frames;
    m_sequence = runner.sequence;
    m_monochrome = runner.monochrome;
    rebuildFrames();
    m_animator.setSequence(m_sequence);
    m_animator.setPaused(!isVisible());
}

void RunnerPreviewWidget::clearRunner()
{
    m_animator.setPaused(true);
    m_runnerId.clear();
    m_sourceFrames.clear();
    m_presentedFrames.clear();
    m_sequence.clear();
    m_currentFrame = -1;
    m_view->setPixmap(QPixmap());
    m_view->setText(tr("Preview unavailable"));
}

QSize RunnerPreviewWidget::renderedPixmapSize() const
{
    return m_view->pixmap().size();
}

void RunnerPreviewWidget::rebuildFrames()
{
    const QColor colour = palette().color(QPalette::Active, QPalette::WindowText);
    m_presentedFrames.clear();
    for (const QImage &frame : std::as_const(m_sourceFrames))
        m_presentedFrames.append(presentRunnerFrame(frame, m_monochrome, colour));
    updatePixmap();
}

void RunnerPreviewWidget::showFrame(int frameIndex)
{
    if (frameIndex < 0 || frameIndex >= m_presentedFrames.size())
        return;
    m_currentFrame = frameIndex;
    updatePixmap();
    emit framePresented(frameIndex);
}

void RunnerPreviewWidget::updatePixmap()
{
    if (m_currentFrame < 0 || m_currentFrame >= m_presentedFrames.size())
        return;
    m_view->clear();
    const QSize available = m_view->contentsRect().size();
    const QPixmap pixmap = QPixmap::fromImage(m_presentedFrames[m_currentFrame])
                                .scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_view->setPixmap(pixmap);
}

void RunnerPreviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePixmap();
}

void RunnerPreviewWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_sourceFrames.isEmpty())
        m_animator.setPaused(false);
}

void RunnerPreviewWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_animator.setPaused(true);
}

void RunnerPreviewWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        rebuildFrames();
}

