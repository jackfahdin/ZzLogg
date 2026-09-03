/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <functional>

#include <QCoreApplication>
#include <QShortcut>
#include <QWidget>

#include "shortcuts.h"

QStringList getKeyBindings( QKeySequence::StandardKey standardKey )
{
    auto bindings = QKeySequence::keyBindings( standardKey );
    QStringList stringBindings;
    std::transform( bindings.cbegin(), bindings.cend(), std::back_inserter( stringBindings ),
                    []( const auto& keySequence ) { return keySequence.toString(); } );

    return stringBindings;
}

QString ShortcutAction::displayName( const std::string& action )
{
    const auto& definitions = defaultShortcutList();
    const auto definition = definitions.find( action );
    return definition != definitions.end()
               ? QCoreApplication::translate( "ShortcutAction", definition->second.sourceText )
               : QString::fromStdString( action );
}

QStringList ShortcutAction::defaultShortcutKeys( const std::string& action )
{
    const auto& shortcuts = defaultShortcutList();
    const auto actionShortcuts = shortcuts.find( action );
    return actionShortcuts != shortcuts.end() ? actionShortcuts->second.keySequence : QStringList{};
}

void ShortcutAction::registerShortcut( const ConfiguredShortcuts& configuredShortcuts,
                                       std::map<QString, QShortcut*>& shortcutsStorage,
                                       QWidget* shortcutsParent, Qt::ShortcutContext context,
                                       const std::string& action,
                                       const std::function<void()>& func )
{
    const auto keysConfiguration = configuredShortcuts.find( action );
    const auto keys = keysConfiguration != configuredShortcuts.end()
                          ? keysConfiguration->second
                          : ShortcutAction::defaultShortcutKeys( action );

    for ( const auto& key : keys ) {
        if ( key.isEmpty() ) {
            continue;
        }

        auto shortcut = shortcutsStorage.extract( key );
        if ( shortcut ) {
            shortcut.mapped()->deleteLater();
        }

        registerShortcut( key, shortcutsStorage, shortcutsParent, context, func );
    }
}

void ShortcutAction::registerShortcut( const QString& key,
                                       std::map<QString, QShortcut*>& shortcutsStorage,
                                       QWidget* shortcutsParent, Qt::ShortcutContext context,
                                       const std::function<void()>& func )
{
    auto newShortcut = new QShortcut( QKeySequence( key ), shortcutsParent );
    newShortcut->setContext( context );
    newShortcut->connect( newShortcut, &QShortcut::activated, shortcutsParent,
                          [ func ] { func(); } );
    shortcutsStorage.emplace( key, newShortcut );
}

QList<QKeySequence> ShortcutAction::shortcutKeys( const std::string& action,
                                                  const ConfiguredShortcuts& configuredShortcuts )
{
    const auto keysConfiguration = configuredShortcuts.find( action );
    const auto keys = keysConfiguration != configuredShortcuts.end()
                          ? keysConfiguration->second
                          : ShortcutAction::defaultShortcutKeys( action );

    QList<QKeySequence> shortcuts;
    std::transform( keys.cbegin(), keys.cend(), std::back_inserter( shortcuts ),
                    []( const QString& hotkeys ) { return QKeySequence( hotkeys ); } );

    return shortcuts;
}

const ShortcutAction::ShortcutList& ShortcutAction::defaultShortcutList()
{
    static ShortcutList defaultShortcutKeys = {
        {
            MainWindowNewWindow,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Open new window" ),
                QStringList{},
            },
        },
        {
            MainWindowOpenFile,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Open file" ),
                getKeyBindings( QKeySequence::Open ),
            },
        },
        {
            MainWindowCloseFile,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Close file" ),
                getKeyBindings( QKeySequence::Close ),
            },
        },
        {
            MainWindowCloseAll,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Close all files" ),
                QStringList{},
            },
        },
        {
            MainWindowSelectAll,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Select all" ),
                QStringList{ "Ctrl+A" },
            },
        },
        {
            MainWindowCopy,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Copy selection to clipboard" ),
                getKeyBindings( QKeySequence::Copy ),
            },
        },
        {
            MainWindowQuit,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Exit application" ),
                QStringList{ "Ctrl+Q" },
            },
        },
        {
            MainWindowFullScreen,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Full Screen" ),
                QStringList{},
            },
        },
        {
            MainWindowMax,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Maximize window" ),
                QStringList{},
            },
        },
        {
            MainWindowMin,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Minimize Window" ),
                QStringList{},
            },
        },
        {
            MainWindowPreference,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Preferences" ),
                QStringList{},
            },
        },
        {
            MainWindowOpenQf,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Open quick find" ),
                getKeyBindings( QKeySequence::Find ),
            },
        },
        {
            MainWindowOpenQfForward,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Quick find forward" ),
                QStringList{ QKeySequence( Qt::Key_Apostrophe ).toString() },
            },
        },
        {
            MainWindowOpenQfBackward,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Quick find backward" ),
                QStringList{ QKeySequence( Qt::Key_QuoteDbl ).toString() },
            },
        },
        {
            MainWindowFocusSearchInput,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Set focus to search input" ),
                QStringList{ { "Ctrl+S", "Ctrl+Shift+F" } },
            },
        },
        {
            MainWindowClearFile,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Clear file" ),
                QStringList{ getKeyBindings( QKeySequence::Cut ) },
            },
        },
        {
            MainWindowOpenContainingFolder,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Open containing folder" ),
                QStringList{},
            },
        },
        {
            MainWindowOpenInEditor,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Open file in editor" ),
                QStringList{},
            },
        },
        {
            MainWindowCopyPathToClipboard,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Copy file path to clipboard" ),
                QStringList{},
            },
        },
        {
            MainWindowOpenFromClipboard,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Paste text from clipboard" ),
                getKeyBindings( QKeySequence::Paste ),
            },
        },
        {
            MainWindowOpenFromUrl,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Open file from URL" ),
                QStringList{},
            },
        },
        {
            MainWindowFollowFile,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Monitor file changes" ),
                { { QKeySequence( Qt::Key_F ).toString(),
                    QKeySequence( Qt::Key_F10 ).toString() } },
            },
        },
        {
            MainWindowTextWrap,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Toggle text wrap" ),
                QStringList{ QKeySequence( Qt::Key_W ).toString() },
            },
        },
        {
            MainWindowReload,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Reload file" ),
                QStringList{ getKeyBindings( QKeySequence::Refresh ) },
            },
        },
        {
            MainWindowStop,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Stop file loading" ),
                QStringList{ getKeyBindings( QKeySequence::Cancel ) },
            },
        },
        {
            MainWindowScratchpad,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Open scratchpad" ),
                QStringList{},
            },
        },
        {
            MainWindowSelectOpenFile,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Switch to file" ),
                QStringList{ "Ctrl+Shift+O" },
            },
        },
        {
            CrawlerChangeVisibilityForward,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Change filtered lines visibility forward" ),
                QStringList{ QKeySequence( Qt::Key_V ).toString() },
            },
        },
        {
            CrawlerChangeVisibilityBackward,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Change filtered lines visibility backward" ),
                QStringList{ "Shift+V" },
            },
        },
        {
            CrawlerChangeVisibilityToMarksAndMatches,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Change filtered lines visibility to marks and matches" ),
                QStringList{ QKeySequence( Qt::Key_1 ).toString() },
            },
        },
        {
            CrawlerChangeVisibilityToMarks,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Change filtered lines visibility to marks" ),
                QStringList{ QKeySequence( Qt::Key_2 ).toString() },
            },
        },
        {
            CrawlerChangeVisibilityToMatches,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Change filtered lines visibility to matches" ),
                QStringList{ QKeySequence( Qt::Key_3 ).toString() },
            },
        },
        {
            CrawlerIncreseTopViewSize,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Increase main view" ),
                QStringList{ QKeySequence( Qt::Key_Plus ).toString() },
            },
        },
        {
            CrawlerDecreaseTopViewSize,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Decrease main view" ),
                QStringList{ QKeySequence( Qt::Key_Minus ).toString() },
            },
        },
        {
            CrawlerEnableCaseMatching,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Enable case matching" ),
                QStringList{ QKeySequence( Qt::Key_4 ).toString() },
            },
        },
        {
            CrawlerEnableRegex,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Enable regex" ),
                QStringList{ QKeySequence( Qt::Key_5 ).toString() },
            },
        },
        {
            CrawlerEnableInverseMatching,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Enable inverse matching" ),
                QStringList{ QKeySequence( Qt::Key_6 ).toString() },
            },
        },
        {
            CrawlerEnableRegexCombining,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Enable regex combining" ),
                QStringList{ QKeySequence( Qt::Key_7 ).toString() },
            },
        },
        {
            CrawlerEnableAutoRefresh,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Enable auto refresh" ),
                QStringList{ QKeySequence( Qt::Key_8 ).toString() },
            },
        },
        {
            CrawlerKeepResults,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Keep search results" ),
                QStringList{ QKeySequence( Qt::Key_9 ).toString() },
            },
        },
        // remove by commit 0b75b9d6
        // { QfFindNext, { QApplication::tr( "QuickFind: Find next" ), QStringList{ getKeyBindings(
        // QKeySequence::FindNext ) } } }, { QfFindPrev, { QApplication::tr( "QuickFind: Find
        // previous"
        // ), QStringList{ getKeyBindings( QKeySequence::FindPrevious ) } } },
        {
            LogViewMark,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Add line mark" ),
                QStringList{ QKeySequence( Qt::Key_M ).toString() },
            },
        },
        {
            LogViewNextMark,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Jump to next mark" ),
                QStringList{ QKeySequence( Qt::Key_BracketRight ).toString() },
            },
        },
        {
            LogViewPrevMark,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Jump to previous mark" ),
                QStringList{ QKeySequence( Qt::Key_BracketLeft ).toString() },
            },
        },
        {
            LogViewSelectionUp,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Move selection up" ),
                { { QKeySequence( Qt::Key_Up ).toString(), QKeySequence( Qt::Key_K ).toString() } },
            },
        },
        {
            LogViewSelectionDown,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Move selection down" ),
                { { QKeySequence( Qt::Key_Down ).toString(),
                    QKeySequence( Qt::Key_J ).toString() } },
            },
        },
        {
            LogViewScrollUp,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Scroll up" ),
                QStringList{ "Ctrl+Up" },
            },
        },
        {
            LogViewScrollDown,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Scroll down" ),
                QStringList{ "Ctrl+Down" },
            },
        },
        {
            LogViewScrollLeft,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Scroll left" ),
                { { QKeySequence( Qt::Key_Left ).toString(),
                    QKeySequence( Qt::Key_H ).toString() } },
            },
        },
        {
            LogViewScrollRight,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Scroll right" ),
                { { QKeySequence( Qt::Key_Right ).toString(),
                    QKeySequence( Qt::Key_L ).toString() } },
            },
        },
        {
            LogViewJumpToStartOfLine,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Jump to the beginning of the current line" ),
                { QKeySequence( Qt::Key_Home ).toString(),
                  QKeySequence( Qt::Key_AsciiCircum ).toString() },
            },
        },
        {
            LogViewJumpToEndOfLine,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Jump to the end start of the current line" ),
                QStringList{ QKeySequence( Qt::Key_Dollar ).toString() },
            },
        },
        {
            LogViewJumpToRightOfScreen,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Jump to the right of the text" ),
                QStringList{ QKeySequence( Qt::Key_End ).toString() },
            },
        },
        {
            LogViewJumpToBottom,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Jump to the bottom of the text" ),
                QStringList{ { "Ctrl+End", "Shift+G" } },
            },
        },
        {
            LogViewJumpToTop,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Jump to the top of the text" ),
                QStringList{ "Ctrl+Home" },
            },
        },
        {
            LogViewJumpToLine,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Jump to line" ),
                QStringList{ "Ctrl+L" },
            },
        },
        {
            LogViewQfForward,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Main view: find next" ),
                getKeyBindings( QKeySequence::FindNext )
                    << QKeySequence( Qt::Key_N ).toString() << "Ctrl+G",
            },
        },
        {
            LogViewQfBackward,
            { QT_TRANSLATE_NOOP( "ShortcutAction", "Main view: find previous" ),
              getKeyBindings( QKeySequence::FindPrevious ) << "Shift+N"
                                                           << "Ctrl+Shift+G" },
        },
        {
            LogViewQfSelectedForward,
            { QT_TRANSLATE_NOOP( "ShortcutAction", "Set selection to QuickFind and find next" ),
              { QKeySequence( Qt::Key_Asterisk ).toString(),
                QKeySequence( Qt::Key_Period ).toString() } },
        },
        {
            LogViewQfSelectedBackward,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Set selection to QuickFind and find previous" ),
                QStringList{ QKeySequence( Qt::Key_Slash ).toString(),
                             QKeySequence( Qt::Key_Comma ).toString() },
            },
        },
        {
            LogViewExitView,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Release focus from view" ),
                QStringList{ QKeySequence( Qt::Key_Space ).toString() },
            },
        },
        {
            LogViewAddColorLabel1,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Highlight text with color 1" ),
                QStringList{ "Ctrl+Shift+1" },
            },
        },
        {
            LogViewAddColorLabel2,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Highlight text with color 2" ),
                QStringList{ "Ctrl+Shift+2" },
            },
        },
        {
            LogViewAddColorLabel3,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Highlight text with color 3" ),
                QStringList{ "Ctrl+Shift+3" },
            },
        },
        {
            LogViewAddColorLabel4,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Highlight text with color 4" ),
                QStringList{ "Ctrl+Shift+4" },
            },
        },
        {
            LogViewAddColorLabel5,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Highlight text with color 5" ),
                QStringList{ "Ctrl+Shift+5" },
            },
        },
        {
            LogViewAddColorLabel6,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Highlight text with color 6" ),
                QStringList{ "Ctrl+Shift+6" },
            },
        },
        {
            LogViewAddColorLabel7,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Highlight text with color 7" ),
                QStringList{ "Ctrl+Shift+7" },
            },
        },
        {
            LogViewAddColorLabel8,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Highlight text with color 8" ),
                QStringList{ "Ctrl+Shift+8" },
            },
        },
        {
            LogViewAddColorLabel9,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Highlight text with color 9" ),
                QStringList{ "Ctrl+Shift+9" },
            },
        },
        {
            LogViewAddNextColorLabel,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Highlight text with next color" ),
                QStringList{ "Ctrl+D" },
            },
        },
        {
            LogViewClearColorLabels,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Clear all color labels" ),
                QStringList{ "Ctrl+Shift+0" },
            },
        },
        {
            LogViewSendSelectionToScratchpad,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Send selection to scratchpad" ),
                QStringList{ "Ctrl+Z" },
            },
        },
        {
            LogViewReplaceScratchpadWithSelection,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Replace scratchpad with selection" ),
                QStringList{ "Ctrl+Shift+Z" },
            },
        },
        {
            LogViewAddToSearch,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Add selection to search pattern" ),
                QStringList{ "Shift+A" },
            },
        },
        {
            LogViewExcludeFromSearch,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Exclude selection from search pattern " ),
                QStringList{ "Shift+E" },
            },
        },
        {
            LogViewReplaceSearch,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Replace search pattern with selection" ),
                QStringList{ "Shift+R" },
            },
        },
        {
            LogViewSelectLinesUp,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Select lines down" ),
                QStringList{ "Shift+Up" },
            },
        },
        {
            LogViewSelectLinesDown,
            {
                QT_TRANSLATE_NOOP( "ShortcutAction", "Select lines up" ),
                QStringList{ "Shift+Down" },
            },
        },
    };
    return defaultShortcutKeys;
}
