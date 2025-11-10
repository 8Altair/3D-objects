#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>  // Qt base class for main application windows
#include <QLabel>
#include <QLineEdit>


QT_BEGIN_NAMESPACE  // Begin Qt namespace block (matches ui header style)

namespace Ui { class MainWindow; }   // Forward declaration of auto-generated Ui class

QT_END_NAMESPACE    // End Qt namespace block

class MainWindow final : public QMainWindow // Main window class that publicly inherits QMainWindow class
{
    Q_OBJECT    // Enables Qt meta-object features (signals/slots, RTTI)

    Ui::MainWindow *ui{}; // Pointer to auto-generated UI helper (owns child widgets)
    QLineEdit *camera_position_x_line_edit_{nullptr};
    QLineEdit *camera_position_y_line_edit_{nullptr};
    QLineEdit *camera_position_z_line_edit_{nullptr};
    QLineEdit *camera_rotation_x_line_edit_{nullptr};
    QLineEdit *camera_rotation_y_line_edit_{nullptr};
    QLineEdit *camera_rotation_z_line_edit_{nullptr};
    QLabel *help_label_{nullptr};

public:
    explicit MainWindow(QWidget *parent = nullptr); // Constructor; "explicit" avoids implicit conversions
    ~MainWindow() override; // Virtual destructor (overrides QObject destructor)
};


#endif //MAIN_WINDOW_H