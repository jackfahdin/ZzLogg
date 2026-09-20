#include <QtTest>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QFile>
#include "abstractlogview.h"
#include "configuration.h"
#include "logdata.h"
#include "highlighterset.h"
#include "quickfindpattern.h"
#include "storagecontext.h"
#include "filteredview.h"
#include "mainwindow.h"
#include "crawlerwidget.h"
#include "logmainview.h"
#include "session.h"

struct TextCacheCrawlerAccess {};
template <> struct CrawlerWidget::access_by<TextCacheCrawlerAccess> {
    static bool loaded(const CrawlerWidget& crawler) { return !crawler.loadingInProgress_; }
    static auto matches(const CrawlerWidget& crawler) { return crawler.nbMatches_.get(); }
};
using CrawlerAccess = CrawlerWidget::access_by<TextCacheCrawlerAccess>;

// Exercise the real shared renderer and real file-backed data without a main window.
class CacheView final : public AbstractLogView {
public:
    CacheView(const LogData* data, const QuickFindPattern* pattern)
        : AbstractLogView(data, pattern) {}
protected:
    AbstractLogData::LineType lineType(LineNumber) const override {
        return AbstractLogData::LineTypeFlags::Plain;
    }
};

class TextCacheTest final : public QObject {
    Q_OBJECT
    QTemporaryDir root_;
    std::unique_ptr<LogData> data_;
    QuickFindPattern pattern_;
    std::unique_ptr<AbstractLogView> view_;
    std::unique_ptr<LogFilteredData> filtered_;
    QString file_;

    QImage frame() {
        QApplication::processEvents();
        view_->viewport()->repaint();
        return view_->viewport()->grab().toImage();
    }
    void matchesFull() {
        const auto actual = frame();
        view_->forceRefresh();
        const auto expected = frame();
        if (actual != expected) {
            QRect delta;
            int count = 0;
            for (int y = 0; y < expected.height(); ++y)
                for (int x = 0; x < expected.width(); ++x)
                    if (actual.pixel(x, y) != expected.pixel(x, y)) {
                        delta |= QRect(x, y, 1, 1); ++count;
                    }
            qInfo() << QTest::currentTestFunction() << "pixel difference" << count << delta;
        }
        QCOMPARE(actual, expected);
    }
    void scrollSequence() {
        const QList<int> positions{101, 102, 99, 103, 110, 109, 108, 107, 106, 105, 100};
        QList<QImage> reference;
        for (int p : positions) {
            view_->verticalScrollBar()->setValue(p);
            view_->forceRefresh();
            reference.append(frame());
        }
        view_->verticalScrollBar()->setValue(100);
        view_->forceRefresh();
        frame();
        for (int i = 0; i < positions.size(); ++i) {
            view_->verticalScrollBar()->setValue(positions[i]);
            QCOMPARE(frame(), reference[i]);
        }
    }
private Q_SLOTS:
    void initTestCase() {
        QVERIFY(root_.isValid());
        QVERIFY(StorageContext::install({StorageMode::CustomDirectory, root_.path(),
            root_.filePath("storage.ini"), true}));
        Configuration::getSynced().setUseTextWrap(false);
        HighlighterSetCollection::getSynced();
    }
    void init() {
        file_ = root_.filePath("cache.log");
        QFile file(file_);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        for (int i = 0; i < 500; ++i)
            file.write(QString("row %1 ERROR marker\t%2 中文日志\n")
                .arg(i, 4, 10, QLatin1Char('0')).arg(QString(180, QLatin1Char('x'))).toUtf8());
        file.close();
        data_ = std::make_unique<LogData>();
        QSignalSpy loaded(data_.get(), &LogData::loadingFinished);
        data_->attachFile(file_);
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        data_->setDisplayEncoding("UTF-8");
        pattern_.changeSearchPattern("");
        view_ = std::make_unique<CacheView>(data_.get(), &pattern_);
        view_->resize(900, 510);
        view_->updateFont(QFont("Consolas", 10));
        view_->updateData();
        view_->setLineNumbersVisible(true);
        view_->show();
        QVERIFY(QTest::qWaitForWindowExposed(view_.get()));
        view_->verticalScrollBar()->setValue(100);
        view_->forceRefresh();
        frame();
    }
    void cleanup() {
        view_.reset();
        filtered_.reset();
        data_.reset();
    }
    void syntaxRenderingAndOverlayPriority() {
        CodeSyntax syntax([&](quint64 line) { return data_->getLineString(LineNumber{line}); },
                          [&] { return quint64(data_->getNbLine().get()); });
        const auto plain = frame();
        view_->setCodeSyntax(&syntax);
        syntax.setLanguage("cpp");
        QTRY_VERIFY(!syntax.formats(100, false).isEmpty());
        QTRY_VERIFY(frame() != plain);
        // Compare stable cached frames after all rows in this scroll range
        // have completed their asynchronous syntax pass.
        for (quint64 line = 99; line < 180; ++line) {
            QTRY_VERIFY(!syntax.formats(line, false).isEmpty());
        }
        scrollSequence();
        view_->selectAll();
        const auto selected = frame();
        syntax.setLanguage("plain");
        QCOMPARE(frame(), selected);
        syntax.setLanguage("cpp");
        QTRY_VERIFY(!syntax.formats(100, false).isEmpty());
        QCOMPARE(frame(), selected);
    }
    void syntaxUsesOriginalFilteredState() {
        view_.reset();
        QFile file(file_);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("/* hidden opener\nint ERROR = 1;\n*/\nint ERROR = 2;\n");
        file.close();
        QSignalSpy loaded(data_.get(), &LogData::loadingFinished);
        data_->reload();
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        data_->setDisplayEncoding("UTF-8");
        CodeSyntax syntax([&](quint64 line) { return data_->getLineString(LineNumber{line}); },
                          [&] { return quint64(data_->getNbLine().get()); });
        syntax.setLanguage("cpp");
        filtered_ = std::make_unique<LogFilteredData>(data_.get());
        filtered_->runSearch(RegularExpressionPattern("ERROR", true, false, false, true));
        QTRY_COMPARE_WITH_TIMEOUT(filtered_->getNbMatches().get(), 2, 10000);
        view_ = std::make_unique<FilteredView>(filtered_.get(), &pattern_);
        view_->resize(900, 510);
        view_->setCodeSyntax(&syntax);
        view_->updateFont(QFont("Consolas", 12));
        view_->updateData();
        view_->show();
        QTRY_VERIFY(!syntax.formats(1, false).isEmpty());
        const auto comment = syntax.formats(1, view_->palette().color(QPalette::Base).lightness() < 128).front().color;
        const auto hasCommentColor = [&] {
            const auto img = frame();
            for (int y = 0; y < QFontMetrics(view_->font()).height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (img.pixelColor(x, y) == comment) return true;
            return false;
        };
        QTRY_VERIFY(hasCommentColor());
        matchesFull();
        syntax.setLanguage("plain");
        QVERIFY(!hasCommentColor());
    }
    void syntaxMenuControlsActiveDocument() {
        MainWindow window{WindowSession{std::make_shared<Session>(), "syntax-menu", 0}};
        window.loadFileNonInteractive(file_);
        auto* crawler = window.findChild<CrawlerWidget*>();
        QVERIFY(crawler);
        QTRY_VERIFY_WITH_TIMEOUT(CrawlerAccess::loaded(*crawler), 10000);
        auto* menu = window.findChild<QMenu*>("syntaxHighlightingMenu");
        QVERIFY(menu);
        QCOMPARE(menu->actions().size(), 5);
        for (auto* action : menu->actions()) {
            action->trigger();
            QCOMPARE(crawler->syntaxLanguage(), action->data().toString());
        }
    }
    void syntaxReadLimitUsesIndex() {
        const auto denied = data_->getLinesRaw(0_lnum, 1_lcount, 4);
        QVERIFY(denied.buffer.empty());
        QVERIFY(denied.endOfLines.empty());
        const auto accepted = data_->getLinesRaw(0_lnum, 1_lcount, 65536);
        QVERIFY(!accepted.buffer.empty());
        QCOMPARE(accepted.decodeLines().front(), data_->getLineString(0_lnum));
    }
    void lineNumberChangeInvalidatesExistingPixels() {
        view_->setLineNumbersVisible(false);
        matchesFull();
        view_->verticalScrollBar()->setValue(101);
        matchesFull();
    }
    void scrollingMatchesFullRendering() { scrollSequence(); }
    void clickingAfterReuseSelectsTheDisplayedLine() {
        scrollSequence();
        view_->verticalScrollBar()->setValue(103);
        frame();
        const QFontMetrics metrics(view_->font());
        QTest::mouseClick(view_->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(200, metrics.height() * 2 + metrics.height() / 2));
        QVERIFY(view_->getSelectedText().startsWith("row 0105 ERROR marker"));
        QVERIFY(view_->getSelectedText().contains(QString::fromUtf8("中文日志")));
    }
    void directFontChangeRebuildsGeometry() {
        const QFont font("Consolas", 14);
        view_->setFont(font);
        const auto actual = frame();
        view_->updateFont(font);
        QCOMPARE(actual, frame());
        scrollSequence();
    }
    void filteredResultsAndMarks() {
        view_.reset();
        filtered_ = std::make_unique<LogFilteredData>(data_.get());
        filtered_->runSearch(RegularExpressionPattern("ERROR", true, false, false, true));
        QTRY_COMPARE_WITH_TIMEOUT(filtered_->getNbMatches().get(), 500, 10000);
        view_ = std::make_unique<FilteredView>(filtered_.get(), &pattern_);
        view_->resize(900, 510);
        view_->updateFont(QFont("Consolas", 10));
        view_->updateData();
        view_->show();
        QVERIFY(QTest::qWaitForWindowExposed(view_.get()));
        scrollSequence();
        filtered_->addMark(105_lnum);
        view_->updateData();
        matchesFull();
        scrollSequence();
        filtered_->deleteMark(105_lnum);
        view_->updateData();
        matchesFull();
        filtered_->clearSearch();
        filtered_->runSearch(RegularExpressionPattern("row 01", true, false, false, true));
        QTRY_COMPARE_WITH_TIMEOUT(filtered_->getNbMatches().get(), 100, 10000);
        view_->updateData();
        matchesFull();
        view_->verticalScrollBar()->setValue(1);
        matchesFull();
        view_->verticalScrollBar()->setValue(2);
        matchesFull();
    }
    void changingSelectionWhileJumpingClearsOldPixels() {
        view_->selectAndDisplayLine(125_lnum);
        matchesFull();
        view_->selectAndDisplayLine(140_lnum);
        matchesFull();
        scrollSequence();
    }
    void quickFindClearingSelectionInvalidatesPixels() {
        const Portion selected(105_lnum, 9_lcol, 13_lcol);
        QVERIFY(QMetaObject::invokeMethod(view_.get(), "setQuickFindResult", Qt::DirectConnection,
                                          Q_ARG(bool, true), Q_ARG(Portion, selected)));
        QCOMPARE(view_->getSelectedText(), QString("ERROR"));
        matchesFull();
        QVERIFY(QMetaObject::invokeMethod(view_.get(), "setQuickFindResult", Qt::DirectConnection,
                                          Q_ARG(bool, false), Q_ARG(Portion, Portion{})));
        QCOMPARE(view_->getSelectedText(), QString());
        matchesFull();
        scrollSequence();
    }
    void searchProgressRefreshesMainViewBullets() {
        MainWindow window{WindowSession{std::make_shared<Session>(), "cache-search", 0}};
        window.resize(1000, 700);
        window.show();
        window.loadFileNonInteractive(file_);
        auto* crawler = window.findChild<CrawlerWidget*>();
        QVERIFY(crawler);
        QTRY_VERIFY_WITH_TIMEOUT(CrawlerAccess::loaded(*crawler), 10000);
        auto* main = crawler->findChild<LogMainView*>();
        QVERIFY(main);
        main->verticalScrollBar()->setValue(100);
        const auto capture = [main] {
            QApplication::processEvents();
            main->viewport()->repaint();
            return main->viewport()->grab().toImage();
        };
        const auto before = capture();
        auto* edit = crawler->findChild<QComboBox*>("mainSearchEdit");
        auto* button = crawler->findChild<QToolButton*>("mainSearchButton");
        QVERIFY(edit && button);
        edit->setCurrentText("ERROR");
        button->click();
        // Cache the pending-search frame before queued results update the bullets.
        main->viewport()->repaint();
        QTRY_COMPARE_WITH_TIMEOUT(CrawlerAccess::matches(*crawler), 500, 10000);
        const auto actual = capture();
        main->forceRefresh();
        const auto expected = capture();
        QVERIFY(before != expected);
        QCOMPARE(actual, expected);
    }
    void highlighterChangeAndPartialSelection() {
        view_->setSearchPattern(RegularExpressionPattern("ERROR", true, false, false, true));
        matchesFull();
        pattern_.changeSearchPattern("marker");
        matchesFull();
        const QFontMetrics metrics(view_->font());
        const int digit = metrics.horizontalAdvance(QLatin1Char('0'));
        const int textStart = 11 + 1 + 6 + 3 * digit + 1;
        QTest::mouseDClick(view_->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(textStart + 11 * digit, metrics.height() / 2));
        QVERIFY(view_->isPartialSelection());
        QCOMPARE(view_->getSelectedText(), QString("ERROR"));
        matchesFull();
        scrollSequence();
        QCOMPARE(view_->getSelectedText(), QString("ERROR"));
    }
    void paletteFontSizeAndWrapChanges() {
        auto palette = view_->palette();
        palette.setColor(QPalette::Base, QColor("#ffffff"));
        palette.setColor(QPalette::Window, QColor("#f0f0f0"));
        palette.setColor(QPalette::Text, QColor("#101010"));
        view_->setPalette(palette);
        qInfo("checking palette");
        matchesFull();
        scrollSequence();
        view_->updateFont(QFont("Consolas", 12));
        qInfo("checking font");
        matchesFull();
        scrollSequence();
        view_->resize(760, 435);
        QApplication::processEvents();
        qInfo("checking resize");
        matchesFull();
        scrollSequence();
        view_->horizontalScrollBar()->setValue(7);
        qInfo("checking horizontal");
        matchesFull();
        scrollSequence();
        view_->textWrapSet(true);
        qInfo("checking wrap on");
        matchesFull();
        scrollSequence();
        view_->textWrapSet(false);
        qInfo("checking wrap off");
        matchesFull();
        scrollSequence();
    }
    void fileReloadAndEndOfFile() {
        QFile file(file_);
        QVERIFY(file.open(QIODevice::Append));
        file.write("appended ERROR line\n");
        file.close();
        QSignalSpy loaded(data_.get(), &LogData::loadingFinished);
        data_->reload();
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        view_->updateData();
        matchesFull();
        scrollSequence();
        view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->maximum());
        matchesFull();
        view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->maximum() - 1);
        matchesFull();
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("short ERROR file\n");
        file.close();
        loaded.clear();
        data_->reload();
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        view_->updateData();
        matchesFull();
    }
};

QTEST_MAIN(TextCacheTest)
#include "textcachetest.moc"
