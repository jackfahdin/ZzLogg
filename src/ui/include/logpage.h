#pragma once

#include <QSplitter>

class SearchPanel;
class QTabWidget;

// Layout only; data ownership and search state remain in CrawlerWidget.
class LogPage : public QSplitter {
    Q_OBJECT
public:
    explicit LogPage( QWidget* parent = nullptr );
    QTabWidget* resultsTabs() const
    {
        return resultsTabs_;
    }

protected:
    void compose( QWidget* mainView, SearchPanel* searchPanel, QWidget* firstResults );

private:
    QTabWidget* resultsTabs_ = nullptr;
};
