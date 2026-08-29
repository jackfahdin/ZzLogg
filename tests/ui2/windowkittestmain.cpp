#include <QApplication>
#include <QtTest>

#include <ZzWindowKit/ZzWindowKitBootstrap.h>

void verifyTitleFormatting();
void verifySuccessfulMenuMigration();
void verifyConfigureFailureRollsBack();
void verifyDuplicateInstallIsRejectedWithoutMutation();
void verifyNoMenuDirectFailureIsNonMutating();
void verifyAttachedFailureDestroysWindowKitTemporaries();
void verifyInterruptedMenuCommitSurvivesDeferredDeleteDelivery();
void verifyCustomMenuWidgetIsRejectedWithoutMutation();
void verifyActiveDocumentTitleSynchronization();
void verifyChromeStateAndIconSynchronization();
void verifyCompleteChromeConfigurationBuilder();
void verifyWindowButtonIntents();
void verifyAlwaysOnTopPreservesWindowPresentation();
void verifySharedThemeObservationAndForwarding();
void verifyWindowOwnsShellLifetime();
void verifyConfiguratorExceptionBecomesFailure();

class FluentShellTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void formatsLogicalTitles()
    {
        verifyTitleFormatting();
    }

    void commitsMenuMigrationWithoutReplacingBusinessWidgets()
    {
        verifySuccessfulMenuMigration();
    }

    void rollsBackAttachWhenChromeConfigurationFails()
    {
        verifyConfigureFailureRollsBack();
    }

    void rejectsDuplicateInstallBeforeTouchingInstalledChrome()
    {
        verifyDuplicateInstallIsRejectedWithoutMutation();
    }

    void leavesNoMenuWindowUntouchedWhenAttachDirectlyFails()
    {
        verifyNoMenuDirectFailureIsNonMutating();
    }

    void destroysWindowKitTemporariesAfterAttachedFailure()
    {
        verifyAttachedFailureDestroysWindowKitTemporaries();
    }

    void rollsBackInterruptedMenuCommitBeforeDeferredDelete()
    {
        verifyInterruptedMenuCommitSurvivesDeferredDeleteDelivery();
    }

    void rejectsCustomMenuWidgetBeforeWindowKitConfiguration()
    {
        verifyCustomMenuWidgetIsRejectedWithoutMutation();
    }

    void synchronizesCompleteLogicalDocumentTitles()
    {
        verifyActiveDocumentTitleSynchronization();
    }

    void synchronizesWindowKitChromeStateAndIcon()
    {
        verifyChromeStateAndIconSynchronization();
    }

    void buildsCompleteWindowKitChromeConfiguration()
    {
        verifyCompleteChromeConfigurationBuilder();
    }

    void executesWindowButtonIntents()
    {
        verifyWindowButtonIntents();
    }

    void preservesVisibilityAndStateWhenTogglingAlwaysOnTop()
    {
        verifyAlwaysOnTopPreservesWindowPresentation();
    }

    void observesSharedThemeAndOnlyForwardsRequests()
    {
        verifySharedThemeObservationAndForwarding();
    }

    void destroysShellWithItsWindow()
    {
        verifyWindowOwnsShellLifetime();
    }

    void convertsConfiguratorExceptionsToFailures()
    {
        verifyConfiguratorExceptionBecomesFailure();
    }
};

int main( int argc, char* argv[] )
{
    const auto prepared = ZzWindowKit::ZzWindowKitBootstrap::prepare();
    if ( !prepared )
        return 2;
    QApplication app( argc, argv );
    FluentShellTest test;
    return QTest::qExec( &test, argc, argv );
}

#include "windowkittestmain.moc"
