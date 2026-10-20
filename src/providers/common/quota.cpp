#include "quota.h"

#include <QLocale>

#include <cmath>

namespace aqtruncat::providers {

QuotaJudgement judgeQuota(const std::optional<QuotaValue> &newest, const QDateTime &now,
                          qint64 freshForSeconds, qint64 maximumAgeSeconds)
{
    QuotaJudgement judgement;
    judgement.value = newest;
    if (!newest)
        return judgement;
    const qint64 age = newest->observedAt.secsTo(now);
    if (newest->resetsAt.isValid() && newest->resetsAt <= now)
        judgement.presence = QuotaPresence::Expired;
    else if (age > maximumAgeSeconds)
        judgement.presence = QuotaPresence::TooOld;
    else
        judgement.presence = QuotaPresence::Current;
    judgement.stale = judgement.presence == QuotaPresence::Current && age > freshForSeconds;
    return judgement;
}

std::optional<QuotaValue> newerQuota(const std::optional<QuotaValue> &a, const std::optional<QuotaValue> &b)
{
    if (a && b)
        return b->observedAt >= a->observedAt ? b : a;
    return a ? a : b;
}

QString quotaPercentText(double percent)
{
    if (std::floor(percent) == percent)
        return QString::number(qint64(percent)) + u'%';
    return QString::number(percent, 'f', 1) + u'%';
}

QString quotaCompactText(double percent)
{
    if (percent > 0.0 && percent < 1.0)
        return QStringLiteral("<1%");
    return QString::number(qRound64(percent)) + u'%';
}

QString formatMoment(const QDateTime &moment, const QDateTime &now, const QTimeZone &zone)
{
    const QDateTime local = moment.toTimeZone(zone);
    const QString time = local.time().toString(QStringLiteral("HH:mm"));
    if (local.date() == now.toTimeZone(zone).date())
        return time;
    return QLocale::c().toString(local.date(), QStringLiteral("MMM d")) + QStringLiteral(", ") + time;
}

QJsonObject quotaItem(const QuotaItemSpec &spec, const QuotaJudgement &judgement, const QDateTime &now,
                      const QTimeZone &zone)
{
    QJsonObject object{{QStringLiteral("id"), spec.id}, {QStringLiteral("title"), spec.title},
                       {QStringLiteral("metricsBarLabel"), spec.barLabel}};
    const auto moment = [&](const QDateTime &time) { return formatMoment(time, now, zone); };
    switch (judgement.presence) {
    case QuotaPresence::Current: {
        const QuotaValue &value = *judgement.value;
        QString text = QStringLiteral("%1 used").arg(quotaPercentText(value.usedPercent));
        if (value.usedPercent >= 100.0)
            text += QStringLiteral(" — limit reached");
        QStringList detail;
        if (spec.showRemaining)
            detail << QStringLiteral("%1 left").arg(quotaPercentText(std::max(0.0, 100.0 - value.usedPercent)));
        if (value.resetsAt.isValid()) {
            const QString when = moment(value.resetsAt);
            detail << (when.contains(u',') ? QStringLiteral("Resets %1").arg(when) : QStringLiteral("Resets at %1").arg(when));
        } else {
            detail << QStringLiteral("Reset time not reported");
        }
        if (judgement.stale)
            detail << QStringLiteral("as of %1").arg(moment(value.observedAt));
        object.insert(QStringLiteral("formattedValue"), text);
        object.insert(QStringLiteral("normalizedValue"), value.usedPercent / 100.0);
        object.insert(QStringLiteral("detail"), detail.join(QStringLiteral(" · ")));
        object.insert(QStringLiteral("metricsBarValue"), quotaCompactText(value.usedPercent));
        break;
    }
    case QuotaPresence::Expired:
        object.insert(QStringLiteral("formattedValue"), QStringLiteral("Out of date"));
        object.insert(QStringLiteral("warning"), true);
        object.insert(QStringLiteral("detail"), QStringLiteral("The window reset at %1; no newer report yet")
                                                    .arg(moment(judgement.value->resetsAt)));
        break;
    case QuotaPresence::TooOld:
        object.insert(QStringLiteral("formattedValue"), QStringLiteral("Out of date"));
        object.insert(QStringLiteral("warning"), true);
        object.insert(QStringLiteral("detail"), QStringLiteral("The last report (%1) is too old to show")
                                                    .arg(moment(judgement.value->observedAt)));
        break;
    case QuotaPresence::Missing:
        object.insert(QStringLiteral("formattedValue"), QStringLiteral("Not reported"));
        object.insert(QStringLiteral("warning"), true);
        object.insert(QStringLiteral("detail"), spec.missingHint);
        break;
    }
    return object;
}

} // namespace aqtruncat::providers
