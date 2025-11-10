#include "main_window.h"    // Header for this class (declaration of MainWindow)
#include "ui_main_window.h" // Auto-generated header from MainWindow.ui (defines Ui::MainWindow)
#include "view_3D.h"   // OpenGL widget class (QOpenGLWidget subclass)

#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>
#include <QSizePolicy>
#include <QDoubleValidator>
#include <QLocale>

#include <limits>
#include <memory>


MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)   // Initialize base QMainWindow and allocate UI helper
{
    ui->setupUi(this);

    // GL scene widget into your layout
    auto *scene = new View(this);
    ui->verticalLayout->addWidget(scene);

    // Toolbar 1: Controls
    QToolBar* tool_bar = addToolBar("Controls");
    tool_bar->setMovable(false);
    tool_bar->setStyleSheet("QToolBar { spacing: 10px; }");

    auto *left_spacer = new QWidget(tool_bar);
    left_spacer->setFixedWidth(16);
    left_spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    tool_bar->addWidget(left_spacer);

    const auto create_double_line_edit = [tool_bar](const QString& text, const int width)
    {
        auto *line_edit = new QLineEdit(text, tool_bar);
        line_edit->setFixedWidth(width);
        auto validator = std::make_unique<QDoubleValidator>();
        validator->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), 6);
        validator->setNotation(QDoubleValidator::StandardNotation);
        validator->setLocale(QLocale::c());
        validator->setParent(line_edit);
        const auto *validator_ptr = validator.release();
        line_edit->setValidator(validator_ptr);
        return line_edit;
    };

    const auto read_float = [](const QLineEdit* edit)
    {
        bool ok = false;
        const float value = QLocale::c().toFloat(edit->text(), &ok);
        return ok ? value : edit->text().toFloat();
    };

    // Reset
    const QAction *act_reset = tool_bar->addAction("Reset");
    if (QWidget *reset_button = tool_bar->widgetForAction(const_cast<QAction*>(act_reset)))
    {
        reset_button->setObjectName("resetButton");
        reset_button->setStyleSheet(
            "#resetButton {"
            "  background-color: #1e88e5;"
            "  color: white;"
            "  padding: 6px 12px;"
            "  border-radius: 4px;"
            "  font-weight: 600;"
            "}"
            "#resetButton:hover {"
            "  background-color: #1565c0;"
            "}"
            "#resetButton:pressed {"
            "  background-color: #0d47a1;"
            "}"
        );
    }
    connect(act_reset, &QAction::triggered, this, [this, scene]
    {
        scene->reset_all();

        camera_position_x_line_edit_->setText("3.0");
        camera_position_y_line_edit_->setText("3.5");
        camera_position_z_line_edit_->setText("12.5");

        camera_rotation_x_line_edit_->setText("-15");
        camera_rotation_y_line_edit_->setText("15");
        camera_rotation_z_line_edit_->setText("0");
    });

    tool_bar->addSeparator();
    tool_bar->addWidget(new QLabel("Camera position (x, y, z):", tool_bar));
    camera_position_x_line_edit_ = create_double_line_edit("3.0", 55);
    camera_position_y_line_edit_ = create_double_line_edit("3.5", 55);
    camera_position_z_line_edit_ = create_double_line_edit("12.5", 55);
    tool_bar->addWidget(camera_position_x_line_edit_);
    tool_bar->addWidget(camera_position_y_line_edit_);
    tool_bar->addWidget(camera_position_z_line_edit_);
    const auto apply_camera_position = [this, scene, read_float]
    {
        scene->set_cam_position(
            read_float(camera_position_x_line_edit_),
            read_float(camera_position_y_line_edit_),
            read_float(camera_position_z_line_edit_));
    };
    connect(camera_position_x_line_edit_, &QLineEdit::returnPressed, apply_camera_position);
    connect(camera_position_y_line_edit_, &QLineEdit::returnPressed, apply_camera_position);
    connect(camera_position_z_line_edit_, &QLineEdit::returnPressed, apply_camera_position);

    // Camera rotation editors
    tool_bar->addSeparator();
    tool_bar->addWidget(new QLabel("Camera rotation (x, y, z):", tool_bar));
    camera_rotation_x_line_edit_ = create_double_line_edit("-15", 55);
    camera_rotation_y_line_edit_ = create_double_line_edit("15", 55);
    camera_rotation_z_line_edit_ = create_double_line_edit("0", 55);
    tool_bar->addWidget(camera_rotation_x_line_edit_);
    tool_bar->addWidget(camera_rotation_y_line_edit_);
    tool_bar->addWidget(camera_rotation_z_line_edit_);
    const auto apply_camera_rotation = [this, scene, read_float]
    {
        scene->set_cam_rotation(
            read_float(camera_rotation_x_line_edit_),
            read_float(camera_rotation_y_line_edit_),
            read_float(camera_rotation_z_line_edit_));
    };
    connect(camera_rotation_x_line_edit_, &QLineEdit::returnPressed, apply_camera_rotation);
    connect(camera_rotation_y_line_edit_, &QLineEdit::returnPressed, apply_camera_rotation);
    connect(camera_rotation_z_line_edit_, &QLineEdit::returnPressed, apply_camera_rotation);

    // Toolbar 2: Help (full-width below)
    addToolBarBreak();  // Place next toolbar on a new row

    QToolBar* help_tool_bar = addToolBar("Help");
    help_tool_bar->setMovable(false);

    const QString help_text =
        "Left mouse button: Orbit camera   |   Right mouse button (+Shift): Pan   |   Mouse wheel: Dolly\n"
        "W/A/S/D, R/F: Move camera   |   I/K, J/L, U/O: Rotate camera";

    help_label_ = new QLabel(help_text, help_tool_bar);
    help_label_->setWordWrap(true);
    help_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QFont help_font = help_label_->font();
    help_font.setPointSize(help_font.pointSize() + 3);
    help_label_->setFont(help_font);

    help_label_->setMinimumWidth(900);
    help_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    help_label_->setStyleSheet("padding:6px 8px;");

    help_tool_bar->addWidget(help_label_);
}

MainWindow::~MainWindow()
{
    delete ui;  // Destroy the auto-generated UI wrapper (child widgets get deleted by Qt)
}