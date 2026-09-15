#pragma once

#include <QTextBrowser>

class DocumentationWindow final : public QTextBrowser {
public:
    explicit DocumentationWindow(QWidget* parent);

protected:
    void changeEvent(QEvent* event) override;

private:
    void reload();
};
