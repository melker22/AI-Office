#include "core/RtfReader.h"

#include "core/RtfLang.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QTimeZone>
#include <QVector>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace aioffice {
namespace {

// External RTF is hostile (L1). These caps are the rejection points, not hints.
constexpr int kMaxDepth = 64;
constexpr qint64 kMaxBin = 8 * 1024 * 1024;
constexpr qint64 kMaxPict = 8 * 1024 * 1024;
constexpr qint64 kMaxFile = 32 * 1024 * 1024;
constexpr int kMaxBlocks = 100000;
constexpr int kMaxCells = 256;
constexpr int kMaxRows = 5000;
constexpr int kMaxUc = 32;

enum class Kind {
    Group,
    Rtf,
    FontTbl,
    FontEntry,
    ColorTbl,
    StyleSheet,
    StyleEntry,
    Info,
    InfoField,
    Upr,
    Ud,
    ListTable,
    List,
    ListLevel,
    ListOverrideTable,
    ListOverride,
    Field,
    Pict,
    PicProp,
    PropName,
    PropValue,
    Footnote,
    BkmkStart,
    BkmkEnd,
    AioListKind,
    AioCheck,
    RevTbl,
    RevAuthor,
    Annotation,
    AtnAuthor,
    AtnId,
    AnnProp,
    AioRevFmt,
    Shp,
    ShpInst,
    ShpTxt,
    Sp,
    SpName,
    SpVal
};

enum class Sink {
    Body,
    Note,
    Inst,
    Result,
    Discard,
    Pict,
    FontName,
    StyleName,
    InfoText,
    Bkmk,
    RevName,
    AnnText,
    ShpTxt
};

enum class InfoKind { None, Title, Author, Subject, Keywords };

struct CharState {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool super = false;
    bool sub = false;
    bool ab = false;
    bool ai = false;
    int font = -1;
    bool fsSet = false;
    int fs = 24;
    int cf = 0;
    int highlight = 0;
    int lang = 0;
    int uc = 1;
    bool hidden = false;
    bool revised = false;
    bool deleted = false;
    int revauth = -1;
    int revauthdel = -1;
    int crauth = -1;
    bool revdttmSet = false;
    qint64 revdttm = 0;
    bool revdttmBad = false;
    bool revdttmdelSet = false;
    qint64 revdttmdel = 0;
    bool revdttmdelBad = false;
    bool crdateSet = false;
    qint64 crdate = 0;
    bool crdateBad = false;
};

struct ParaState {
    QString align = QStringLiteral("left");
    QString dir;
    int li = 0;
    int ri = 0;
    int fi = 0;
    int sb = 0;
    int sa = 0;
    bool sbSet = false;
    bool saSet = false;
    int sl = 0;
    bool slSet = false;
    bool slmult = false;
    int styleIndex = -1;
    int ls = 0;
    int ilvl = 0;
    bool inTable = false;
    int outline = -1;
    bool keepn = false;
    bool pagebb = false;
    // Private {\*\aiolistkind}/{\*\aiocheck} hints. They survive group close
    // by bubbling to the frame that owns the following \par.
    QString listKindOverride;
    bool checkSet = false;
    bool checked = false;
};

struct Frame {
    Kind kind = Kind::Group;
    Sink sink = Sink::Body;
    bool skipAll = false;
    bool expectDest = true;
    bool nameDone = false;
    InfoKind info = InfoKind::None;
    QString tag;
    int fontIndex = 0;
    int styleIndex = -1;
    CharState ch;
    ParaState para;
    QString buf;
};

struct ColorSlot {
    bool valid = false;
    int r = 0;
    int g = 0;
    int b = 0;
};

struct StyleInfo {
    QString name;
    int outline = -1;
};

struct ListLevel {
    int nfc = 255;
};

struct ListDef {
    int id = -1;
    QVector<ListLevel> levels;
    // {\*\aiolistkind checkbox|task} on the list definition (writer), not on the item.
    QString kindOverride;
};

struct OverrideBuild {
    int ls = -1;
    int listId = -1;
};

struct Fmt {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool super = false;
    bool sub = false;
    QString font;
    QString color;
    QString highlight;
    QString lang;
    double sizePt = 0;
    bool hidden = false;
    bool redact = false;

    bool matches(const TextRun &r) const
    {
        return r.field.isEmpty() && r.noteId.isEmpty() && r.link.isEmpty() && r.bold == bold && r.italic == italic
            && r.underline == underline && r.strike == strike && r.superscript == super && r.subscript == sub
            && r.font == font && r.color == color && r.highlight == highlight && r.lang == lang && r.sizePt == sizePt
            && r.hidden == hidden && r.redact == redact;
    }

    void apply(TextRun &r) const
    {
        r.bold = bold;
        r.italic = italic;
        r.underline = underline;
        r.strike = strike;
        r.superscript = super;
        r.subscript = sub;
        r.font = font;
        r.color = color;
        r.highlight = highlight;
        r.lang = lang;
        r.sizePt = sizePt;
        r.hidden = hidden;
        r.redact = redact;
    }
};

struct FieldState {
    QString inst;
    QVector<TextRun> result;
};

struct PendingNote {
    QString tempId;
    QVector<TextRun> runs;
    bool endnote = false;
    QVector<Revision> revs;
};

struct UprState {
    Sink dest = Sink::Body;
    bool sawUd = false;
    QVector<TextRun> runs;
    QString inst;
};

void skipFieldWs(const QString &s, int &i)
{
    while (i < s.size() && s.at(i).isSpace())
        ++i;
}

// Unescape \\ \{ \} \" and drop control characters. Used on one field token,
// not on a whole instruction, so a quoted span can still see \" as an escape.
QString unescapeFieldPiece(const QString &in)
{
    QString out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i) {
        const QChar c = in.at(i);
        if (c == QLatin1Char('\\') && i + 1 < in.size()) {
            const QChar n = in.at(i + 1);
            if (n == QLatin1Char('\\') || n == QLatin1Char('{') || n == QLatin1Char('}')
                || n == QLatin1Char('"')) {
                out.append(n);
                ++i;
                continue;
            }
        }
        if (c.category() == QChar::Other_Control)
            continue;
        out.append(c);
    }
    return out;
}

QString readFieldArg(const QString &s, int &i)
{
    skipFieldWs(s, i);
    if (i >= s.size())
        return {};
    if (s.at(i) == QLatin1Char('"')) {
        ++i;
        QString out;
        while (i < s.size()) {
            const QChar c = s.at(i);
            if (c == QLatin1Char('\\') && i + 1 < s.size()) {
                const QChar n = s.at(i + 1);
                if (n == QLatin1Char('\\') || n == QLatin1Char('{') || n == QLatin1Char('}')
                    || n == QLatin1Char('"')) {
                    out.append(n);
                    i += 2;
                    continue;
                }
            }
            if (c.category() == QChar::Other_Control) {
                ++i;
                continue;
            }
            if (c == QLatin1Char('"')) {
                ++i;
                break;
            }
            out.append(c);
            ++i;
        }
        return out;
    }
    const int start = i;
    while (i < s.size() && !s.at(i).isSpace())
        ++i;
    return unescapeFieldPiece(s.mid(start, i - start));
}

// Switches before the URL are not the URL. \l, \o, and \t consume the next
// argument. \m and \n do not. A HYPERLINK that only has \l is the local target.
QString hyperlinkTarget(const QString &s, int i)
{
    QString url;
    QString location;
    bool sawUrl = false;
    while (i < s.size()) {
        skipFieldWs(s, i);
        if (i >= s.size())
            break;
        if (s.at(i) == QLatin1Char('\\') && i + 1 < s.size()) {
            const QChar sw = s.at(i + 1).toLower();
            const bool withArg = sw == QLatin1Char('l') || sw == QLatin1Char('o') || sw == QLatin1Char('t');
            const bool bare = sw == QLatin1Char('m') || sw == QLatin1Char('n');
            if (withArg || bare) {
                i += 2;
                if (withArg) {
                    const QString arg = readFieldArg(s, i);
                    if (sw == QLatin1Char('l'))
                        location = arg;
                }
                continue;
            }
        }
        const QString tok = readFieldArg(s, i);
        if (!sawUrl) {
            url = tok;
            sawUrl = true;
        }
    }
    if (location.isEmpty())
        return url;
    // \l with a URL is a location inside that target: url#location.
    return url + QLatin1Char('#') + location;
}

QString readFieldToken(const QString &s, int &i)
{
    skipFieldWs(s, i);
    const int start = i;
    while (i < s.size() && !s.at(i).isSpace())
        ++i;
    return s.mid(start, i - start);
}

QString seqFieldArg(const QString &name, const QString &format)
{
    QJsonObject o;
    o.insert(QStringLiteral("format"), format);
    o.insert(QStringLiteral("name"), name);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

struct PendingBookmark {
    QString name;
    int offset = 0;
};

int hexVal(unsigned char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool isAlpha(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isDigit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

double twipsToMm(int twips)
{
    return std::round(static_cast<double>(twips) * 25.4 / 1440.0 * 100.0) / 100.0;
}

// MS-DOC DTTM. The value is a 32-bit bitfield written as a decimal integer.
// Weekday bits 29–31 can push it above INT_MAX, so callers must not use fitsInt.
bool dttmToUnixMs(qint64 raw, qint64 *outMs)
{
    if (raw < 0 || raw > 0xFFFFFFFFLL)
        return false;
    const quint32 bits = static_cast<quint32>(raw);
    const int minute = static_cast<int>(bits & 0x3Fu);
    const int hour = static_cast<int>((bits >> 6) & 0x1Fu);
    const int day = static_cast<int>((bits >> 11) & 0x1Fu);
    const int month = static_cast<int>((bits >> 16) & 0x0Fu);
    const int year = static_cast<int>((bits >> 20) & 0x1FFu) + 1900;
    if (minute > 59 || hour > 23 || month < 1 || month > 12 || day < 1 || day > 31)
        return false;
    const QDate date(year, month, day);
    if (!date.isValid())
        return false;
    const QDateTime dt(date, QTime(hour, minute, 0), QTimeZone::utc());
    if (!dt.isValid())
        return false;
    if (outMs)
        *outMs = dt.toMSecsSinceEpoch();
    return true;
}

QString colorRefHex(qint64 raw)
{
    if (raw < 0)
        raw = 0;
    const quint32 c = static_cast<quint32>(raw & 0xFFFFFFFFLL);
    const int r = static_cast<int>(c & 0xFFu);
    const int g = static_cast<int>((c >> 8) & 0xFFu);
    const int b = static_cast<int>((c >> 16) & 0xFFu);
    return QStringLiteral("#%1%2%3")
        .arg(r, 2, 16, QLatin1Char('0'))
        .arg(g, 2, 16, QLatin1Char('0'))
        .arg(b, 2, 16, QLatin1Char('0'))
        .toUpper();
}

enum class RevKind { None, Insert, Delete, Format };

struct RevMark {
    RevKind kind = RevKind::None;
    int author = 0;
    bool dateSet = false;
    qint64 dateRaw = 0;
    bool dateBad = false;
};

struct ShapeBuild {
    bool typeSet = false;
    int shapeType = -1;
    bool leftSet = false;
    bool topSet = false;
    bool rightSet = false;
    bool bottomSet = false;
    qint64 left = 0;
    qint64 top = 0;
    qint64 right = 0;
    qint64 bottom = 0;
    bool zSet = false;
    int z = 0;
    bool fillColorSet = false;
    qint64 fillColor = 0;
    bool lineColorSet = false;
    qint64 lineColor = 0;
    int fFilled = -1;
    int fLine = -1;
    QVector<TextRun> runs;
    QString sn;
};

int runChars(const QVector<TextRun> &runs)
{
    int n = 0;
    for (const TextRun &r : runs)
        n += static_cast<int>(r.text.size());
    return n;
}

// LCIDs that are not in the shared D1 table. 1025/1037/1041/1042/1049 stay out
// so the short tags (ar, he, ja, ko, ru) win. 1034 is the traditional Spanish
// sort LCID and still means es-ES.
QString legacyLcidTag(int id)
{
    switch (id) {
    case 1026: return QStringLiteral("bg-BG");
    case 1027: return QStringLiteral("ca-ES");
    case 1029: return QStringLiteral("cs-CZ");
    case 1030: return QStringLiteral("da-DK");
    case 1032: return QStringLiteral("el-GR");
    case 1034: return QStringLiteral("es-ES");
    case 1035: return QStringLiteral("fi-FI");
    case 1038: return QStringLiteral("hu-HU");
    case 1043: return QStringLiteral("nl-NL");
    case 1044: return QStringLiteral("nb-NO");
    case 1045: return QStringLiteral("pl-PL");
    case 1048: return QStringLiteral("ro-RO");
    case 1050: return QStringLiteral("hr-HR");
    case 1053: return QStringLiteral("sv-SE");
    case 1055: return QStringLiteral("tr-TR");
    case 1057: return QStringLiteral("id-ID");
    case 1058: return QStringLiteral("uk-UA");
    case 1066: return QStringLiteral("vi-VN");
    case 1081: return QStringLiteral("hi-IN");
    default: return {};
    }
}

// LibreOffice writes spaces in style names as _20_ (and other ASCII as _HH_).
QString decodeStyleName(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (int i = 0; i < name.size();) {
        if (name.at(i) == QLatin1Char('_') && i + 3 < name.size() && name.at(i + 3) == QLatin1Char('_')) {
            bool ok = false;
            const int v = name.mid(i + 1, 2).toInt(&ok, 16);
            if (ok && v >= 0x20 && v <= 0x7e) {
                out.append(QChar(v));
                i += 4;
                continue;
            }
        }
        out.append(name.at(i));
        ++i;
    }
    return out.trimmed();
}

int headingLevelOf(const QString &name)
{
    const QString n = decodeStyleName(name).toLower();
    static const QRegularExpression re(QStringLiteral("^(?:heading|t[ií]tulo|titulo)\\s*(\\d+)$"));
    const auto m = re.match(n);
    if (m.hasMatch()) {
        const int lv = m.captured(1).toInt();
        return lv > 0 ? lv : 1;
    }
    if (n == QLatin1String("heading") || n == QLatin1String("title") || n == QLatin1String("título")
        || n == QLatin1String("titulo"))
        return 1;
    return 0;
}

const char *codePageName(int cpg)
{
    switch (cpg) {
    case 437: return "IBM437";
    case 850: return "IBM850";
    case 1250: return "windows-1250";
    case 1251: return "windows-1251";
    case 1252: return "windows-1252";
    case 1253: return "windows-1253";
    case 1254: return "windows-1254";
    case 1255: return "windows-1255";
    case 1256: return "windows-1256";
    case 1257: return "windows-1257";
    case 1258: return "windows-1258";
    case 10000: return "macintosh";
    case 28591: return "ISO-8859-1";
    case 28605: return "ISO-8859-15";
    case 65001: return "UTF-8";
    default: return nullptr;
    }
}

class RtfParser {
public:
    bool parse(const QByteArray &data, Document &out, QString *error, QStringList *warnings)
    {
        data_ = data;
        n_ = data_.size();
        i_ = 0;
        doc_ = Document::blank(QStringLiteral("Sem título"));
        doc_.body.clear();
        if (!setCodePage(1252))
            return finish(out, error);
        while (i_ < n_ && (data_[i_] == ' ' || data_[i_] == '\t' || data_[i_] == '\r' || data_[i_] == '\n'))
            ++i_;
        if (i_ >= n_ || static_cast<unsigned char>(data_[i_]) == 0xEF)
            skipBom();
        if (i_ >= n_ || data_[i_] != '{')
            return fail(QStringLiteral("Malformed RTF: missing {\\rtf header"), out, error);
        while (i_ < n_ && error_.isEmpty()) {
            const unsigned char c = static_cast<unsigned char>(data_[i_]);
            if (c == '{')
                beginGroup();
            else if (c == '}')
                endGroup();
            else if (c == '\\')
                parseControl();
            else if (c == '\r' || c == '\n' || c == '\0')
                ++i_;
            else if (c == ';' && takeSemicolon())
                ;
            else
                parseLiteral(c);
        }
        if (!error_.isEmpty())
            return finish(out, error);
        // Some writers close every inner group and omit the final brace of {\rtf1.
        // A file that ends in the middle of the root (no trailing '}') is still malformed.
        if (omittedRootClose()) {
            finishDocumentBody();
            if (!error_.isEmpty())
                return finish(out, error);
            stack_.clear();
        }
        if (!stack_.isEmpty())
            return fail(QStringLiteral("Malformed RTF: unclosed group"), out, error);
        if (ucSkip_ > 0)
            return fail(QStringLiteral("Malformed RTF: truncated \\u fallback"), out, error);
        if (!sawRtf_)
            return fail(QStringLiteral("Malformed RTF: missing {\\rtf header"), out, error);
        flushAnsi();
        if (!error_.isEmpty())
            return finish(out, error);
        if (doc_.body.isEmpty()) {
            Block p;
            p.type = QStringLiteral("paragraph");
            p.style = QStringLiteral("Normal");
            p.align = QStringLiteral("left");
            doc_.assignId(p);
            doc_.body.push_back(p);
        }
        applyPage();
        resolveCommentParents();
        if (warnings)
            *warnings = warnings_;
        out = doc_;
        return true;
    }

private:
    QByteArray data_;
    qsizetype i_ = 0;
    qsizetype n_ = 0;
    QString error_;
    Document doc_;
    QVector<Frame> stack_;
    QByteArray ansi_;
    QByteArray codec_ = QByteArray("windows-1252");
    int codePage_ = 1252;
    int ucSkip_ = 0;
    bool sawRtf_ = false;
    bool starNext_ = false;
    bool suppressEmptyPar_ = false;
    int looseBkmkKind_ = 0; // 1=start, 2=end (legacy ungrouped \bkmkstart/\bkmkend)

    QHash<int, QString> fonts_;
    int deff_ = 0;
    QVector<ColorSlot> colors_;
    int cr_ = -1;
    int cg_ = -1;
    int cb_ = -1;
    bool colorSeen_ = false;
    QHash<int, StyleInfo> styles_;
    QVector<ListDef> listStack_;
    QVector<ListLevel> levelStack_;
    QVector<OverrideBuild> ovStack_;
    QHash<int, ListDef> lists_;
    QHash<int, int> overrides_;

    QVector<TextRun> bodyRuns_;
    QVector<QVector<TextRun>> noteStack_;
    QVector<bool> noteEnd_;
    QVector<FieldState> fields_;
    QVector<PendingNote> pendingNotes_;
    QVector<UprState> upr_;
    QStringList warnings_;
    QVector<PendingBookmark> pendingBms_;
    int noteSerial_ = 0;
    QHash<QString, int> idNext_;
    // First body index that may still hold an unresolved note marker.
    // Advanced only when pendingNotes_ is empty, so a nested note whose
    // marker landed inside a note block added during this flush stays visible.
    int noteScanFrom_ = 0;

    QByteArray pict_;
    bool pictActive_ = false;
    bool pictPng_ = false;
    bool pictJpeg_ = false;
    bool pictHi_ = false;
    int pictNibble_ = 0;
    int pictGoal_ = 0;
    QString pictSn_;
    QString pictAlt_;
    QString pictName_;

    QVector<int> cellX_;
    int trLeft_ = 0;
    bool rowHeader_ = false;
    QVector<TableCell> rowCells_;
    Block table_;
    bool tableOpen_ = false;

    int paperW_ = 0;
    int paperH_ = 0;
    int margL_ = -1;
    int margR_ = -1;
    int margT_ = -1;
    int margB_ = -1;

    enum class AnnWhere { None, Para, Table };

    struct OpenRev {
        bool open = false;
        RevKind kind = RevKind::None;
        int author = 0;
        bool dateSet = false;
        qint64 dateRaw = 0;
        bool dateBad = false;
        int start = 0;
        QVector<TextRun> deletedRuns;
        QString payload;
        QVector<TextRun> priorRuns;
        bool tookFmt = false;
    };

    QStringList revAuthors_;
    // One buffer for the host paragraph and one per open footnote. `side` tracks
    // a format change that covers the same live characters as an insert: markOf
    // can return only one kind, and a delete point between two slices of the
    // same change must not split it (the slices abut in the live text).
    struct RevBuf {
        OpenRev open;
        OpenRev side;
        QVector<Revision> revs;
        bool pendingFmtSet = false;
        QString pendingPayload;
        QVector<TextRun> pendingPrior;
    };
    RevBuf paraRev_;
    QVector<RevBuf> noteRevStack_;

    QVector<Comment> paraComments_;
    QVector<Comment> tableComments_;
    QString pendingAuthor_;
    QString pendingInitials_;
    bool annActive_ = false;
    AnnWhere annWhere_ = AnnWhere::None;
    int annStub_ = -1;
    QString annText_;
    QString annId_;
    QString annParent_;
    QString annAuthor_;
    bool annResolved_ = false;
    bool annResolvedSet_ = false;
    bool paraCarrier_ = false;

    QVector<ShapeBuild> shapeStack_;
    bool warnedCellRev_ = false;
    bool warnedBadDate_ = false;
    bool warnedBadJson_ = false;
    bool warnedShapeSize_ = false;
    bool warnedCommentParent_ = false;
    bool warnedCarrier_ = false;
    bool warnedShpGrp_ = false;
    bool warnedShapeInTable_ = false;
    QSet<int> warnedAuthorIdx_;
    QSet<QString> warnedShapeType_;

    bool fail(const QString &message, Document &out, QString *error)
    {
        Q_UNUSED(out);
        if (error_.isEmpty())
            error_ = message;
        if (error)
            *error = error_;
        return false;
    }

    bool fail(const QString &message)
    {
        if (error_.isEmpty())
            error_ = message;
        return false;
    }

    bool finish(Document &out, QString *error) const
    {
        Q_UNUSED(out);
        if (error)
            *error = error_;
        return false;
    }

    bool setCodePage(int cpg)
    {
        const char *name = codePageName(cpg);
        if (!name) {
            fail(QStringLiteral("RTF code page not supported: %1").arg(cpg));
            return false;
        }
        QStringDecoder probe(name);
        if (!probe.isValid()) {
            fail(QStringLiteral("RTF code page not supported: %1").arg(cpg));
            return false;
        }
        codec_ = name;
        codePage_ = cpg;
        return true;
    }

    void skipBom()
    {
        if (i_ + 2 < n_ && static_cast<unsigned char>(data_[i_]) == 0xEF
            && static_cast<unsigned char>(data_[i_ + 1]) == 0xBB
            && static_cast<unsigned char>(data_[i_ + 2]) == 0xBF)
            i_ += 3;
        while (i_ < n_ && (data_[i_] == ' ' || data_[i_] == '\t' || data_[i_] == '\r' || data_[i_] == '\n'))
            ++i_;
    }

    bool omittedRootClose() const
    {
        if (stack_.size() != 1 || !sawRtf_)
            return false;
        qsizetype j = n_;
        while (j > 0) {
            const unsigned char c = static_cast<unsigned char>(data_[j - 1]);
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                break;
            --j;
        }
        return j > 0 && data_[j - 1] == '}';
    }

    // True when the current group contains the control word before its matching close.
    bool groupContainsWord(const char *word) const
    {
        const int n = static_cast<int>(std::strlen(word));
        int depth = 1;
        qsizetype j = i_;
        while (j < n_ && depth > 0) {
            const unsigned char c = static_cast<unsigned char>(data_[j]);
            if (c == '{') {
                ++depth;
                ++j;
                continue;
            }
            if (c == '}') {
                --depth;
                ++j;
                continue;
            }
            if (c == '\\' && j + 1 < n_) {
                const unsigned char nch = static_cast<unsigned char>(data_[j + 1]);
                if (nch == '\\' || nch == '{' || nch == '}') {
                    j += 2;
                    continue;
                }
                if (nch == '\'') {
                    j += (j + 3 < n_) ? 4 : (n_ - j);
                    continue;
                }
                if (!isAlpha(nch)) {
                    j += 2;
                    continue;
                }
                const qsizetype start = j + 1;
                j = start;
                while (j < n_ && isAlpha(static_cast<unsigned char>(data_[j])))
                    ++j;
                if (j - start == n && std::memcmp(data_.constData() + start, word, static_cast<size_t>(n)) == 0)
                    return true;
                if (j < n_ && data_[j] == '-')
                    ++j;
                while (j < n_ && isDigit(static_cast<unsigned char>(data_[j])))
                    ++j;
                if (j < n_ && data_[j] == ' ')
                    ++j;
                continue;
            }
            ++j;
        }
        return false;
    }

    bool inside(Kind k) const
    {
        for (const Frame &f : stack_) {
            if (f.kind == k)
                return true;
        }
        return false;
    }

    Fmt fmtOf(const CharState &ch) const
    {
        Fmt f;
        f.bold = ch.bold || ch.ab;
        f.italic = ch.italic || ch.ai;
        f.underline = ch.underline;
        f.strike = ch.strike;
        f.super = ch.super;
        f.sub = ch.sub;
        f.hidden = ch.hidden;
        if (ch.font >= 0 && ch.font != deff_ && fonts_.contains(ch.font))
            f.font = fonts_.value(ch.font);
        if (ch.fsSet && ch.fs > 0 && ch.fs != 24)
            f.sizePt = ch.fs / 2.0;
        if (ch.cf > 0)
            f.color = colorAt(ch.cf);
        if (ch.highlight > 0)
            f.highlight = colorAt(ch.highlight);
        if (ch.lang > 0 && ch.lang != 1024) {
            f.lang = rtfTagForLcid(ch.lang);
            if (f.lang.isEmpty())
                f.lang = legacyLcidTag(ch.lang);
        }
        return f;
    }

    QString colorAt(int idx) const
    {
        if (idx < 0 || idx >= colors_.size() || !colors_.at(idx).valid)
            return {};
        const ColorSlot &c = colors_.at(idx);
        return QStringLiteral("#%1%2%3")
            .arg(c.r, 2, 16, QLatin1Char('0'))
            .arg(c.g, 2, 16, QLatin1Char('0'))
            .arg(c.b, 2, 16, QLatin1Char('0'))
            .toUpper();
    }

    void appendRun(QVector<TextRun> &runs, const QString &text, const Fmt &fmt)
    {
        if (text.isEmpty())
            return;
        if (!runs.isEmpty() && fmt.matches(runs.last())) {
            runs.last().text += text;
            return;
        }
        TextRun r;
        fmt.apply(r);
        r.text = text;
        runs.push_back(r);
    }

    void appendChar(QChar ch)
    {
        if (ucSkip_ > 0) {
            --ucSkip_;
            return;
        }
        if (stack_.isEmpty())
            return;
        const Frame &f = stack_.last();
        if (f.skipAll || !sawRtf_)
            return;
        const Fmt fmt = fmtOf(f.ch);
        switch (f.sink) {
        case Sink::Body: {
            TextRun piece;
            fmt.apply(piece);
            piece.text = QString(ch);
            appendBodyRun(piece);
            break;
        }
        case Sink::RevName:
            if (!stack_.last().nameDone)
                stack_.last().buf.append(ch);
            break;
        case Sink::AnnText:
            annText_.append(ch);
            break;
        case Sink::ShpTxt:
            if (!shapeStack_.isEmpty())
                appendRun(shapeStack_.last().runs, QString(ch), fmt);
            break;
        case Sink::Note:
            if (!noteStack_.isEmpty() && !noteRevStack_.isEmpty()) {
                TextRun piece;
                fmt.apply(piece);
                piece.text = QString(ch);
                // Note text is not the host paragraph. A true flag here would
                // let the note body cancel an empty-paragraph suppression.
                appendRevised(noteRevStack_.last(), noteStack_.last(), piece, f.ch, false, false);
            }
            break;
        case Sink::Inst:
            if (!fields_.isEmpty())
                fields_.last().inst.append(ch);
            break;
        case Sink::Result:
            if (!fields_.isEmpty())
                appendRun(fields_.last().result, QString(ch), fmt);
            break;
        case Sink::Pict:
            pictAddChar(ch);
            break;
        case Sink::FontName:
        case Sink::StyleName:
        case Sink::InfoText:
        case Sink::Bkmk:
            stack_.last().buf.append(ch);
            break;
        case Sink::Discard:
            takeUprDiscard(ch);
            break;
        }
    }

    bool takeUprDiscard(QChar ch)
    {
        if (upr_.isEmpty() || upr_.last().sawUd || stack_.isEmpty() || stack_.last().skipAll)
            return false;
        UprState &u = upr_.last();
        if (u.dest == Sink::Inst) {
            u.inst.append(ch);
            return true;
        }
        appendRun(u.runs, QString(ch), fmtOf(stack_.last().ch));
        return true;
    }

    void appendExisting(QVector<TextRun> &runs, const TextRun &incoming)
    {
        if (incoming.text.isEmpty())
            return;
        if (!runs.isEmpty()) {
            TextRun &last = runs.last();
            const bool same = last.field.isEmpty() && last.noteId.isEmpty() && last.link.isEmpty()
                && incoming.field.isEmpty() && incoming.noteId.isEmpty() && incoming.link.isEmpty()
                && last.bold == incoming.bold && last.italic == incoming.italic
                && last.underline == incoming.underline && last.strike == incoming.strike
                && last.superscript == incoming.superscript && last.subscript == incoming.subscript
                && last.font == incoming.font && last.color == incoming.color
                && last.highlight == incoming.highlight && last.lang == incoming.lang
                && last.sizePt == incoming.sizePt && last.hidden == incoming.hidden
                && last.redact == incoming.redact;
            if (same) {
                last.text += incoming.text;
                return;
            }
        }
        runs.push_back(incoming);
    }

    void emitUpr(const UprState &u)
    {
        if (u.dest == Sink::Inst) {
            if (!fields_.isEmpty())
                fields_.last().inst += u.inst;
            return;
        }
        QVector<TextRun> *dest = nullptr;
        if (u.dest == Sink::Body) {
            for (const TextRun &r : u.runs)
                appendBodyRun(r);
            return;
        }
        if (u.dest == Sink::Note && !noteStack_.isEmpty())
            dest = &noteStack_.last();
        else if (u.dest == Sink::Result && !fields_.isEmpty())
            dest = &fields_.last().result;
        if (!dest)
            return;
        for (const TextRun &r : u.runs)
            appendExisting(*dest, r);
    }

    void finishUpr()
    {
        if (upr_.isEmpty())
            return;
        const UprState u = upr_.takeLast();
        if (!u.sawUd)
            emitUpr(u);
    }

    void bubbleListHints()
    {
        if (stack_.size() < 2)
            return;
        const ParaState &src = stack_.last().para;
        ParaState &dst = stack_[stack_.size() - 2].para;
        if (!src.listKindOverride.isEmpty() && dst.listKindOverride.isEmpty())
            dst.listKindOverride = src.listKindOverride;
        if (src.checkSet && !dst.checkSet) {
            dst.checkSet = true;
            dst.checked = src.checked;
        }
    }

    void applyAioList(Kind kind)
    {
        if (kind == Kind::AioListKind) {
            const QString k = stack_.last().buf.trimmed().toLower();
            if (k != QLatin1String("checkbox") && k != QLatin1String("task"))
                return;
            // The writer puts the kind on {\list}. A per-item group still wins later.
            if (!listStack_.isEmpty() && inside(Kind::List)) {
                listStack_.last().kindOverride = k;
                return;
            }
            if (stack_.size() < 2)
                return;
            stack_[stack_.size() - 2].para.listKindOverride = k;
            return;
        }
        if (stack_.size() < 2)
            return;
        ParaState &para = stack_[stack_.size() - 2].para;
        const QString t = stack_.last().buf.trimmed();
        if (t == QLatin1String("0") || t == QLatin1String("1")) {
            para.checkSet = true;
            para.checked = t == QLatin1String("1");
        }
    }

    void flushAnsi()
    {
        if (ansi_.isEmpty() || !error_.isEmpty())
            return;
        const QByteArray bytes = ansi_;
        ansi_.clear();
        QStringDecoder dec(codec_.constData());
        if (!dec.isValid()) {
            fail(QStringLiteral("RTF code page not supported: %1").arg(codePage_));
            return;
        }
        const QString text = dec.decode(bytes);
        if (dec.hasError()) {
            fail(QStringLiteral("Malformed RTF: invalid text for code page %1").arg(codePage_));
            return;
        }
        for (const QChar &c : text)
            appendChar(c);
    }

    void beginGroup()
    {
        ++i_;
        if (static_cast<int>(stack_.size()) >= kMaxDepth) {
            fail(QStringLiteral("RTF group nesting depth exceeds 64"));
            return;
        }
        flushAnsi();
        Frame f;
        if (!stack_.isEmpty()) {
            f.skipAll = stack_.last().skipAll;
            f.sink = f.skipAll ? Sink::Discard : stack_.last().sink;
            f.ch = stack_.last().ch;
            f.para = stack_.last().para;
            f.expectDest = !f.skipAll;
        }
        stack_.push_back(f);
        // {\revtbl{Name;}} entries have no destination word. A raw ';' ends the
        // name; \'3b has already been decoded into the name by then.
        if (stack_.size() >= 2 && stack_.at(stack_.size() - 2).kind == Kind::RevTbl) {
            Frame &child = stack_.last();
            child.kind = Kind::RevAuthor;
            child.sink = Sink::RevName;
            child.expectDest = false;
            child.skipAll = false;
        }
    }

    void endGroup()
    {
        ++i_;
        if (stack_.isEmpty()) {
            fail(QStringLiteral("Malformed RTF: unexpected }"));
            return;
        }
        flushAnsi();
        if (!error_.isEmpty())
            return;
        if (ucSkip_ > 0) {
            fail(QStringLiteral("Malformed RTF: truncated \\u fallback"));
            return;
        }
        if (looseBkmkKind_ != 0 && stack_.last().sink == Sink::Bkmk)
            finishLooseBkmk();
        const Kind kind = stack_.last().kind;
        if (stack_.size() == 1)
            finishDocumentBody();
        else if (kind == Kind::Pict)
            finishPict();
        else if (kind == Kind::Field)
            finishField();
        else if (kind == Kind::Footnote)
            finishNote();
        else if (kind == Kind::FontEntry && !stack_.last().nameDone)
            commitName();
        else if (kind == Kind::StyleEntry && !stack_.last().nameDone)
            commitName();
        else if (kind == Kind::InfoField)
            commitInfo();
        else if (kind == Kind::BkmkStart)
            commitBookmark();
        else if (kind == Kind::List)
            finishList();
        else if (kind == Kind::ListLevel)
            finishListLevel();
        else if (kind == Kind::ListOverride)
            finishOverride();
        else if (kind == Kind::Upr)
            finishUpr();
        else if (kind == Kind::PropName)
            pictSn_ = stack_.last().buf.trimmed();
        else if (kind == Kind::PropValue) {
            const QString value = stack_.last().buf.trimmed();
            if (pictSn_.compare(QLatin1String("wzDescription"), Qt::CaseInsensitive) == 0)
                pictAlt_ = value;
            else if (pictSn_.compare(QLatin1String("wzName"), Qt::CaseInsensitive) == 0)
                pictName_ = value;
        } else if (kind == Kind::AioListKind || kind == Kind::AioCheck)
            applyAioList(kind);
        else if (kind == Kind::RevAuthor && !stack_.last().nameDone)
            commitRevAuthor();
        else if (kind == Kind::AtnAuthor)
            commitAtnAuthor();
        else if (kind == Kind::AtnId)
            commitAtnId();
        else if (kind == Kind::AnnProp)
            commitAnnProp();
        else if (kind == Kind::Annotation)
            finishAnnotation();
        else if (kind == Kind::AioRevFmt)
            finishAioRevFmt();
        else if (kind == Kind::Shp)
            finishShape();
        else if (kind == Kind::SpName && !shapeStack_.isEmpty())
            shapeStack_.last().sn = stack_.last().buf.trimmed();
        else if (kind == Kind::SpVal && !shapeStack_.isEmpty())
            applyShapeProp(shapeStack_.last().sn, stack_.last().buf);
        if (!error_.isEmpty())
            return;
        bubbleListHints();
        stack_.removeLast();
        if (stack_.isEmpty()) {
            while (i_ < n_ && (data_[i_] == ' ' || data_[i_] == '\t' || data_[i_] == '\r' || data_[i_] == '\n'))
                ++i_;
            if (i_ < n_)
                fail(QStringLiteral("Malformed RTF: trailing data"));
        }
    }

    void parseLiteral(unsigned char c)
    {
        if (!sawRtf_) {
            fail(QStringLiteral("Malformed RTF: missing {\\rtf header"));
            return;
        }
        if (c >= 128) {
            ansi_.append(static_cast<char>(c));
            ++i_;
            return;
        }
        flushAnsi();
        if (c == ' ' || c == '\t') {
            appendChar(QChar(QLatin1Char(static_cast<char>(c))));
            ++i_;
            return;
        }
        appendChar(QChar(QLatin1Char(static_cast<char>(c))));
        ++i_;
    }

    bool takeSemicolon()
    {
        if (stack_.isEmpty())
            return false;
        const Sink sink = stack_.last().sink;
        if (sink == Sink::FontName || sink == Sink::StyleName || sink == Sink::RevName) {
            flushAnsi();
            if (sink == Sink::RevName)
                commitRevAuthor();
            else
                commitName();
            ++i_;
            return true;
        }
        if (stack_.last().kind == Kind::ColorTbl) {
            commitColor();
            ++i_;
            return true;
        }
        return false;
    }

    void parseControl()
    {
        ++i_;
        if (i_ >= n_) {
            fail(QStringLiteral("Malformed RTF: truncated control"));
            return;
        }
        const unsigned char c = static_cast<unsigned char>(data_[i_]);
        if (c == '\'') {
            ++i_;
            if (i_ + 1 >= n_) {
                fail(QStringLiteral("Malformed RTF: truncated \\' escape"));
                return;
            }
            const int h1 = hexVal(static_cast<unsigned char>(data_[i_]));
            const int h2 = hexVal(static_cast<unsigned char>(data_[i_ + 1]));
            if (h1 < 0 || h2 < 0) {
                fail(QStringLiteral("Malformed RTF: truncated \\' escape"));
                return;
            }
            i_ += 2;
            if (ucSkip_ > 0) {
                --ucSkip_;
                return;
            }
            ansi_.append(static_cast<char>((h1 << 4) | h2));
            return;
        }
        if (!isAlpha(c)) {
            ++i_;
            onSymbol(static_cast<char>(c));
            return;
        }
        flushAnsi();
        QString word;
        while (i_ < n_ && isAlpha(static_cast<unsigned char>(data_[i_]))) {
            word.append(QLatin1Char(data_[i_]));
            ++i_;
            if (word.size() > 32) {
                fail(QStringLiteral("Malformed RTF: control word too long"));
                return;
            }
        }
        bool neg = false;
        bool has = false;
        bool overflow = false;
        qint64 v = 0;
        if (i_ < n_ && data_[i_] == '-') {
            neg = true;
            ++i_;
        }
        while (i_ < n_ && isDigit(static_cast<unsigned char>(data_[i_]))) {
            has = true;
            const int d = data_[i_] - '0';
            if (v > (std::numeric_limits<qint64>::max() - d) / 10)
                overflow = true;
            else
                v = v * 10 + d;
            ++i_;
        }
        if (neg && !has)
            has = false;
        if (i_ < n_ && data_[i_] == ' ')
            ++i_;
        const qint64 signedV = neg ? -v : v;
        if (ucSkip_ > 0 && word != QLatin1String("bin")) {
            --ucSkip_;
            return;
        }
        if (word == QLatin1String("bin")) {
            onBin(signedV, has, overflow);
            return;
        }
        if (word == QLatin1String("u")) {
            onUnicode(signedV, has, overflow);
            return;
        }
        if (!stack_.isEmpty() && stack_.last().skipAll)
            return;
        if (!stack_.isEmpty() && stack_.last().expectDest) {
            stack_.last().expectDest = false;
            if (tryDestination(word, signedV, has)) {
                starNext_ = false;
                return;
            }
            if (starNext_) {
                stack_.last().skipAll = true;
                stack_.last().sink = Sink::Discard;
                starNext_ = false;
                return;
            }
        } else if (starNext_) {
            starNext_ = false;
            return;
        }
        if (looseBkmkKind_ != 0)
            finishLooseBkmk();
        onFormatting(word, signedV, has, overflow);
    }

    void onSymbol(char c)
    {
        if (c == '*') {
            starNext_ = true;
            return;
        }
        if (ucSkip_ > 0) {
            --ucSkip_;
            return;
        }
        flushAnsi();
        if (!stack_.isEmpty() && stack_.last().skipAll)
            return;
        switch (c) {
        case '\\': appendChar(QLatin1Char('\\')); break;
        case '{': appendChar(QLatin1Char('{')); break;
        case '}': appendChar(QLatin1Char('}')); break;
        case '~': appendChar(QChar(0x00A0)); break;
        case '-': appendChar(QChar(0x00AD)); break;
        case '_': appendChar(QChar(0x2011)); break;
        default: break;
        }
    }

    void onUnicode(qint64 v, bool has, bool overflow)
    {
        if (!has || overflow || v < -32768 || v > 32767) {
            fail(QStringLiteral("RTF \\u value out of range"));
            return;
        }
        int code = static_cast<int>(v);
        if (code < 0)
            code += 65536;
        appendChar(QChar(static_cast<char16_t>(code)));
        int uc = stack_.isEmpty() ? 1 : stack_.last().ch.uc;
        if (uc < 0)
            uc = 0;
        ucSkip_ = uc;
    }

    void onBin(qint64 v, bool has, bool overflow)
    {
        const bool inPict = pictActive_ && !stack_.isEmpty() && !stack_.last().skipAll
            && stack_.last().sink == Sink::Pict;
        if (!has || v < 0) {
            if (overflow)
                fail(inPict ? QStringLiteral("RTF \\pict exceeds 8388608 bytes")
                            : QStringLiteral("RTF \\bin exceeds 8388608 bytes"));
            else
                fail(QStringLiteral("Malformed RTF: \\bin length is invalid"));
            return;
        }
        if (inPict && (overflow || v > kMaxPict || static_cast<qint64>(pict_.size()) + v > kMaxPict)) {
            fail(QStringLiteral("RTF \\pict exceeds 8388608 bytes"));
            return;
        }
        if (overflow || v > kMaxBin) {
            fail(QStringLiteral("RTF \\bin exceeds 8388608 bytes"));
            return;
        }
        if (v > static_cast<qint64>(n_ - i_)) {
            fail(QStringLiteral("Malformed RTF: \\bin extends past end of file"));
            return;
        }
        if (inPict)
            pict_.append(data_.constData() + i_, static_cast<int>(v));
        i_ += static_cast<int>(v);
    }

    bool fitsInt(qint64 v, bool has, bool overflow, int *out) const
    {
        if (!has || overflow || v > std::numeric_limits<int>::max() || v < std::numeric_limits<int>::min())
            return false;
        *out = static_cast<int>(v);
        return true;
    }

    bool tryDestination(const QString &word, qint64 v, bool has)
    {
        if (!sawRtf_) {
            if (word == QLatin1String("rtf")) {
                sawRtf_ = true;
                stack_.last().kind = Kind::Rtf;
                return true;
            }
            fail(QStringLiteral("Malformed RTF: missing {\\rtf header"));
            return true;
        }
        auto skip = [&]() {
            stack_.last().skipAll = true;
            stack_.last().sink = Sink::Discard;
            return true;
        };
        if (word == QLatin1String("fonttbl")) {
            stack_.last().kind = Kind::FontTbl;
            stack_.last().sink = Sink::Discard;
            return true;
        }
        if (word == QLatin1String("f") && inside(Kind::FontTbl)) {
            stack_.last().kind = Kind::FontEntry;
            stack_.last().sink = Sink::FontName;
            stack_.last().fontIndex = has ? static_cast<int>(v) : 0;
            return true;
        }
        if (word == QLatin1String("colortbl")) {
            stack_.last().kind = Kind::ColorTbl;
            stack_.last().sink = Sink::Discard;
            cr_ = cg_ = cb_ = -1;
            colorSeen_ = false;
            return true;
        }
        if (word == QLatin1String("stylesheet")) {
            stack_.last().kind = Kind::StyleSheet;
            stack_.last().sink = Sink::Discard;
            return true;
        }
        if (word == QLatin1String("s") && inside(Kind::StyleSheet) && !inside(Kind::StyleEntry)) {
            stack_.last().kind = Kind::StyleEntry;
            stack_.last().sink = Sink::StyleName;
            stack_.last().styleIndex = has ? static_cast<int>(v) : 0;
            return true;
        }
        if (word == QLatin1String("info")) {
            stack_.last().kind = Kind::Info;
            stack_.last().sink = Sink::Discard;
            return true;
        }
        if (word == QLatin1String("title") || word == QLatin1String("author") || word == QLatin1String("subject")
            || word == QLatin1String("keywords") || word == QLatin1String("operator")
            || word == QLatin1String("company") || word == QLatin1String("doccomm")) {
            stack_.last().kind = Kind::InfoField;
            stack_.last().sink = Sink::InfoText;
            if (word == QLatin1String("title"))
                stack_.last().info = InfoKind::Title;
            else if (word == QLatin1String("author"))
                stack_.last().info = InfoKind::Author;
            else if (word == QLatin1String("subject"))
                stack_.last().info = InfoKind::Subject;
            else if (word == QLatin1String("keywords"))
                stack_.last().info = InfoKind::Keywords;
            return true;
        }
        if (word == QLatin1String("upr")) {
            UprState u;
            u.dest = stack_.size() >= 2 ? stack_.at(stack_.size() - 2).sink : Sink::Body;
            upr_.push_back(u);
            stack_.last().kind = Kind::Upr;
            stack_.last().sink = Sink::Discard;
            return true;
        }
        if (word == QLatin1String("ud")) {
            stack_.last().kind = Kind::Ud;
            if (!upr_.isEmpty()) {
                upr_.last().sawUd = true;
                upr_.last().runs.clear();
                upr_.last().inst.clear();
                stack_.last().sink = upr_.last().dest;
            }
            // A lone \ud keeps the sink it inherited. Recognized children
            // (title, fldinst) still override that sink.
            return true;
        }
        if (word == QLatin1String("listtable")) {
            stack_.last().kind = Kind::ListTable;
            stack_.last().sink = Sink::Discard;
            return true;
        }
        if (word == QLatin1String("list")) {
            stack_.last().kind = Kind::List;
            stack_.last().sink = Sink::Discard;
            listStack_.push_back(ListDef{});
            return true;
        }
        if (word == QLatin1String("listlevel")) {
            stack_.last().kind = Kind::ListLevel;
            stack_.last().sink = Sink::Discard;
            levelStack_.push_back(ListLevel{});
            return true;
        }
        if (word == QLatin1String("listoverridetable")) {
            stack_.last().kind = Kind::ListOverrideTable;
            stack_.last().sink = Sink::Discard;
            return true;
        }
        if (word == QLatin1String("listoverride")) {
            stack_.last().kind = Kind::ListOverride;
            stack_.last().sink = Sink::Discard;
            ovStack_.push_back(OverrideBuild{});
            return true;
        }
        if (word == QLatin1String("field")) {
            stack_.last().kind = Kind::Field;
            stack_.last().sink = Sink::Discard;
            fields_.push_back(FieldState{});
            return true;
        }
        if (word == QLatin1String("fldinst")) {
            stack_.last().sink = Sink::Inst;
            return true;
        }
        if (word == QLatin1String("fldrslt") || word == QLatin1String("result")) {
            stack_.last().sink = Sink::Result;
            return true;
        }
        if (word == QLatin1String("shppict")) {
            stack_.last().sink = Sink::Discard;
            return true;
        }
        if (word == QLatin1String("revtbl")) {
            stack_.last().kind = Kind::RevTbl;
            stack_.last().sink = Sink::Discard;
            revAuthors_.clear();
            return true;
        }
        if (word == QLatin1String("annotation")) {
            beginAnnotation();
            return true;
        }
        if (word == QLatin1String("atnauthor")) {
            stack_.last().kind = Kind::AtnAuthor;
            stack_.last().sink = Sink::InfoText;
            return true;
        }
        if (word == QLatin1String("atnid")) {
            stack_.last().kind = Kind::AtnId;
            stack_.last().sink = Sink::InfoText;
            return true;
        }
        if (word == QLatin1String("aiocid") || word == QLatin1String("aiocparent")
            || word == QLatin1String("aiocresolved") || word == QLatin1String("atnref")) {
            stack_.last().kind = Kind::AnnProp;
            stack_.last().tag = word;
            stack_.last().sink = Sink::InfoText;
            if (word == QLatin1String("aiocresolved") && has)
                stack_.last().buf = QString::number(v);
            return true;
        }
        if (word == QLatin1String("aiorevfmt")) {
            stack_.last().kind = Kind::AioRevFmt;
            stack_.last().sink = Sink::InfoText;
            return true;
        }
        if (word == QLatin1String("aiocarrier")) {
            paraCarrier_ = true;
            return skip();
        }
        if (word == QLatin1String("shpgrp")) {
            if (!warnedShpGrp_) {
                warnedShpGrp_ = true;
                warnings_ << QStringLiteral("RTF import: shape group ignored");
            }
            return skip();
        }
        if (word == QLatin1String("shp")) {
            stack_.last().kind = Kind::Shp;
            stack_.last().sink = Sink::Discard;
            shapeStack_.push_back(ShapeBuild{});
            return true;
        }
        if (word == QLatin1String("shpinst")) {
            stack_.last().kind = Kind::ShpInst;
            stack_.last().sink = Sink::Discard;
            return true;
        }
        if (word == QLatin1String("shptxt")) {
            stack_.last().kind = Kind::ShpTxt;
            stack_.last().sink = Sink::ShpTxt;
            return true;
        }
        if (word == QLatin1String("shprslt")) {
            // Inside an accepted shape the fallback is a duplicate. Otherwise the
            // text belongs to the sink outside the shape (the shape group itself
            // discards).
            if (!shapeStack_.isEmpty() && acceptedShape(shapeStack_.last()))
                return skip();
            stack_.last().sink = sinkOutsideShape();
            return true;
        }
        if (word == QLatin1String("sp") && inside(Kind::Shp)) {
            stack_.last().kind = Kind::Sp;
            stack_.last().sink = Sink::Discard;
            return true;
        }
        if (word == QLatin1String("sn") && inside(Kind::Sp)) {
            stack_.last().kind = Kind::SpName;
            stack_.last().sink = Sink::InfoText;
            return true;
        }
        if (word == QLatin1String("sv") && inside(Kind::Sp)) {
            stack_.last().kind = Kind::SpVal;
            stack_.last().sink = Sink::InfoText;
            return true;
        }
        if (word == QLatin1String("pict")) {
            if (inside(Kind::Shp) || inside(Kind::ShpInst) || inside(Kind::ShpTxt))
                return skip();
        }
        if (word == QLatin1String("pict")) {
            stack_.last().kind = Kind::Pict;
            stack_.last().sink = Sink::Pict;
            pict_.clear();
            pictActive_ = true;
            pictPng_ = false;
            pictJpeg_ = false;
            pictHi_ = false;
            pictGoal_ = 0;
            pictAlt_.clear();
            pictName_.clear();
            pictSn_.clear();
            return true;
        }
        if (word == QLatin1String("picprop")) {
            stack_.last().kind = Kind::PicProp;
            stack_.last().sink = Sink::Discard;
            return true;
        }
        if ((word == QLatin1String("sn") || word == QLatin1String("sv")) && inside(Kind::PicProp)) {
            stack_.last().kind = word == QLatin1String("sn") ? Kind::PropName : Kind::PropValue;
            stack_.last().sink = Sink::InfoText;
            return true;
        }
        if (word == QLatin1String("aiolistkind")) {
            stack_.last().kind = Kind::AioListKind;
            stack_.last().sink = Sink::InfoText;
            return true;
        }
        if (word == QLatin1String("aiocheck")) {
            // Spec form is {\*\aiocheck 0} / {\*\aiocheck 1}: the space ends the
            // control word, so 0|1 is destination text. \aiocheck0 / \aiocheck1
            // (numeric parameter, no text) is accepted as the same signal.
            stack_.last().kind = Kind::AioCheck;
            stack_.last().sink = Sink::InfoText;
            if (has && (v == 0 || v == 1))
                stack_.last().buf = QString::number(v);
            return true;
        }
        if (word == QLatin1String("footnote")) {
            stack_.last().kind = Kind::Footnote;
            stack_.last().sink = Sink::Note;
            noteStack_.push_back({});
            noteEnd_.push_back(false);
            noteRevStack_.push_back(RevBuf{});
            return true;
        }
        if (word == QLatin1String("bkmkstart")) {
            stack_.last().kind = Kind::BkmkStart;
            stack_.last().sink = Sink::Bkmk;
            return true;
        }
        if (word == QLatin1String("bkmkend")) {
            stack_.last().kind = Kind::BkmkEnd;
            stack_.last().sink = Sink::Bkmk;
            return true;
        }
        if (word == QLatin1String("listtext")) {
            // {\listtext\tab} is only the marker. A group that contains \par holds
            // the paragraph text (hand-written Word fixture) and must be kept.
            if (!groupContainsWord("par"))
                return skip();
            stack_.last().sink = Sink::Body;
            return true;
        }
        if (word == QLatin1String("nonshppict") || word == QLatin1String("pn")
            || word == QLatin1String("pntext") || word == QLatin1String("pntxta") || word == QLatin1String("pntxtb")
            || word == QLatin1String("leveltext") || word == QLatin1String("levelnumbers")
            || word == QLatin1String("header") || word == QLatin1String("footer") || word == QLatin1String("headerl")
            || word == QLatin1String("headerr") || word == QLatin1String("headerf") || word == QLatin1String("footerl")
            || word == QLatin1String("footerr") || word == QLatin1String("footerf") || word == QLatin1String("ftnsep")
            || word == QLatin1String("ftnsepc") || word == QLatin1String("ftncn") || word == QLatin1String("aftnsep")
            || word == QLatin1String("generator") || word == QLatin1String("userprops")
            || word == QLatin1String("xmlnstbl") || word == QLatin1String("latentstyles")
            || word == QLatin1String("colorschememapping") || word == QLatin1String("datastore")
            || word == QLatin1String("themedata") || word == QLatin1String("rsidtbl")
            || word == QLatin1String("background") || word == QLatin1String("blipuid")
            || word == QLatin1String("object") || word == QLatin1String("objdata") || word == QLatin1String("comment")
            || word == QLatin1String("falt"))
            return skip();
        Q_UNUSED(has);
        return false;
    }

    static bool flagOn(qint64 v, bool has) { return !has || v != 0; }

    void resetChar(CharState &ch) const
    {
        const int uc = ch.uc;
        ch = CharState{};
        ch.uc = uc;
    }

    void onFormatting(const QString &word, qint64 v, bool has, bool overflow)
    {
        if (stack_.isEmpty())
            return;
        Frame &f = stack_.last();
        int iv = 0;
        const bool ok = fitsInt(v, has, overflow, &iv);
        auto para = [&]() -> ParaState & { return stack_.last().para; };
        auto ch = [&]() -> CharState & { return stack_.last().ch; };

        if (word == QLatin1String("bkmkstart") || word == QLatin1String("bkmkend")) {
            if (looseBkmkKind_ != 0)
                finishLooseBkmk();
            looseBkmkKind_ = word == QLatin1String("bkmkstart") ? 1 : 2;
            f.sink = Sink::Bkmk;
            f.buf.clear();
            return;
        }

        if (word == QLatin1String("par")) {
            onPar();
            return;
        }
        if (word == QLatin1String("pard")) {
            stack_.last().para = ParaState{};
            return;
        }
        if (word == QLatin1String("plain")) {
            resetChar(stack_.last().ch);
            return;
        }
        if (word == QLatin1String("page")) {
            onPage();
            return;
        }
        if (word == QLatin1String("column")) {
            onColumn();
            return;
        }
        if (word == QLatin1String("ftnalt")) {
            if (!noteEnd_.isEmpty())
                noteEnd_.last() = true;
            return;
        }
        if (word == QLatin1String("cell")) {
            onCell();
            return;
        }
        if (word == QLatin1String("row")) {
            onRow();
            return;
        }
        if (word == QLatin1String("trowd")) {
            cellX_.clear();
            rowHeader_ = false;
            trLeft_ = 0;
            return;
        }
        if (word == QLatin1String("trhdr")) {
            rowHeader_ = true;
            return;
        }
        if (word == QLatin1String("cellx") && ok)
            cellX_.push_back(iv);
        else if (word == QLatin1String("trleft") && ok)
            trLeft_ = iv;
        else if (word == QLatin1String("intbl"))
            para().inTable = flagOn(v, has);
        else if (word == QLatin1String("v"))
            ch().hidden = flagOn(v, has);
        else if (word == QLatin1String("revised"))
            ch().revised = flagOn(v, has);
        else if (word == QLatin1String("deleted"))
            ch().deleted = flagOn(v, has);
        else if (word == QLatin1String("revauth"))
            ch().revauth = storeAuth(v, has, overflow);
        else if (word == QLatin1String("revauthdel"))
            ch().revauthdel = storeAuth(v, has, overflow);
        else if (word == QLatin1String("crauth"))
            ch().crauth = storeAuth(v, has, overflow);
        else if (word == QLatin1String("revdttm"))
            storeDttm(ch().revdttmSet, ch().revdttm, ch().revdttmBad, v, has, overflow);
        else if (word == QLatin1String("revdttmdel"))
            storeDttm(ch().revdttmdelSet, ch().revdttmdel, ch().revdttmdelBad, v, has, overflow);
        else if (word == QLatin1String("crdate"))
            storeDttm(ch().crdateSet, ch().crdate, ch().crdateBad, v, has, overflow);
        else if (word == QLatin1String("chatn"))
            onChatn();
        else if (!shapeStack_.isEmpty()
                 && (word == QLatin1String("shpleft") || word == QLatin1String("shptop")
                     || word == QLatin1String("shpright") || word == QLatin1String("shpbottom")
                     || word == QLatin1String("shpz")))
            applyShapeGeometry(word, v, has, overflow);
        else if (word == QLatin1String("b"))
            ch().bold = flagOn(v, has);
        else if (word == QLatin1String("ab"))
            ch().ab = flagOn(v, has);
        else if (word == QLatin1String("i"))
            ch().italic = flagOn(v, has);
        else if (word == QLatin1String("ai"))
            ch().ai = flagOn(v, has);
        else if (word == QLatin1String("ulnone"))
            ch().underline = false;
        else if (word == QLatin1String("ulc"))
            ;
        else if (word.startsWith(QLatin1String("ul")))
            ch().underline = !(word == QLatin1String("ul") && has && v == 0);
        else if (word == QLatin1String("strike") || word == QLatin1String("striked"))
            ch().strike = flagOn(v, has);
        else if (word == QLatin1String("super")) {
            ch().super = flagOn(v, has);
            if (ch().super)
                ch().sub = false;
        } else if (word == QLatin1String("sub")) {
            ch().sub = flagOn(v, has);
            if (ch().sub)
                ch().super = false;
        } else if (word == QLatin1String("nosupersub")) {
            ch().super = false;
            ch().sub = false;
        } else if (word == QLatin1String("f") && ok)
            ch().font = iv;
        else if ((word == QLatin1String("fs") || word == QLatin1String("afs")) && ok && iv > 0 && iv < 4000) {
            ch().fs = iv;
            ch().fsSet = true;
        } else if (word == QLatin1String("cf") && ok)
            ch().cf = iv;
        else if ((word == QLatin1String("highlight") || word == QLatin1String("cb") || word == QLatin1String("chcbpat"))
                 && ok)
            ch().highlight = iv;
        else if (word == QLatin1String("lang") && ok)
            ch().lang = iv;
        else if (word == QLatin1String("uc") && ok) {
            if (iv < 0 || iv > kMaxUc)
                fail(QStringLiteral("Malformed RTF: \\uc value out of range"));
            else
                ch().uc = iv;
        } else if (word == QLatin1String("deff") && ok)
            deff_ = iv;
        else if (word == QLatin1String("ansicpg") && ok)
            setCodePage(iv);
        else if (word == QLatin1String("ansi"))
            setCodePage(1252);
        else if (word == QLatin1String("mac"))
            setCodePage(10000);
        else if (word == QLatin1String("pc"))
            setCodePage(437);
        else if (word == QLatin1String("pca"))
            setCodePage(850);
        else if (word == QLatin1String("ql"))
            para().align = QStringLiteral("left");
        else if (word == QLatin1String("qr"))
            para().align = QStringLiteral("right");
        else if (word == QLatin1String("qc"))
            para().align = QStringLiteral("center");
        else if (word == QLatin1String("qj") || word == QLatin1String("qd"))
            para().align = QStringLiteral("justify");
        else if ((word == QLatin1String("li") || word == QLatin1String("lin")) && ok)
            para().li = iv;
        else if ((word == QLatin1String("ri") || word == QLatin1String("rin")) && ok)
            para().ri = iv;
        else if (word == QLatin1String("fi") && ok)
            para().fi = iv;
        else if (word == QLatin1String("sb") && ok) {
            para().sb = iv;
            para().sbSet = true;
        } else if (word == QLatin1String("sa") && ok) {
            para().sa = iv;
            para().saSet = true;
        } else if (word == QLatin1String("sl") && ok) {
            para().sl = iv;
            para().slSet = true;
        } else if (word == QLatin1String("slmult"))
            para().slmult = flagOn(v, has);
        else if (word == QLatin1String("rtlpar"))
            para().dir = QStringLiteral("rtl");
        else if (word == QLatin1String("ltrpar"))
            para().dir.clear();
        else if (word == QLatin1String("s") && ok)
            para().styleIndex = iv;
        else if (word == QLatin1String("ls") && ok) {
            if (f.kind == Kind::ListOverride && !ovStack_.isEmpty())
                ovStack_.last().ls = iv;
            else
                para().ls = iv;
        } else if (word == QLatin1String("ilvl") && ok)
            para().ilvl = iv;
        else if (word == QLatin1String("outlinelevel") && ok)
            para().outline = iv;
        else if (word == QLatin1String("keepn"))
            para().keepn = flagOn(v, has);
        else if (word == QLatin1String("pagebb"))
            para().pagebb = flagOn(v, has);
        else if (word == QLatin1String("listid") && ok) {
            if (f.kind == Kind::ListOverride && !ovStack_.isEmpty())
                ovStack_.last().listId = iv;
            else if (!listStack_.isEmpty())
                listStack_.last().id = iv;
        } else if (word == QLatin1String("levelnfc") && ok && !levelStack_.isEmpty())
            levelStack_.last().nfc = iv;
        else if (word == QLatin1String("red") && ok && inside(Kind::ColorTbl)) {
            cr_ = qBound(0, iv, 255);
            colorSeen_ = true;
        } else if (word == QLatin1String("green") && ok && inside(Kind::ColorTbl)) {
            cg_ = qBound(0, iv, 255);
            colorSeen_ = true;
        } else if (word == QLatin1String("blue") && ok && inside(Kind::ColorTbl)) {
            cb_ = qBound(0, iv, 255);
            colorSeen_ = true;
        } else if (word == QLatin1String("paperw") && ok)
            paperW_ = iv;
        else if (word == QLatin1String("paperh") && ok)
            paperH_ = iv;
        else if (word == QLatin1String("margl") && ok)
            margL_ = iv;
        else if (word == QLatin1String("margr") && ok)
            margR_ = iv;
        else if (word == QLatin1String("margt") && ok)
            margT_ = iv;
        else if (word == QLatin1String("margb") && ok)
            margB_ = iv;
        else if (word == QLatin1String("pngblip") && pictActive_)
            pictPng_ = true;
        else if (word == QLatin1String("jpegblip") && pictActive_)
            pictJpeg_ = true;
        else if (word == QLatin1String("picwgoal") && ok && pictActive_)
            pictGoal_ = iv;
        else if (word == QLatin1String("tab"))
            appendChar(QLatin1Char('\t'));
        else if (word == QLatin1String("line"))
            appendChar(QLatin1Char('\n'));
        else if (word == QLatin1String("emdash"))
            appendChar(QChar(0x2014));
        else if (word == QLatin1String("endash"))
            appendChar(QChar(0x2013));
        else if (word == QLatin1String("lquote"))
            appendChar(QChar(0x2018));
        else if (word == QLatin1String("rquote"))
            appendChar(QChar(0x2019));
        else if (word == QLatin1String("ldblquote"))
            appendChar(QChar(0x201C));
        else if (word == QLatin1String("rdblquote"))
            appendChar(QChar(0x201D));
        else if (word == QLatin1String("bullet"))
            appendChar(QChar(0x2022));
        // A field switch written as a control word (\l, not \\l) is eaten before
        // the instruction string exists. Put \l \o \t \m \n back so HYPERLINK can
        // tell the switch from the URL. The argument, when there is one, follows
        // as ordinary destination text.
        if (!fields_.isEmpty() && f.sink == Sink::Inst) {
            const QString sw = word.toLower();
            if (sw == QLatin1String("l") || sw == QLatin1String("o") || sw == QLatin1String("t")
                || sw == QLatin1String("m") || sw == QLatin1String("n")) {
                fields_.last().inst.append(QLatin1Char('\\'));
                fields_.last().inst.append(sw);
            }
        }
        Q_UNUSED(f);
    }

    void onPar()
    {
        if (stack_.isEmpty())
            return;
        const Frame &f = stack_.last();
        if (f.skipAll)
            return;
        if (f.sink == Sink::AnnText) {
            annText_.append(QLatin1Char('\n'));
            return;
        }
        if (f.sink == Sink::ShpTxt) {
            if (!shapeStack_.isEmpty())
                appendRun(shapeStack_.last().runs, QStringLiteral("\n"), fmtOf(f.ch));
            return;
        }
        if (f.sink == Sink::Note) {
            if (!noteStack_.isEmpty() && !noteRevStack_.isEmpty()) {
                TextRun piece;
                fmtOf(f.ch).apply(piece);
                piece.text = QString(QChar(0x2029));
                appendRevised(noteRevStack_.last(), noteStack_.last(), piece, f.ch, false, false);
            }
            return;
        }
        if (f.sink == Sink::Result) {
            if (!fields_.isEmpty())
                appendRun(fields_.last().result, QStringLiteral("\n"), fmtOf(f.ch));
            return;
        }
        if (f.sink != Sink::Body)
            return;
        if (f.para.inTable) {
            const RevMark mark = markOf(f.ch);
            if (mark.kind == RevKind::Delete) {
                warnCellRev();
                return;
            }
            if (mark.kind != RevKind::None)
                warnCellRev();
            appendRun(bodyRuns_, QStringLiteral("\n"), fmtOf(f.ch));
            return;
        }
        flushBodyParagraph(f.para, true);
    }

    void onPage()
    {
        if (stack_.isEmpty() || stack_.last().sink != Sink::Body || stack_.last().skipAll)
            return;
        if (stack_.last().para.inTable)
            return;
        if (paragraphPending())
            flushBodyParagraph(stack_.last().para, false);
        Block br;
        br.type = QStringLiteral("break");
        br.breakKind = QStringLiteral("page");
        addBlock(br);
        suppressEmptyPar_ = true;
    }

    void onColumn()
    {
        if (stack_.isEmpty() || stack_.last().sink != Sink::Body || stack_.last().skipAll)
            return;
        if (stack_.last().para.inTable)
            return;
        if (paragraphPending())
            flushBodyParagraph(stack_.last().para, false);
        Block br;
        br.type = QStringLiteral("break");
        br.breakKind = QStringLiteral("column");
        addBlock(br);
        suppressEmptyPar_ = true;
    }

    void onCell()
    {
        if (stack_.isEmpty() || stack_.last().skipAll)
            return;
        if (stack_.last().para.inTable) {
            // Cell text keeps no revision (D5). Drop the open mark; do not commit it.
            paraRev_.open = OpenRev{};
            paraRev_.side = OpenRev{};
        }
        if (rowCells_.size() >= kMaxCells) {
            fail(QStringLiteral("RTF table exceeds 256 columns"));
            return;
        }
        TableCell cell;
        cell.runs = bodyRuns_.isEmpty() ? QVector<TextRun>{TextRun{}} : bodyRuns_;
        rowCells_.push_back(cell);
        bodyRuns_.clear();
    }

    void onRow()
    {
        if (!bodyRuns_.isEmpty())
            onCell();
        if (rowCells_.isEmpty())
            return;
        if (!tableOpen_) {
            table_ = Block{};
            table_.type = QStringLiteral("table");
            table_.style = QStringLiteral("Normal");
            table_.align = QStringLiteral("left");
            tableOpen_ = true;
        }
        if (table_.table.size() >= kMaxRows) {
            fail(QStringLiteral("RTF table exceeds 5000 rows"));
            return;
        }
        if (table_.table.isEmpty()) {
            table_.headerRow = rowHeader_;
            int prev = trLeft_;
            for (int x : cellX_) {
                table_.colWidthsMm.push_back(twipsToMm(qMax(0, x - prev)));
                prev = x;
            }
        }
        table_.table.push_back(rowCells_);
        rowCells_.clear();
        rowHeader_ = false;
        cellX_.clear();
    }

    void pushTable()
    {
        if (!rowCells_.isEmpty())
            onRow();
        if (!tableOpen_ || table_.table.isEmpty()) {
            tableOpen_ = false;
            table_ = Block{};
            return;
        }
        if (!addBlock(table_)) {
            table_ = Block{};
            tableOpen_ = false;
            tableComments_.clear();
            return;
        }
        if (!doc_.body.isEmpty() && !tableComments_.isEmpty()) {
            for (Comment &c : tableComments_)
                ensureCommentId(c);
            doc_.body.last().comments += tableComments_;
        }
        tableComments_.clear();
        table_ = Block{};
        tableOpen_ = false;
        // Notes whose marker is already in the table. A note opened in the
        // paragraph that follows stays pending until that paragraph is flushed.
        resolveNotes(true);
    }

    void flushBodyParagraph(const ParaState &para, bool allowEmpty)
    {
        if (tableOpen_ && !para.inTable)
            pushTable();
        closeBuf(paraRev_, bodyRuns_);
        if (paraCarrier_) {
            if (!doc_.body.isEmpty()) {
                for (Comment &c : paraComments_)
                    ensureCommentId(c);
                doc_.body.last().comments += paraComments_;
            } else if (!paraComments_.isEmpty() && !warnedCarrier_) {
                warnedCarrier_ = true;
                warnings_ << QStringLiteral("RTF import: carrier paragraph has no previous block");
            }
            paraCarrier_ = false;
            paraComments_.clear();
            paraRev_ = RevBuf{};
            bodyRuns_.clear();
            pendingBms_.clear();
            suppressEmptyPar_ = false;
            return;
        }
        if (bodyRuns_.isEmpty() && pendingBms_.isEmpty() && paraRev_.revs.isEmpty() && paraComments_.isEmpty()) {
            if (!allowEmpty || suppressEmptyPar_) {
                suppressEmptyPar_ = false;
                return;
            }
            bodyRuns_.push_back(TextRun{});
        }
        suppressEmptyPar_ = false;
        Block b = makeBlock(bodyRuns_, para);
        if (!paraComments_.isEmpty()) {
            for (Comment &c : paraComments_)
                ensureCommentId(c);
            b.comments += paraComments_;
        }
        if (!addBlock(b)) {
            bodyRuns_.clear();
            paraComments_.clear();
            paraRev_ = RevBuf{};
            pendingBms_.clear();
            return;
        }
        attachParaRevs(doc_.body.last().id);
        paraComments_.clear();
        bodyRuns_.clear();
        if (!doc_.body.isEmpty()) {
            for (const PendingBookmark &bm : pendingBms_) {
                Bookmark mark;
                mark.name = bm.name;
                mark.id = bm.name;
                mark.blockId = doc_.body.last().id;
                mark.offset = bm.offset;
                doc_.bookmarks.push_back(mark);
            }
        }
        pendingBms_.clear();
        resolveNotes(false);
    }

    void finishDocumentBody()
    {
        if (stack_.isEmpty())
            return;
        const ParaState para = stack_.last().para;
        if (paragraphPending())
            flushBodyParagraph(para, false);
        else if (tableOpen_)
            pushTable();
        resolveNotes(false);
    }

    Block makeBlock(QVector<TextRun> runs, const ParaState &para) const
    {
        Block b;
        b.align = para.align.isEmpty() ? QStringLiteral("left") : para.align;
        b.dir = para.dir;
        if (para.li != 0)
            b.indentLeftMm = twipsToMm(para.li);
        if (para.ri != 0)
            b.indentRightMm = twipsToMm(para.ri);
        if (para.fi != 0)
            b.indentFirstMm = twipsToMm(para.fi);
        if (para.sbSet)
            b.spaceBeforePt = para.sb / 20.0;
        if (para.saSet)
            b.spaceAfterPt = para.sa / 20.0;
        if (para.slSet && para.slmult && para.sl > 0)
            b.lineHeight = std::round(para.sl / 240.0 * 100.0) / 100.0;
        b.keepWithNext = para.keepn;
        b.breakBefore = para.pagebb;

        QString styleName = QStringLiteral("Normal");
        int outline = para.outline;
        if (styles_.contains(para.styleIndex)) {
            const StyleInfo st = styles_.value(para.styleIndex);
            if (!st.name.isEmpty())
                styleName = st.name;
            if (outline < 0)
                outline = st.outline;
        }
        int hLevel = headingLevelOf(styleName);
        if (outline >= 0)
            hLevel = outline + 1;
        else if (hLevel <= 0 && headingLevelOf(styleName) > 0)
            hLevel = headingLevelOf(styleName);

        int nfc = 255;
        QString listKindFromDef;
        if (para.ls > 0 && overrides_.contains(para.ls)) {
            const int listId = overrides_.value(para.ls);
            if (lists_.contains(listId)) {
                const ListDef def = lists_.value(listId);
                listKindFromDef = def.kindOverride;
                if (para.ilvl >= 0 && para.ilvl < def.levels.size())
                    nfc = def.levels.at(para.ilvl).nfc;
            }
        }
        const QString kindOverride = !para.listKindOverride.isEmpty() ? para.listKindOverride : listKindFromDef;
        const bool headingStyle = headingLevelOf(styleName) > 0 || outline >= 0;
        if (!kindOverride.isEmpty()) {
            b.type = QStringLiteral("list-item");
            b.level = qBound(1, para.ilvl + 1, 9);
            b.listKind = kindOverride;
            b.checked = para.checkSet && para.checked;
            b.style = styleName.trimmed().isEmpty() ? QStringLiteral("Normal") : styleName.trimmed();
        } else if (headingStyle && hLevel > 0 && (nfc == 255 || para.ls <= 0)) {
            b.type = QStringLiteral("heading");
            b.level = qBound(1, hLevel, 9);
            b.style = headingStyleName(styleName, b.level);
        } else if (para.ls > 0 && nfc != 255 && nfc >= 0) {
            b.type = QStringLiteral("list-item");
            b.level = qBound(1, para.ilvl + 1, 9);
            b.listKind = nfc == 23 ? QStringLiteral("bullet") : QStringLiteral("decimal");
            if (para.checkSet)
                b.checked = para.checked;
            b.style = styleName.trimmed().isEmpty() ? QStringLiteral("Normal") : styleName.trimmed();
        } else if (headingStyle && hLevel > 0) {
            b.type = QStringLiteral("heading");
            b.level = qBound(1, hLevel, 9);
            b.style = headingStyleName(styleName, b.level);
        } else {
            b.type = QStringLiteral("paragraph");
            b.style = styleName.trimmed().isEmpty() ? QStringLiteral("Normal") : styleName.trimmed();
        }
        hoistLang(runs, b);
        if (runs.isEmpty())
            runs.push_back(TextRun{});
        b.runs = runs;
        return b;
    }

    static QString headingStyleName(const QString &raw, int level)
    {
        const QString n = raw.trimmed().toLower();
        if ((n == QLatin1String("title") || n == QLatin1String("título") || n == QLatin1String("titulo"))
            && headingLevelOf(raw) == 1 && !n.contains(QLatin1Char('1')))
            return QStringLiteral("Title");
        return QStringLiteral("Heading %1").arg(level);
    }

    static void hoistLang(QVector<TextRun> &runs, Block &b)
    {
        if (runs.isEmpty())
            return;
        const QString lang = runs.first().lang;
        if (lang.isEmpty())
            return;
        for (const TextRun &r : runs) {
            if (r.lang != lang)
                return;
        }
        b.lang = lang;
        for (TextRun &r : runs)
            r.lang.clear();
    }

    QString autoId(const QString &type)
    {
        // Document::assignId scans every block. A footnote file adds two blocks
        // per note, so that scan is quadratic and a modest RTF blocks the session.
        QString prefix = QStringLiteral("p");
        if (type == QLatin1String("heading"))
            prefix = QStringLiteral("h");
        else if (type == QLatin1String("list-item"))
            prefix = QStringLiteral("l");
        else if (type == QLatin1String("table"))
            prefix = QStringLiteral("t");
        else if (type == QLatin1String("image"))
            prefix = QStringLiteral("img");
        else if (type == QLatin1String("break"))
            prefix = QStringLiteral("br");
        else if (type == QLatin1String("footnote"))
            prefix = QStringLiteral("fn");
        else if (type == QLatin1String("endnote"))
            prefix = QStringLiteral("en");
        else if (type == QLatin1String("toc"))
            prefix = QStringLiteral("toc");
        else if (type == QLatin1String("frame"))
            prefix = QStringLiteral("fr");
        else if (type == QLatin1String("shape"))
            prefix = QStringLiteral("sh");
        else if (type == QLatin1String("chart"))
            prefix = QStringLiteral("ch");
        else if (type == QLatin1String("qr"))
            prefix = QStringLiteral("qr");
        else if (type == QLatin1String("equation"))
            prefix = QStringLiteral("eq");
        else if (type == QLatin1String("form"))
            prefix = QStringLiteral("fm");
        else if (type == QLatin1String("biblio"))
            prefix = QStringLiteral("bib");
        return prefix + QString::number(++idNext_[prefix]);
    }

    bool addBlock(Block b)
    {
        if (doc_.body.size() >= kMaxBlocks) {
            fail(QStringLiteral("RTF document exceeds 100000 blocks"));
            return false;
        }
        if (b.id.isEmpty())
            b.id = autoId(b.type);
        doc_.body.push_back(std::move(b));
        return true;
    }

    // One pass over body[start..]. A per-note scan is quadratic when one
    // paragraph or one table flushes tens of thousands of notes together.
    QSet<QString> noteIdsInDocument(int start) const
    {
        QSet<QString> ids;
        auto collect = [&](const QVector<TextRun> &runs) {
            for (const TextRun &r : runs) {
                if (!r.noteId.isEmpty())
                    ids.insert(r.noteId);
            }
        };
        const int from = qMax(0, start);
        for (int i = from; i < doc_.body.size(); ++i) {
            const Block &b = doc_.body.at(i);
            collect(b.runs);
            for (const auto &row : b.table) {
                for (const TableCell &c : row)
                    collect(c.runs);
            }
        }
        return ids;
    }

    void resolveNotes(bool onlyInDocument)
    {
        if (pendingNotes_.isEmpty()) {
            noteScanFrom_ = doc_.body.size();
            return;
        }
        const int start = noteScanFrom_;
        const QSet<QString> inDoc = onlyInDocument ? noteIdsInDocument(start) : QSet<QString>{};
        QVector<PendingNote> now;
        QVector<PendingNote> later;
        now.reserve(pendingNotes_.size());
        for (const PendingNote &note : pendingNotes_) {
            if (!onlyInDocument || inDoc.contains(note.tempId))
                now.push_back(note);
            else
                later.push_back(note);
        }
        pendingNotes_ = std::move(later);
        QHash<QString, QString> idMap;
        idMap.reserve(now.size());
        for (const PendingNote &note : now) {
            Block nb;
            nb.type = note.endnote ? QStringLiteral("endnote") : QStringLiteral("footnote");
            nb.style = QStringLiteral("Normal");
            nb.align = QStringLiteral("left");
            QVector<TextRun> runs = note.runs;
            stripNote(runs);
            hoistLang(runs, nb);
            if (runs.isEmpty())
                runs.push_back(TextRun{});
            nb.runs = runs;
            if (!addBlock(nb)) {
                replaceNoteIds(idMap, start);
                return;
            }
            idMap.insert(note.tempId, doc_.body.last().id);
            attachRevisions(note.revs, doc_.body.last().id);
        }
        replaceNoteIds(idMap, start);
        if (pendingNotes_.isEmpty())
            noteScanFrom_ = doc_.body.size();
    }

    static void stripNote(QVector<TextRun> &runs)
    {
        // The note body's own terminal \par becomes a U+2029 (D1/D2); a trailing \line
        // is a '\n'. Both are the paragraph/line terminator, not body text, so drop
        // the trailing run of terminators (same rule the '\n'-only code had).
        auto trailing = [](QChar c) { return c == QLatin1Char('\n') || c == QChar(0x2029); };
        while (!runs.isEmpty() && !runs.last().text.isEmpty()
               && trailing(runs.last().text.at(runs.last().text.size() - 1))) {
            runs.last().text.chop(1);
            if (runs.last().text.isEmpty() && runs.last().field.isEmpty())
                runs.removeLast();
        }
        if (!runs.isEmpty() && runs.first().text.startsWith(QLatin1Char('\t'))) {
            runs.first().text.remove(0, 1);
            if (runs.first().text.isEmpty() && runs.first().field.isEmpty())
                runs.removeFirst();
        }
    }

    void replaceNoteIds(const QHash<QString, QString> &map, int start)
    {
        if (map.isEmpty())
            return;
        auto fix = [&](QVector<TextRun> &runs) {
            for (TextRun &r : runs) {
                if (r.noteId.isEmpty())
                    continue;
                const auto it = map.constFind(r.noteId);
                if (it != map.constEnd())
                    r.noteId = it.value();
            }
        };
        const int fromIndex = qMax(0, start);
        for (int i = fromIndex; i < doc_.body.size(); ++i) {
            Block &b = doc_.body[i];
            fix(b.runs);
            for (auto &row : b.table) {
                for (TableCell &c : row)
                    fix(c.runs);
            }
        }
        fix(bodyRuns_);
        for (TableCell &c : rowCells_)
            fix(c.runs);
        for (auto &row : table_.table) {
            for (TableCell &c : row)
                fix(c.runs);
        }
    }

    void finishNote()
    {
        bool endnote = false;
        if (!noteEnd_.isEmpty())
            endnote = noteEnd_.takeLast();
        QVector<TextRun> runs;
        QVector<Revision> revs;
        if (!noteRevStack_.isEmpty()) {
            if (!noteStack_.isEmpty())
                closeBuf(noteRevStack_.last(), noteStack_.last());
            revs = noteRevStack_.takeLast().revs;
        }
        if (!noteStack_.isEmpty())
            runs = noteStack_.takeLast();
        const QString temp = QStringLiteral("rtf-note-%1").arg(++noteSerial_);
        const int number = noteSerial_;
        PendingNote pn;
        pn.tempId = temp;
        pn.runs = runs;
        pn.endnote = endnote;
        pn.revs = revs;
        pendingNotes_.push_back(pn);
        TextRun mark;
        mark.text = QString::number(number);
        mark.superscript = true;
        mark.noteId = temp;
        if (stack_.size() >= 2)
            mark.lang = fmtOf(stack_.at(stack_.size() - 2).ch).lang;
        const Sink parent = stack_.size() >= 2 ? stack_.at(stack_.size() - 2).sink : Sink::Body;
        if (parent == Sink::Note && !noteStack_.isEmpty())
            noteStack_.last().push_back(mark);
        else if (parent == Sink::Result && !fields_.isEmpty())
            fields_.last().result.push_back(mark);
        else
            bodyRuns_.push_back(mark);
    }

    void finishField()
    {
        if (fields_.isEmpty())
            return;
        const FieldState field = fields_.takeLast();
        const Sink parent = stack_.size() >= 2 ? stack_.at(stack_.size() - 2).sink : Sink::Body;
        QVector<TextRun> *dest = nullptr;
        if (parent == Sink::Note && !noteStack_.isEmpty())
            dest = &noteStack_.last();
        else if (parent == Sink::Result && !fields_.isEmpty())
            dest = &fields_.last().result;
        else if (parent == Sink::Body || parent == Sink::Discard || parent == Sink::Pict)
            dest = &bodyRuns_;
        if (!dest)
            return;
        const QString inst = field.inst;
        int i = 0;
        skipFieldWs(inst, i);
        const int keyAt = i;
        while (i < inst.size() && inst.at(i).isLetter())
            ++i;
        const QString key = inst.mid(keyAt, i - keyAt).toUpper();
        if (key == QLatin1String("HYPERLINK")) {
            const QString url = hyperlinkTarget(inst, i);
            QVector<TextRun> runs = field.result;
            if (runs.isEmpty()) {
                TextRun r;
                r.text = url;
                r.link = url;
                runs.push_back(r);
            } else {
                for (TextRun &r : runs)
                    r.link = url;
            }
            for (const TextRun &r : runs)
                pushFieldRun(dest, r);
            return;
        }
        QString kind;
        QString arg;
        QString xrefKind;
        if (key == QLatin1String("PAGE"))
            kind = QStringLiteral("page_number");
        else if (key == QLatin1String("NUMPAGES"))
            kind = QStringLiteral("page_count");
        else if (key == QLatin1String("DATE"))
            kind = QStringLiteral("date");
        else if (key == QLatin1String("TIME"))
            kind = QStringLiteral("time");
        else if (key == QLatin1String("AUTHOR"))
            kind = QStringLiteral("author");
        else if (key == QLatin1String("TITLE"))
            kind = QStringLiteral("title");
        else if (key == QLatin1String("FILENAME")) {
            kind = QStringLiteral("filename");
            while (i < inst.size()) {
                const QString tok = readFieldToken(inst, i);
                if (tok.isEmpty())
                    break;
                if (tok.compare(QLatin1String("\\p"), Qt::CaseInsensitive) == 0)
                    kind = QStringLiteral("filepath");
            }
        }
        else if (key == QLatin1String("NUMWORDS"))
            kind = QStringLiteral("word_count");
        else if (key == QLatin1String("REF") || key == QLatin1String("PAGEREF")) {
            kind = QStringLiteral("xref");
            arg = readFieldArg(inst, i);
            if (key == QLatin1String("PAGEREF"))
                xrefKind = QStringLiteral("page");
        } else if (key == QLatin1String("MERGEFIELD")) {
            kind = QStringLiteral("merge");
            arg = readFieldArg(inst, i);
        } else if (key == QLatin1String("DOCVARIABLE")) {
            kind = QStringLiteral("var");
            arg = readFieldArg(inst, i);
        } else if (key == QLatin1String("FILLIN")) {
            kind = QStringLiteral("input");
            arg = readFieldArg(inst, i);
        } else if (key == QLatin1String("SEQ")) {
            kind = QStringLiteral("seq");
            const QString name = readFieldArg(inst, i);
            QString format = QStringLiteral("arabic");
            while (i < inst.size()) {
                const QString tok = readFieldToken(inst, i);
                if (tok.isEmpty())
                    break;
                if (tok != QLatin1String("\\*") && !tok.startsWith(QLatin1String("\\*")))
                    continue;
                QString sw = tok.mid(2);
                if (sw.isEmpty())
                    sw = readFieldToken(inst, i);
                if (sw.compare(QLatin1String("ROMAN"), Qt::CaseInsensitive) == 0)
                    format = QStringLiteral("roman");
                else if (sw.compare(QLatin1String("ALPHABETIC"), Qt::CaseInsensitive) == 0
                         || sw.compare(QLatin1String("ALPHA"), Qt::CaseInsensitive) == 0)
                    format = QStringLiteral("alpha");
                else if (sw.compare(QLatin1String("ARABIC"), Qt::CaseInsensitive) == 0)
                    format = QStringLiteral("arabic");
            }
            arg = seqFieldArg(name, format);
        }
        if (!kind.isEmpty()) {
            TextRun r;
            if (!field.result.isEmpty()) {
                r = field.result.first();
                r.text.clear();
                for (const TextRun &part : field.result)
                    r.text += part.text;
            }
            r.field = kind;
            r.fieldArg = arg;
            r.xrefKind = xrefKind;
            r.link.clear();
            r.noteId.clear();
            pushFieldRun(dest, r);
            return;
        }
        for (const TextRun &r : field.result)
            pushFieldRun(dest, r);
    }

    void pictAddChar(QChar ch)
    {
        const char c = ch.toLatin1();
        const int v = hexVal(static_cast<unsigned char>(c));
        if (v < 0)
            return;
        if (!pictHi_) {
            pictNibble_ = v;
            pictHi_ = true;
            return;
        }
        if (pict_.size() >= kMaxPict) {
            fail(QStringLiteral("RTF \\pict exceeds 8388608 bytes"));
            return;
        }
        pict_.append(static_cast<char>((pictNibble_ << 4) | v));
        pictHi_ = false;
    }

    void finishPict()
    {
        const bool png = pictPng_;
        const bool jpeg = pictJpeg_;
        const bool odd = pictHi_;
        const int goal = pictGoal_;
        QByteArray bytes = pict_;
        QString alt = pictAlt_.trimmed();
        QString name = pictName_.trimmed();
        pict_.clear();
        pictActive_ = false;
        pictPng_ = false;
        pictJpeg_ = false;
        pictHi_ = false;
        pictGoal_ = 0;
        pictAlt_.clear();
        pictName_.clear();
        pictSn_.clear();
        if (odd && (png || jpeg)) {
            fail(QStringLiteral("Malformed RTF: incomplete \\pict hex"));
            return;
        }
        if ((!png && !jpeg) || bytes.isEmpty())
            return;
        const bool inTable = stack_.size() >= 2 && stack_.at(stack_.size() - 2).para.inTable;
        if (inTable) {
            auto cleanLabel = [](QString s) {
                QString out;
                for (const QChar &c : s) {
                    if (c.category() != QChar::Other_Control)
                        out.append(c);
                }
                return out.trimmed();
            };
            QString label = cleanLabel(alt);
            if (label.isEmpty())
                label = cleanLabel(name);
            if (label.isEmpty())
                label = png ? QStringLiteral("png") : QStringLiteral("jpeg");
            const QString text = QStringLiteral("[imagem: %1]").arg(label);
            TextRun r;
            if (stack_.size() >= 2)
                fmtOf(stack_.at(stack_.size() - 2).ch).apply(r);
            r.text = text;
            appendExisting(bodyRuns_, r);
            warnings_ << QStringLiteral("RTF import: image in table cell replaced: %1").arg(text);
            return;
        }
        ParaState para;
        bool parentBody = true;
        if (stack_.size() >= 2) {
            para = stack_.at(stack_.size() - 2).para;
            parentBody = stack_.at(stack_.size() - 2).sink == Sink::Body;
        }
        if (parentBody)
            flushBodyParagraph(para, false);
        Block img;
        img.type = QStringLiteral("image");
        img.imageBytes = bytes;
        img.imageMime = png ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
        img.imageWidthMm = goal > 0 ? qRound(twipsToMm(goal)) : 40;
        if (img.imageWidthMm <= 0)
            img.imageWidthMm = 40;
        addBlock(img);
        suppressEmptyPar_ = true;
    }

    void commitName()
    {
        Frame &f = stack_.last();
        const QString name = f.buf.trimmed();
        f.buf.clear();
        f.nameDone = true;
        if (f.kind == Kind::FontEntry) {
            if (!name.isEmpty())
                fonts_.insert(f.fontIndex, name.left(512));
        } else if (f.kind == Kind::StyleEntry) {
            StyleInfo st;
            st.name = decodeStyleName(name).left(512);
            st.outline = f.para.outline;
            styles_.insert(f.styleIndex, st);
        }
    }

    void commitInfo()
    {
        const QString text = stack_.last().buf.trimmed();
        switch (stack_.last().info) {
        case InfoKind::Title:
            if (!text.isEmpty())
                doc_.title = text;
            break;
        case InfoKind::Author:
            doc_.author = text;
            break;
        case InfoKind::Subject:
            doc_.subject = text;
            break;
        case InfoKind::Keywords:
            doc_.keywords = text;
            break;
        case InfoKind::None:
            break;
        }
    }

    void commitBookmark()
    {
        const QString name = stack_.last().buf.trimmed();
        if (name.isEmpty())
            return;
        int offset = 0;
        for (const TextRun &r : bodyRuns_)
            offset += r.text.size();
        PendingBookmark bm;
        bm.name = name.left(256);
        bm.offset = offset;
        pendingBms_.push_back(bm);
    }

    void finishLooseBkmk()
    {
        if (looseBkmkKind_ == 0 || stack_.isEmpty())
            return;
        const int kind = looseBkmkKind_;
        looseBkmkKind_ = 0;
        Frame &f = stack_.last();
        if (kind == 1)
            commitBookmark();
        f.buf.clear();
        f.sink = Sink::Body;
    }

    void commitColor()
    {
        ColorSlot slot;
        slot.valid = colorSeen_;
        slot.r = cr_ < 0 ? 0 : cr_;
        slot.g = cg_ < 0 ? 0 : cg_;
        slot.b = cb_ < 0 ? 0 : cb_;
        colors_.push_back(slot);
        cr_ = cg_ = cb_ = -1;
        colorSeen_ = false;
    }

    void finishListLevel()
    {
        if (levelStack_.isEmpty())
            return;
        const ListLevel level = levelStack_.takeLast();
        if (!listStack_.isEmpty()) {
            if (listStack_.last().levels.size() < 32)
                listStack_.last().levels.push_back(level);
        }
    }

    void finishList()
    {
        if (listStack_.isEmpty())
            return;
        const ListDef list = listStack_.takeLast();
        if (list.id >= 0)
            lists_.insert(list.id, list);
    }

    void finishOverride()
    {
        if (ovStack_.isEmpty())
            return;
        const OverrideBuild ov = ovStack_.takeLast();
        if (ov.ls > 0 && ov.listId >= 0)
            overrides_.insert(ov.ls, ov.listId);
    }

    bool paragraphPending() const
    {
        return !bodyRuns_.isEmpty() || !pendingBms_.isEmpty() || !paraComments_.isEmpty() || paraCarrier_
            || paraRev_.open.open || paraRev_.side.open || !paraRev_.revs.isEmpty();
    }

    RevMark markOf(const CharState &ch) const
    {
        RevMark m;
        if (ch.deleted) {
            m.kind = RevKind::Delete;
            if (ch.revauthdel >= 0)
                m.author = ch.revauthdel;
            else if (ch.revauth >= 0)
                m.author = ch.revauth;
            if (ch.revdttmdelSet) {
                m.dateSet = true;
                m.dateRaw = ch.revdttmdel;
                m.dateBad = ch.revdttmdelBad;
            } else if (ch.revdttmSet) {
                m.dateSet = true;
                m.dateRaw = ch.revdttm;
                m.dateBad = ch.revdttmBad;
            }
            return m;
        }
        if (ch.revised) {
            m.kind = RevKind::Insert;
            m.author = ch.revauth >= 0 ? ch.revauth : 0;
            if (ch.revdttmSet) {
                m.dateSet = true;
                m.dateRaw = ch.revdttm;
                m.dateBad = ch.revdttmBad;
            }
            return m;
        }
        if (ch.crauth >= 0 || ch.crdateSet) {
            m.kind = RevKind::Format;
            m.author = ch.crauth >= 0 ? ch.crauth : 0;
            if (ch.crdateSet) {
                m.dateSet = true;
                m.dateRaw = ch.crdate;
                m.dateBad = ch.crdateBad;
            }
        }
        return m;
    }

    int storeAuth(qint64 v, bool has, bool overflow) const
    {
        int iv = 0;
        if (!fitsInt(v, has, overflow, &iv) || iv < 0)
            return std::numeric_limits<int>::max();
        return iv;
    }

    void storeDttm(bool &setFlag, qint64 &raw, bool &bad, qint64 v, bool has, bool overflow) const
    {
        setFlag = true;
        if (!has || overflow || v < 0 || v > 0xFFFFFFFFLL) {
            bad = true;
            raw = 0;
            return;
        }
        raw = v;
        qint64 unused = 0;
        bad = !dttmToUnixMs(v, &unused);
    }

    bool sameMark(const OpenRev &o, const RevMark &m) const
    {
        return o.open && o.kind == m.kind && o.author == m.author && o.dateSet == m.dateSet && o.dateRaw == m.dateRaw
            && o.dateBad == m.dateBad;
    }

    QString authorAt(int index)
    {
        if (index < 0)
            index = 0;
        if (index >= revAuthors_.size()) {
            if (!warnedAuthorIdx_.contains(index)) {
                warnedAuthorIdx_.insert(index);
                warnings_ << QStringLiteral("RTF import: revision author index %1 is outside the table").arg(index);
            }
            return QStringLiteral("Unknown");
        }
        const QString name = revAuthors_.at(index).trimmed();
        return name.isEmpty() ? QStringLiteral("Unknown") : name;
    }

    qint64 stampOf(bool dateSet, qint64 raw, bool bad)
    {
        if (!dateSet)
            return 0;
        qint64 ms = 0;
        if (bad || !dttmToUnixMs(raw, &ms)) {
            if (!warnedBadDate_) {
                warnedBadDate_ = true;
                warnings_ << QStringLiteral("RTF import: invalid revision date");
            }
            return 0;
        }
        return ms;
    }

    QString sliceText(const QVector<TextRun> &runs, int start, int end) const
    {
        QString all;
        for (const TextRun &r : runs)
            all += r.text;
        if (start < 0)
            start = 0;
        if (end < start)
            end = start;
        if (start > all.size())
            return {};
        return all.mid(start, end - start);
    }

    RevMark formatMarkOf(const CharState &ch) const
    {
        RevMark m;
        if (ch.deleted)
            return m;
        if (ch.crauth >= 0 || ch.crdateSet) {
            m.kind = RevKind::Format;
            m.author = ch.crauth >= 0 ? ch.crauth : 0;
            if (ch.crdateSet) {
                m.dateSet = true;
                m.dateRaw = ch.crdate;
                m.dateBad = ch.crdateBad;
            }
        }
        return m;
    }

    RevBuf &revBufForStack()
    {
        for (int i = stack_.size() - 1; i >= 0; --i) {
            if (stack_.at(i).kind == Kind::Footnote && !noteRevStack_.isEmpty())
                return noteRevStack_.last();
        }
        return paraRev_;
    }

    QVector<TextRun> &runsForStack()
    {
        for (int i = stack_.size() - 1; i >= 0; --i) {
            if (stack_.at(i).kind == Kind::Footnote && !noteStack_.isEmpty())
                return noteStack_.last();
        }
        return bodyRuns_;
    }

    bool continuesEarlierFormat(const RevBuf &buf, const OpenRev &slot, const QVector<TextRun> &runs)
    {
        if (!slot.open || slot.kind != RevKind::Format || slot.start >= runChars(runs))
            return false;
        const QString author = authorAt(slot.author);
        const qint64 stamp = stampOf(slot.dateSet, slot.dateRaw, slot.dateBad);
        for (int i = buf.revs.size() - 1; i >= 0; --i) {
            const Revision &prev = buf.revs.at(i);
            if (prev.kind == QLatin1String("format") && prev.end == slot.start && prev.author == author
                && prev.timestamp == stamp)
                return true;
        }
        return false;
    }

    // A delete point does not consume live characters, so two slices of the same
    // insert or format change still abut and stay one revision (D5).
    bool absorbAbutting(QVector<Revision> &revs, Revision &rv) const
    {
        if (rv.kind != QLatin1String("insert") && rv.kind != QLatin1String("format"))
            return false;
        for (int i = revs.size() - 1; i >= 0; --i) {
            Revision &prev = revs[i];
            if (prev.kind != rv.kind || prev.author != rv.author || prev.timestamp != rv.timestamp)
                continue;
            if (prev.end != rv.start)
                continue;
            // A second {\*\aiorevfmt} is a different format change that happens to
            // start where the previous one ended. A continuation slice has none.
            if (rv.kind == QLatin1String("format") && (!rv.payload.isEmpty() || !rv.priorRuns.isEmpty()))
                continue;
            prev.end = rv.end;
            if (rv.kind == QLatin1String("insert"))
                prev.text += rv.text;
            else {
                if (prev.payload.isEmpty())
                    prev.payload = rv.payload;
                if (prev.priorRuns.isEmpty() && !rv.priorRuns.isEmpty())
                    prev.priorRuns = rv.priorRuns;
            }
            return true;
        }
        return false;
    }

    void commitSlot(RevBuf &buf, OpenRev &slot, const QVector<TextRun> &runs)
    {
        if (!slot.open)
            return;
        const OpenRev o = slot;
        slot = OpenRev{};
        Revision rv;
        rv.author = authorAt(o.author);
        rv.timestamp = stampOf(o.dateSet, o.dateRaw, o.dateBad);
        rv.start = o.start;
        if (o.kind == RevKind::Delete) {
            rv.kind = QStringLiteral("delete");
            rv.end = o.start;
            rv.deletedRuns = o.deletedRuns;
            for (const TextRun &r : o.deletedRuns)
                rv.text += r.text;
            if (rv.text.isEmpty())
                return;
        } else if (o.kind == RevKind::Insert) {
            rv.kind = QStringLiteral("insert");
            rv.end = runChars(runs);
            if (rv.end < rv.start)
                rv.end = rv.start;
            rv.text = sliceText(runs, rv.start, rv.end);
            if (rv.end == rv.start)
                return;
        } else {
            rv.kind = QStringLiteral("format");
            rv.end = runChars(runs);
            if (rv.end < rv.start)
                rv.end = rv.start;
            rv.payload = o.payload;
            rv.priorRuns = o.priorRuns;
            if (rv.end == rv.start && rv.payload.isEmpty() && rv.priorRuns.isEmpty())
                return;
        }
        if (!absorbAbutting(buf.revs, rv))
            buf.revs.push_back(rv);
    }

    void closeBuf(RevBuf &buf, const QVector<TextRun> &runs)
    {
        commitSlot(buf, buf.side, runs);
        commitSlot(buf, buf.open, runs);
    }

    void takePendingFmt(RevBuf &buf, OpenRev &slot)
    {
        if (slot.kind != RevKind::Format || slot.tookFmt || !buf.pendingFmtSet)
            return;
        slot.payload = buf.pendingPayload;
        slot.priorRuns = buf.pendingPrior;
        slot.tookFmt = true;
        buf.pendingFmtSet = false;
        buf.pendingPayload.clear();
        buf.pendingPrior.clear();
    }

    void syncSlot(RevBuf &buf, OpenRev &slot, const RevMark &mark, const QVector<TextRun> &runs)
    {
        if (mark.kind == RevKind::None) {
            commitSlot(buf, slot, runs);
            return;
        }
        if (sameMark(slot, mark))
            return;
        commitSlot(buf, slot, runs);
        slot.open = true;
        slot.kind = mark.kind;
        slot.author = mark.author;
        slot.dateSet = mark.dateSet;
        slot.dateRaw = mark.dateRaw;
        slot.dateBad = mark.dateBad;
        slot.start = runChars(runs);
        takePendingFmt(buf, slot);
    }

    void syncPair(RevBuf &buf, const RevMark &mark, const RevMark &fmt, const QVector<TextRun> &runs)
    {
        syncSlot(buf, buf.open, mark, runs);
        if (mark.kind == RevKind::Insert && fmt.kind == RevKind::Format)
            syncSlot(buf, buf.side, fmt, runs);
        else
            commitSlot(buf, buf.side, runs);
    }

    void warnCellRev()
    {
        if (warnedCellRev_)
            return;
        warnedCellRev_ = true;
        warnings_ << QStringLiteral("RTF import: revision markup in a table cell was discarded");
    }

    void appendRevised(RevBuf &buf, QVector<TextRun> &runs, const TextRun &incoming, const CharState &ch,
                       bool inTable, bool bodyText)
    {
        const RevMark mark = markOf(ch);
        const RevMark fmt = formatMarkOf(ch);
        if (inTable && (mark.kind != RevKind::None || fmt.kind != RevKind::None)) {
            warnCellRev();
            if (mark.kind == RevKind::Delete)
                return;
            if (bodyText && !incoming.text.isEmpty())
                suppressEmptyPar_ = false;
            appendExisting(runs, incoming);
            return;
        }
        syncPair(buf, mark, fmt, runs);
        if (mark.kind == RevKind::Delete) {
            TextRun copy = incoming;
            if (copy.redact)
                copy.text.clear();
            appendExisting(buf.open.deletedRuns, copy);
            return;
        }
        if (bodyText && !incoming.text.isEmpty())
            suppressEmptyPar_ = false;
        appendExisting(runs, incoming);
    }

    void appendBodyRun(const TextRun &incoming)
    {
        const bool inTable = !stack_.isEmpty() && stack_.last().para.inTable;
        const CharState ch = stack_.isEmpty() ? CharState{} : stack_.last().ch;
        appendRevised(paraRev_, bodyRuns_, incoming, ch, inTable, true);
    }

    void deliverFieldRun(const TextRun &incoming)
    {
        const bool inTable = !stack_.isEmpty() && stack_.last().para.inTable;
        const CharState ch = stack_.isEmpty() ? CharState{} : stack_.last().ch;
        const RevMark mark = markOf(ch);
        const RevMark fmt = formatMarkOf(ch);
        if (inTable && (mark.kind != RevKind::None || fmt.kind != RevKind::None)) {
            warnCellRev();
            if (mark.kind == RevKind::Delete)
                return;
            if (!incoming.text.isEmpty())
                suppressEmptyPar_ = false;
            bodyRuns_.push_back(incoming);
            return;
        }
        syncPair(paraRev_, mark, fmt, bodyRuns_);
        if (mark.kind == RevKind::Delete) {
            TextRun copy = incoming;
            if (copy.redact)
                copy.text.clear();
            if (!copy.text.isEmpty() || !copy.field.isEmpty())
                paraRev_.open.deletedRuns.push_back(copy);
            return;
        }
        if (!incoming.text.isEmpty())
            suppressEmptyPar_ = false;
        bodyRuns_.push_back(incoming);
    }

    void pushFieldRun(QVector<TextRun> *dest, const TextRun &run)
    {
        if (!dest)
            return;
        if (dest == &bodyRuns_)
            deliverFieldRun(run);
        else
            dest->push_back(run);
    }

    void attachRevisions(const QVector<Revision> &revs, const QString &blockId)
    {
        for (Revision rv : revs) {
            rv.blockId = blockId;
            rv.id = nextRevisionId(doc_.revisions);
            doc_.revisions.push_back(rv);
            doc_.trackChanges = true;
        }
    }

    void attachParaRevs(const QString &blockId)
    {
        attachRevisions(paraRev_.revs, blockId);
        paraRev_ = RevBuf{};
    }

    QStringList commentIds() const
    {
        QStringList ids;
        auto take = [&](const QVector<Comment> &cs) {
            for (const Comment &c : cs) {
                if (!c.id.isEmpty())
                    ids.append(c.id);
            }
        };
        for (const Block &b : doc_.body)
            take(b.comments);
        take(table_.comments);
        take(paraComments_);
        take(tableComments_);
        return ids;
    }

    void ensureCommentId(Comment &c)
    {
        if (!c.id.isEmpty())
            return;
        c.id = nextPrefixedId(QStringLiteral("c"), commentIds());
    }

    void resolveCommentParents()
    {
        QSet<QString> ids;
        for (const Block &b : doc_.body) {
            for (const Comment &c : b.comments) {
                if (!c.id.isEmpty())
                    ids.insert(c.id);
            }
        }
        for (Block &b : doc_.body) {
            for (Comment &c : b.comments) {
                if (c.parentId.isEmpty() || ids.contains(c.parentId))
                    continue;
                c.parentId.clear();
                if (!warnedCommentParent_) {
                    warnedCommentParent_ = true;
                    warnings_ << QStringLiteral("RTF import: comment parent not found");
                }
            }
        }
    }

    void onChatn()
    {
        if (stack_.isEmpty() || stack_.last().sink != Sink::Body || stack_.last().skipAll)
            return;
        Comment c;
        c.author = !pendingAuthor_.isEmpty() ? pendingAuthor_ : pendingInitials_;
        pendingAuthor_.clear();
        pendingInitials_.clear();
        if (stack_.last().para.inTable) {
            tableComments_.push_back(c);
            annWhere_ = AnnWhere::Table;
            annStub_ = tableComments_.size() - 1;
        } else {
            paraComments_.push_back(c);
            annWhere_ = AnnWhere::Para;
            annStub_ = paraComments_.size() - 1;
        }
    }

    void beginAnnotation()
    {
        annActive_ = true;
        annText_.clear();
        annId_.clear();
        annParent_.clear();
        annAuthor_.clear();
        annResolved_ = false;
        annResolvedSet_ = false;
        stack_.last().kind = Kind::Annotation;
        stack_.last().sink = Sink::AnnText;
    }

    void finishAnnotation()
    {
        Comment *stub = nullptr;
        if (annWhere_ == AnnWhere::Table && annStub_ >= 0 && annStub_ < tableComments_.size())
            stub = &tableComments_[annStub_];
        else if (annWhere_ == AnnWhere::Para && annStub_ >= 0 && annStub_ < paraComments_.size())
            stub = &paraComments_[annStub_];
        Comment local;
        const bool owned = stub == nullptr;
        if (owned) {
            local.author = !annAuthor_.isEmpty() ? annAuthor_
                                                  : (!pendingAuthor_.isEmpty() ? pendingAuthor_ : pendingInitials_);
            stub = &local;
        } else if (!annAuthor_.isEmpty()) {
            stub->author = annAuthor_;
        }
        stub->text = annText_;
        if (!annId_.isEmpty())
            stub->id = annId_;
        if (!annParent_.isEmpty())
            stub->parentId = annParent_;
        if (annResolvedSet_)
            stub->resolved = annResolved_;
        ensureCommentId(*stub);
        if (owned) {
            const bool inTable = !stack_.isEmpty() && stack_.last().para.inTable;
            if (inTable)
                tableComments_.push_back(local);
            else
                paraComments_.push_back(local);
        }
        annActive_ = false;
        annWhere_ = AnnWhere::None;
        annStub_ = -1;
        annText_.clear();
        annId_.clear();
        annParent_.clear();
        annAuthor_.clear();
        annResolved_ = false;
        annResolvedSet_ = false;
        pendingAuthor_.clear();
        pendingInitials_.clear();
    }

    void commitAtnAuthor()
    {
        const QString name = stack_.last().buf.trimmed();
        if (annActive_)
            annAuthor_ = name;
        else
            pendingAuthor_ = name;
    }

    void commitAtnId()
    {
        if (!annActive_)
            pendingInitials_ = stack_.last().buf.trimmed();
    }

    void commitAnnProp()
    {
        const QString tag = stack_.last().tag;
        const QString val = stack_.last().buf.trimmed();
        if (tag == QLatin1String("aiocid"))
            annId_ = val;
        else if (tag == QLatin1String("aiocparent"))
            annParent_ = val;
        else if (tag == QLatin1String("aiocresolved")) {
            annResolvedSet_ = true;
            annResolved_ = val == QLatin1String("1") || val == QLatin1String("true");
        }
    }

    void commitRevAuthor()
    {
        Frame &f = stack_.last();
        if (f.nameDone)
            return;
        const QString name = f.buf.trimmed();
        f.buf.clear();
        f.nameDone = true;
        revAuthors_.push_back(name);
    }

    void finishAioRevFmt()
    {
        const QByteArray raw = stack_.last().buf.toUtf8();
        QJsonParseError err;
        const QJsonDocument parsed = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !parsed.isObject()) {
            if (!warnedBadJson_) {
                warnedBadJson_ = true;
                warnings_ << QStringLiteral("RTF import: invalid aiorevfmt JSON ignored");
            }
            return;
        }
        const QJsonObject o = parsed.object();
        QString payload;
        const QJsonValue pv = o.value(QStringLiteral("payload"));
        if (pv.isObject())
            payload = QString::fromUtf8(QJsonDocument(pv.toObject()).toJson(QJsonDocument::Compact));
        else if (pv.isArray())
            payload = QString::fromUtf8(QJsonDocument(pv.toArray()).toJson(QJsonDocument::Compact));
        else if (!pv.isUndefined() && !pv.isNull())
            payload = pv.toString();
        QJsonArray priorArr;
        if (o.contains(QStringLiteral("priorRuns")))
            priorArr = o.value(QStringLiteral("priorRuns")).toArray();
        else if (o.contains(QStringLiteral("prior_runs")))
            priorArr = o.value(QStringLiteral("prior_runs")).toArray();
        QVector<TextRun> prior;
        for (const QJsonValue &v : priorArr) {
            if (!v.isObject())
                continue;
            TextRun run = TextRun::fromJson(v.toObject());
            if (run.redact)
                run.text.clear();
            prior.push_back(run);
        }
        RevBuf &buf = revBufForStack();
        QVector<TextRun> &runs = runsForStack();
        bool gave = false;
        bool closed = false;
        auto hand = [&](OpenRev &slot) {
            if (!slot.open || slot.kind != RevKind::Format)
                return;
            // The JSON belongs to this still-empty or not-yet-marked slice.
            // A slice that already continues an earlier format must be closed
            // first, or it would swallow the next change's {\*\aiorevfmt}.
            if (!slot.tookFmt && !continuesEarlierFormat(buf, slot, runs)) {
                slot.payload = payload;
                slot.priorRuns = prior;
                slot.tookFmt = true;
                gave = true;
                return;
            }
            commitSlot(buf, slot, runs);
            closed = true;
        };
        hand(buf.open);
        hand(buf.side);
        if (gave)
            return;
        if (buf.pendingFmtSet && !closed)
            return;
        buf.pendingFmtSet = true;
        buf.pendingPayload = payload;
        buf.pendingPrior = prior;
    }

    bool acceptedShape(const ShapeBuild &s) const
    {
        return s.typeSet && (s.shapeType == 1 || s.shapeType == 3 || s.shapeType == 20);
    }

    Sink sinkOutsideShape() const
    {
        for (int i = stack_.size() - 2; i >= 0; --i) {
            const Kind k = stack_.at(i).kind;
            if (k == Kind::Shp || k == Kind::ShpInst || k == Kind::ShpTxt || k == Kind::Sp || k == Kind::SpName
                || k == Kind::SpVal)
                continue;
            return stack_.at(i).sink;
        }
        return Sink::Body;
    }

    void applyShapeProp(const QString &name, const QString &value)
    {
        if (shapeStack_.isEmpty())
            return;
        ShapeBuild &s = shapeStack_.last();
        const QString key = name.trimmed().toLower();
        bool ok = false;
        const qint64 n = value.trimmed().toLongLong(&ok);
        if (key == QLatin1String("shapetype")) {
            if (ok && n <= std::numeric_limits<int>::max() && n >= std::numeric_limits<int>::min()) {
                s.typeSet = true;
                s.shapeType = static_cast<int>(n);
            }
        } else if (key == QLatin1String("fillcolor") && ok) {
            s.fillColorSet = true;
            s.fillColor = n;
        } else if (key == QLatin1String("linecolor") && ok) {
            s.lineColorSet = true;
            s.lineColor = n;
        } else if (key == QLatin1String("ffilled") && ok)
            s.fFilled = static_cast<int>(n);
        else if (key == QLatin1String("fline") && ok)
            s.fLine = static_cast<int>(n);
    }

    void applyShapeGeometry(const QString &word, qint64 v, bool has, bool overflow)
    {
        if (shapeStack_.isEmpty() || !has || overflow)
            return;
        ShapeBuild &s = shapeStack_.last();
        if (word == QLatin1String("shpleft")) {
            s.left = v;
            s.leftSet = true;
        } else if (word == QLatin1String("shptop")) {
            s.top = v;
            s.topSet = true;
        } else if (word == QLatin1String("shpright")) {
            s.right = v;
            s.rightSet = true;
        } else if (word == QLatin1String("shpbottom")) {
            s.bottom = v;
            s.bottomSet = true;
        } else if (word == QLatin1String("shpz")) {
            int iv = 0;
            if (fitsInt(v, has, overflow, &iv)) {
                s.z = iv;
                s.zSet = true;
            }
        }
    }

    double clampShapeMm(qint64 twips, bool &clamped) const
    {
        if (twips < 0) {
            clamped = true;
            twips = 0;
        }
        if (twips > 2000000000LL)
            twips = 2000000000LL;
        double mm = std::round(static_cast<double>(twips) * 25.4 / 1440.0 * 100.0) / 100.0;
        if (mm < 0) {
            clamped = true;
            mm = 0;
        }
        if (mm > 10000.0) {
            clamped = true;
            mm = 10000.0;
        }
        return mm;
    }

    void finishShape()
    {
        if (shapeStack_.isEmpty())
            return;
        const ShapeBuild s = shapeStack_.takeLast();
        const bool inTable = !stack_.isEmpty() && stack_.last().para.inTable;
        if (!acceptedShape(s)) {
            const QString label = s.typeSet ? QString::number(s.shapeType) : QStringLiteral("missing");
            if (!warnedShapeType_.contains(label)) {
                warnedShapeType_.insert(label);
                warnings_ << QStringLiteral("RTF import: shape type %1 ignored").arg(label);
            }
            return;
        }
        if (inTable) {
            if (!warnedShapeInTable_) {
                warnedShapeInTable_ = true;
                warnings_ << QStringLiteral("RTF import: shape in a table cell was ignored");
            }
            return;
        }
        bool clamped = false;
        const bool hasW = s.leftSet || s.rightSet;
        const bool hasH = s.topSet || s.bottomSet;
        const double widthMm = hasW ? clampShapeMm(s.right - s.left, clamped) : 0;
        const double heightMm = hasH ? clampShapeMm(s.bottom - s.top, clamped) : 0;
        if (clamped && !warnedShapeSize_) {
            warnedShapeSize_ = true;
            warnings_ << QStringLiteral("RTF import: shape size clamped");
        }
        ParaState para;
        bool parentBody = true;
        if (stack_.size() >= 2) {
            para = stack_.at(stack_.size() - 2).para;
            const Sink parentSink = stack_.at(stack_.size() - 2).sink;
            parentBody = parentSink == Sink::Body;
        }
        if (parentBody && !para.inTable)
            flushBodyParagraph(para, false);
        Block b;
        b.type = QStringLiteral("shape");
        b.imageWidthMm = 0;
        if (s.shapeType == 3)
            b.shapeKind = QStringLiteral("ellipse");
        else if (s.shapeType == 20)
            b.shapeKind = QStringLiteral("line");
        else
            b.shapeKind = QStringLiteral("rect");
        if (widthMm > 0)
            b.imageWidthMm = qRound(widthMm);
        b.heightMm = heightMm;
        if (s.zSet)
            b.zIndex = s.z;
        const bool fillOn = s.fFilled == 1 || (s.fFilled < 0 && s.fillColorSet);
        if (fillOn && s.fillColorSet)
            b.fill = colorRefHex(s.fillColor);
        const bool lineOn = s.fLine == 1 || (s.fLine < 0 && s.lineColorSet);
        if (lineOn && s.lineColorSet)
            b.stroke = colorRefHex(s.lineColor);
        b.runs = s.runs;
        // {\shptxt ...\par} uses \par as the paragraph mark. The model text does
        // not include that terminator; a newline inside the text is \line.
        if (!b.runs.isEmpty() && b.runs.last().text.endsWith(QLatin1Char('\n'))) {
            b.runs.last().text.chop(1);
            if (b.runs.last().text.isEmpty() && b.runs.last().field.isEmpty())
                b.runs.removeLast();
        }
        if (b.runs.isEmpty())
            b.runs.push_back(TextRun{});
        addBlock(b);
        suppressEmptyPar_ = true;
    }

    void applyPage()
    {
        if (paperW_ > 0 && paperH_ > 0) {
            const bool a4 = std::abs(paperW_ - 11906) < 80 && std::abs(paperH_ - 16838) < 80;
            const bool letter = std::abs(paperW_ - 12240) < 80 && std::abs(paperH_ - 15840) < 80;
            if (a4)
                doc_.page.size = QStringLiteral("A4");
            else if (letter)
                doc_.page.size = QStringLiteral("Letter");
            else {
                doc_.page.size = QStringLiteral("custom");
                doc_.page.widthMm = twipsToMm(paperW_);
                doc_.page.heightMm = twipsToMm(paperH_);
            }
            if (paperW_ > paperH_)
                doc_.page.orientation = QStringLiteral("landscape");
        }
        if (margT_ >= 0)
            doc_.page.marginsMm[0] = twipsToMm(margT_);
        if (margR_ >= 0)
            doc_.page.marginsMm[1] = twipsToMm(margR_);
        if (margB_ >= 0)
            doc_.page.marginsMm[2] = twipsToMm(margB_);
        if (margL_ >= 0)
            doc_.page.marginsMm[3] = twipsToMm(margL_);
    }
};

} // namespace

bool loadRtfFile(const QString &path, Document &doc, QString *error, QStringList *warnings)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot read ") + path;
        return false;
    }
    if (f.size() > qint64(32) * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("RTF file exceeds 33554432 bytes");
        return false;
    }
    const QByteArray data = f.readAll();
    Document parsed;
    if (!RtfParser().parse(data, parsed, error, warnings))
        return false;
    parsed.fileName = QFileInfo(path).fileName();
    parsed.normalizeExternalParaSeps();
    doc = parsed;
    return true;
}

} // namespace aioffice
