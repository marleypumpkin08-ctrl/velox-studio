#include "AboutDialog.h"

#include "BuildInfo.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace velox::ui {

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About Velox Studio"));
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(tr("Velox Studio"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* summary = new QLabel(BuildInfo::summary(), this);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto* repository = new QLabel(
        QStringLiteral("<a href=\"https://github.com/veloxstudio/VeloxStudio\">"
                       "Velox Studio on GitHub</a>"),
        this);
    repository->setOpenExternalLinks(true);
    layout->addWidget(repository);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

} // namespace velox::ui
