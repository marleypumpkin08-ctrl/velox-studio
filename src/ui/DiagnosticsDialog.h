#pragma once

#include <QDialog>

class QVulkanInstance;

namespace velox::ui {

class DiagnosticsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit DiagnosticsDialog(QVulkanInstance* instance, QWidget* parent = nullptr);
};

} // namespace velox::ui
