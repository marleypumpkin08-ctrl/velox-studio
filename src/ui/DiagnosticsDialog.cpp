#include "DiagnosticsDialog.h"

#include "BuildInfo.h"

#include <QVulkanInstance>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace velox::ui {

DiagnosticsDialog::DiagnosticsDialog(QVulkanInstance* instance, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Velox Studio Diagnostics"));
    resize(580, 400);

    auto* layout = new QVBoxLayout(this);
    auto* details = new QPlainTextEdit(this);
    details->setReadOnly(true);
    details->setPlainText(
        BuildInfo::summary()
        + QStringLiteral("\nVulkan instance: %1\nSupported instance extensions: %2")
              .arg(instance->isValid() ? tr("initialized") : tr("unavailable"))
              .arg(instance->supportedExtensions().size()));
    layout->addWidget(details);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

} // namespace velox::ui
