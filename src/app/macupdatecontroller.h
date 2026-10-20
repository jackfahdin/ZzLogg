#ifndef ZZLOGG_MACUPDATECONTROLLER_H
#define ZZLOGG_MACUPDATECONTROLLER_H

#include <QObject>
#include <functional>
#include <memory>

// Built only for official macOS bundles. All methods run on the GUI thread.
class MacUpdateController final : public QObject {
public:
    MacUpdateController( std::function<bool()> prepareApplicationExit,
                         std::function<void()> cancelApplicationExitPreparation,
                         QObject* parent = nullptr );
    ~MacUpdateController() override;
    bool available() const;
    void checkForUpdates( bool manual );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
