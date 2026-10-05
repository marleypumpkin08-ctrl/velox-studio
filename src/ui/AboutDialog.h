#pragma once

#include <QDialog>

namespace velox::ui {

class AboutDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

} // namespace velox::ui
