#pragma once

#include "predefinedfilterscombobox.h"
#include <QAction>
#include <QComboBox>
#include <QMenu>
#include <QToolButton>
#include <QWidget>

class QCompleter;
class QStandardItemModel;
class InfoLine;
class IconLoader;

// Owns search controls and emits user intent; no log data or search task ownership.
class SearchPanel final : public QWidget {
    Q_OBJECT
public:
    explicit SearchPanel( const QStringList& history, QWidget* parent = nullptr );
    void updateHistory( const QStringList& history );
    void retranslateUi();
    void loadIcons( IconLoader& icons );
    QComboBox* visibilityBox() const
    {
        return visibilityBox_;
    }
    QStandardItemModel* visibilityModel() const
    {
        return visibilityModel_;
    }
    PredefinedFiltersComboBox* predefinedFilters() const
    {
        return predefinedFilters_;
    }
    QComboBox* searchLineEdit() const
    {
        return searchLineEdit_;
    }
    QMenu* searchLineContextMenu() const
    {
        return searchLineContextMenu_;
    }
    QCompleter* searchLineCompleter() const
    {
        return searchLineCompleter_;
    }
    QAction* clearSearchHistoryAction() const
    {
        return clearSearchHistoryAction_;
    }
    QAction* editSearchHistoryAction() const
    {
        return editSearchHistoryAction_;
    }
    QAction* saveAsPredefinedFilterAction() const
    {
        return saveAsPredefinedFilterAction_;
    }
    InfoLine* searchInfoLine() const
    {
        return searchInfoLine_;
    }
    QToolButton* clearButton() const
    {
        return clearButton_;
    }
    QToolButton* searchButton() const
    {
        return searchButton_;
    }
    QToolButton* keepSearchResultsButton() const
    {
        return keepSearchResultsButton_;
    }
    QToolButton* stopButton() const
    {
        return stopButton_;
    }
    QToolButton* matchCaseButton() const
    {
        return matchCaseButton_;
    }
    QToolButton* useRegexpButton() const
    {
        return useRegexpButton_;
    }
    QToolButton* inverseButton() const
    {
        return inverseButton_;
    }
    QToolButton* booleanButton() const
    {
        return booleanButton_;
    }
    QToolButton* searchRefreshButton() const
    {
        return searchRefreshButton_;
    }

Q_SIGNALS:
    void searchRequested();
    void stopRequested();
    void patternEdited( const QString& text );
    void patternSelected();
    void filtersChanged( const QList<PredefinedFilter>& filters );
    void contextMenuRequested();
    void saveFilterRequested();
    void clearHistoryRequested();
    void editHistoryRequested();
    void visibilityChanged( int index );
    void autoRefreshChanged( bool enabled );
    void matchCaseChanged( bool enabled );
    void regexpChanged( bool enabled );
    void booleanChanged( bool enabled );

protected:
    void changeEvent( QEvent* event ) override;

private:
    QComboBox* visibilityBox_ = nullptr;
    QStandardItemModel* visibilityModel_ = nullptr;
    PredefinedFiltersComboBox* predefinedFilters_ = nullptr;
    QComboBox* searchLineEdit_ = nullptr;
    QMenu* searchLineContextMenu_ = nullptr;
    QCompleter* searchLineCompleter_ = nullptr;
    QAction* clearSearchHistoryAction_ = nullptr;
    QAction* editSearchHistoryAction_ = nullptr;
    QAction* saveAsPredefinedFilterAction_ = nullptr;
    InfoLine* searchInfoLine_ = nullptr;
    QToolButton* clearButton_ = nullptr;
    QToolButton* searchButton_ = nullptr;
    QToolButton* keepSearchResultsButton_ = nullptr;
    QToolButton* stopButton_ = nullptr;
    QToolButton* matchCaseButton_ = nullptr;
    QToolButton* useRegexpButton_ = nullptr;
    QToolButton* inverseButton_ = nullptr;
    QToolButton* booleanButton_ = nullptr;
    QToolButton* searchRefreshButton_ = nullptr;
};
