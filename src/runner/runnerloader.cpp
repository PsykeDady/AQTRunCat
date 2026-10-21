#include "runnerloader.h"

#include "framenames.h"

#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace aqtruncat {

QString sourceName(RunnerSource source)
{
    switch (source) {
    case RunnerSource::Bundled:
        return QStringLiteral("bundled");
    case RunnerSource::User:
        return QStringLiteral("user");
    }
    return {};
}

namespace {

constexpr int UpstreamHeight = 36;
constexpr int UpstreamMinWidth = 10;
constexpr int UpstreamMaxWidth = 100;

QStringList entries(const QDir &dir, QDir::Filters filters)
{
    return dir.entryList(filters | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

// Directory that actually holds the frames: the runner directory itself, or
// its single sub-directory with frames (Gallery folder with extracted ZIP).
std::optional<QDir> findFramesDirectory(const QDir &root, QString *error)
{
    if (!frameFileNames(entries(root, QDir::Files)).isEmpty())
        return root;

    QList<QDir> candidates;
    for (const QString &name : entries(root, QDir::Dirs)) {
        if (name.startsWith(u'.') || name == QLatin1String("__MACOSX"))
            continue;
        const QDir sub(root.filePath(name));
        if (!frameFileNames(entries(sub, QDir::Files)).isEmpty())
            candidates.append(sub);
    }
    if (candidates.size() == 1)
        return candidates.first();
    *error = candidates.isEmpty()
        ? QStringLiteral("no PNG frames found")
        : QStringLiteral("several sub-directories contain PNG frames; cannot tell which one is the runner");
    return std::nullopt;
}

void applyMetadata(const QString &path, Runner &runner, std::optional<bool> &monochrome,
                   QList<int> &frameOrder)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        runner.warnings.append(QStringLiteral("metadata.json unreadable, ignored"));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(64 * 1024), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        runner.warnings.append(QStringLiteral("metadata.json is not a valid JSON object, ignored"));
        return;
    }
    const QJsonObject meta = doc.object();

    if (const QJsonValue v = meta.value(QLatin1String("displayName")); v.isString() && !v.toString().trimmed().isEmpty())
        runner.displayName = v.toString().trimmed();
    else if (!v.isUndefined())
        runner.warnings.append(QStringLiteral("metadata displayName is not a non-empty string, ignored"));

    if (const QJsonValue v = meta.value(QLatin1String("author")); v.isString())
        runner.author = v.toString().trimmed();

    if (const QJsonValue v = meta.value(QLatin1String("type")); v.isString()) {
        if (v.toString() == QLatin1String("monochrome"))
            monochrome = true;
        else if (v.toString() == QLatin1String("color"))
            monochrome = false;
        else
            runner.warnings.append(QStringLiteral("metadata type \"%1\" unknown, detected from the frames instead").arg(v.toString()));
    }

    if (const QJsonValue v = meta.value(QLatin1String("frameOrder")); v.isArray()) {
        for (const QJsonValue &item : v.toArray()) {
            if (!item.isDouble() || item.toDouble() != static_cast<int>(item.toDouble())) {
                runner.warnings.append(QStringLiteral("metadata frameOrder contains a non-integer, ignored"));
                frameOrder.clear();
                return;
            }
            frameOrder.append(item.toInt());
        }
    }
}

void checkNumbering(const QStringList &files, Runner &runner)
{
    QList<long long> numbers;
    for (const QString &name : files) {
        const long long n = trailingFrameNumber(name);
        if (n < 0)
            return; // free-form names: order is the natural order, nothing to check
        numbers.append(n);
    }
    std::sort(numbers.begin(), numbers.end());
    for (qsizetype i = 1; i < numbers.size(); ++i) {
        if (numbers[i] == numbers[i - 1])
            runner.warnings.append(QStringLiteral("frame number %1 appears more than once").arg(numbers[i]));
        else if (numbers[i] != numbers[i - 1] + 1)
            runner.warnings.append(QStringLiteral("frame numbers jump from %1 to %2 (missing frames?)")
                                       .arg(numbers[i - 1]).arg(numbers[i]));
    }
}

} // namespace

QString displayNameFromId(const QString &id)
{
    QStringList words = QString(id).replace(u'_', u'-').split(u'-', Qt::SkipEmptyParts);
    for (QString &word : words)
        word[0] = word[0].toUpper();
    return words.isEmpty() ? id : words.join(u' ');
}

bool looksMonochrome(const QList<QImage> &frames)
{
    // A pixel is "coloured" when its channels differ by more than ChromaTolerance.
    // A runner is monochrome when less than 3 % of its visible pixels are
    // coloured: anti-aliased edges of black artwork can carry a slight tint.
    // Measured on the 43 Runner Gallery runners: monochrome ones ≤ 0.67 %,
    // colour ones ≥ 8.84 %.
    constexpr int ChromaTolerance = 24;
    constexpr double MaxColouredShare = 0.03;
    qint64 visible = 0;
    qint64 coloured = 0;
    for (const QImage &frame : frames) {
        const QImage image = frame.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QRgb p = line[x];
                if (qAlpha(p) == 0)
                    continue;
                ++visible;
                const int high = std::max({qRed(p), qGreen(p), qBlue(p)});
                const int low = std::min({qRed(p), qGreen(p), qBlue(p)});
                if (high - low > ChromaTolerance)
                    ++coloured;
            }
        }
    }
    return visible == 0 || coloured < MaxColouredShare * visible;
}

RunnerLoadResult loadRunner(const QString &directory, const QString &id, RunnerSource source)
{
    RunnerLoadResult result;
    const QDir root(directory);
    if (!root.exists()) {
        result.error = QStringLiteral("directory does not exist: %1").arg(directory);
        return result;
    }

    QString error;
    const std::optional<QDir> framesDir = findFramesDirectory(root, &error);
    if (!framesDir) {
        result.error = error;
        return result;
    }
    const QStringList files = frameFileNames(entries(*framesDir, QDir::Files));
    if (files.size() > RunnerMaxFrames) {
        result.error = QStringLiteral("%1 frames, more than the maximum of %2").arg(files.size()).arg(RunnerMaxFrames);
        return result;
    }

    Runner runner;
    runner.id = id;
    runner.displayName = displayNameFromId(id);
    runner.source = source;
    runner.path = framesDir->absolutePath();

    for (const QString &name : files) {
        QImageReader reader(framesDir->filePath(name), "png");
        reader.setDecideFormatFromContent(false);
        const QSize size = reader.size(); // from the header, before decoding
        if (!size.isValid() || reader.format() != "png") {
            result.error = QStringLiteral("%1 is not a readable PNG").arg(name);
            return result;
        }
        if (size.width() > RunnerMaxFrameSide || size.height() > RunnerMaxFrameSide) {
            result.error = QStringLiteral("%1 is %2×%3, larger than %4 px")
                               .arg(name).arg(size.width()).arg(size.height()).arg(RunnerMaxFrameSide);
            return result;
        }
        const QImage image = reader.read();
        if (image.isNull()) {
            result.error = QStringLiteral("%1 cannot be decoded: %2").arg(name, reader.errorString());
            return result;
        }
        if (!runner.frames.isEmpty() && image.size() != runner.frameSize) {
            result.error = QStringLiteral("%1 is %2×%3 but the first frame is %4×%5; all frames must have the same size")
                               .arg(name).arg(image.width()).arg(image.height())
                               .arg(runner.frameSize.width()).arg(runner.frameSize.height());
            return result;
        }
        runner.frameSize = image.size();
        runner.frames.append(image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
        runner.frameFiles.append(framesDir->absoluteFilePath(name));
    }

    checkNumbering(files, runner);
    if (runner.frames.size() == 1)
        runner.warnings.append(QStringLiteral("only one frame: the runner will not move"));
    if (runner.frameSize.height() != UpstreamHeight || runner.frameSize.width() < UpstreamMinWidth
        || runner.frameSize.width() > UpstreamMaxWidth)
        runner.warnings.append(QStringLiteral("frames are %1×%2; RunCat Neo runners are 10–100 × 36 px")
                                   .arg(runner.frameSize.width()).arg(runner.frameSize.height()));

    std::optional<bool> monochrome;
    QList<int> frameOrder;
    for (const QString &candidate : {root.filePath(QStringLiteral("metadata.json")),
                                     framesDir->filePath(QStringLiteral("metadata.json"))}) {
        if (QFile::exists(candidate)) {
            applyMetadata(candidate, runner, monochrome, frameOrder);
            break;
        }
    }

    const qsizetype count = runner.frames.size();
    const bool orderValid = !frameOrder.isEmpty()
        && std::all_of(frameOrder.cbegin(), frameOrder.cend(), [count](int i) { return i >= 0 && i < count; });
    if (!frameOrder.isEmpty() && !orderValid)
        runner.warnings.append(QStringLiteral("metadata frameOrder refers to missing frames, ignored"));
    if (orderValid) {
        runner.sequence = frameOrder;
    } else {
        for (int i = 0; i < count; ++i)
            runner.sequence.append(i);
    }

    runner.monochrome = monochrome.value_or(looksMonochrome(runner.frames));
    result.runner = std::move(runner);
    return result;
}

} // namespace aqtruncat
