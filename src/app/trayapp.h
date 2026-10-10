#pragma once

#include "runner/runnerservice.h"

class QApplication;

struct RunOptions {
    QString runnerId;  // empty: default runner
    int fixedFps = 0;  // 0: CPU driven
    bool verbose = false;
    QString defaultRunnerId;
};

// "aqtruncat run": the tray application. Returns the process exit status.
//   MetricSampler raw CPU → CpuMonitor (EMA) → cpuToFps →
//   FpsStabilizer → RunnerAnimator (frame timing) → TrayPresenter.
// The same MetricSampler snapshot independently updates DashboardWindow;
// CustomMetricsService separately watches producer-owned JSON snapshots.
int runTrayApplication(QApplication &app, const aqtruncat::RunnerService &runners, const RunOptions &options);
