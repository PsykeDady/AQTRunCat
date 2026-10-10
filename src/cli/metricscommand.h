#pragma once

// "aqtruncat metrics": presentation of SystemMetricsSnapshot. The metrics
// themselves come from the metrics layer (src/metrics), shared with future
// frontends (Dashboard, Metrics Bar).

#include "commandline.h"
#include "metrics/systemmetrics.h"

#include <QString>
#include <QTextStream>

namespace aqtruncat::cli {

// Multi-line report of one snapshot (sections CPU, Memory, Swap, Network,
// Storage, Disk I/O); unavailable values say why instead of showing 0.
QString formatMetricsReport(const metrics::SystemMetricsSnapshot &snapshot);

// One compact line per sample for --watch, prefixed with `clock` (hh:mm:ss).
QString formatMetricsLine(const metrics::SystemMetricsSnapshot &snapshot, const QString &clock);

// Samples twice ~1 s apart and prints the report, or with --watch prints one
// line per second until SIGINT/SIGTERM (exit 0).
int runMetrics(const Invocation &invocation, QTextStream &out, QTextStream &err,
               metrics::MetricsSources sources = {});

} // namespace aqtruncat::cli
