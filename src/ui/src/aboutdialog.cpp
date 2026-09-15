#include "aboutdialog.h"

#include "klogg_version.h"
#include "zzlogg_brand.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
QLabel* textLabel(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse
                                   | Qt::LinksAccessibleByKeyboard);
    label->setOpenExternalLinks(true);
    return label;
}

QString link(const QString& url, const QString& title, bool dark)
{
    return QStringLiteral("<a style='color:%1' href='%2'>%3</a>")
        .arg(dark ? "#75bfff" : "#0064b4", url.toHtmlEscaped(), title.toHtmlEscaped());
}
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowIcon(QIcon(QStringLiteral(":/zzlogg/icons/ZzLogg.svg")));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 8, 12, 16);
    layout->setSpacing(20);

    auto* identity = new QHBoxLayout;
    identity->setSpacing(20);
    auto* icon = new QLabel(content);
    icon->setPixmap(windowIcon().pixmap(QSize(72, 72), devicePixelRatioF()));
    icon->setFixedSize(72, 72);
    identity->addWidget(icon, 0, Qt::AlignTop);
    auto* heading = new QVBoxLayout;
    heading->setSpacing(5);
    auto* title = textLabel(content);
    title->setText(QString::fromLatin1(zzlogg::brand::ProductName));
    auto titleFont = title->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    title->setFont(titleFont);
    heading->addWidget(title);
    auto* version = textLabel(content);
    version->setText(kloggVersion());
    heading->addWidget(version);
    description_ = textLabel(content);
    description_->setObjectName(QStringLiteral("aboutDescription"));
    heading->addWidget(description_);
    identity->addLayout(heading, 1);
    layout->addLayout(identity);

    auto* details = new QGridLayout;
    details->setHorizontalSpacing(24);
    details->setVerticalSpacing(10);
    details->setColumnStretch(1, 1);
    maintainerTitle_ = textLabel(content);
    buildTitle_ = textLabel(content);
    commitTitle_ = textLabel(content);
    const QList<QLabel*> labels{maintainerTitle_, buildTitle_, commitTitle_};
    const QStringList values{QStringLiteral("Jackfahdin"), kloggBuildDate(), kloggCommit()};
    for (int row = 0; row < labels.size(); ++row) {
        details->addWidget(labels[row], row, 0, Qt::AlignTop);
        auto* value = textLabel(content);
        value->setTextFormat(Qt::PlainText);
        value->setText(values[row]);
        details->addWidget(value, row, 1);
    }
    layout->addLayout(details);
    homepage_ = textLabel(content);
    layout->addWidget(homepage_);
    auto* separator = new QFrame(content);
    separator->setFrameShape(QFrame::HLine);
    layout->addWidget(separator);
    creditsTitle_ = textLabel(content);
    auto sectionFont = creditsTitle_->font();
    sectionFont.setBold(true);
    creditsTitle_->setFont(sectionFont);
    layout->addWidget(creditsTitle_);
    credits_ = textLabel(content);
    layout->addWidget(credits_);
    layout->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    auto* buttons = new QDialogButtonBox(this);
    close_ = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
    close_->setObjectName(QStringLiteral("aboutClose"));
    close_->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
    retranslate();
    resize(QSize(560, 520).boundedTo(parent->screen()->availableGeometry().size() - QSize(48, 80)));
}

void AboutDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange || event->type() == QEvent::PaletteChange)
        retranslate();
}

void AboutDialog::retranslate()
{
    setWindowTitle(tr("About %1").arg(QString::fromLatin1(zzlogg::brand::ProductName)));
    description_->setText(tr("A fast, advanced log explorer."));
    maintainerTitle_->setText(tr("Maintainer"));
    buildTitle_->setText(tr("Build date"));
    commitTitle_->setText(tr("Commit"));
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    homepage_->setText(link(QString::fromLatin1(zzlogg::brand::HomepageUrl), tr("Project homepage"), dark));
    creditsTitle_->setText(tr("Open source and acknowledgements"));
    credits_->setText(tr(
        "<p>Based on %1, a fork of %2.</p>"
        "<p>Interface powered by ZzPureTools. Icons provided by %3.</p>"
        "<p>Copyright &copy; 2020 Nicolas Bonnefon, Anton Filimonov and other contributors.</p>"
        "<p>You may modify and redistribute the program under the GNU GPL, version 3 or later.</p>")
        .arg(link("https://github.com/variar/klogg", "klogg", dark),
             link("http://glogg.bonnefon.org/", "glogg", dark),
             link("https://icons8.com", "Icons8", dark)));
    close_->setText(tr("Close"));
}
