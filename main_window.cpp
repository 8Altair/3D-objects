#include "main_window.h"    // Header for this class (declaration of MainWindow)
#include "ui_main_window.h" // Auto-generated header from MainWindow.ui (defines Ui::MainWindow)
#include "view_3D.h"   // OpenGL widget class (QOpenGLWidget subclass)

#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QString>
#include <QDoubleValidator>
#include <QLocale>
#include <QFileDialog>
#include <QMessageBox>
#include <QSizePolicy>
#include <QSignalBlocker>

#include <limits>
#include <memory>
#include <algorithm>


MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)   // Initialize base QMainWindow and allocate UI helper
{
    ui->setupUi(this);

    // GL scene widget into your layout
    auto *scene = new View(this);
    ui->verticalLayout->addWidget(scene);

    // Toolbar 1: Controls
    QToolBar* tool_bar = addToolBar("Controls");
    tool_bar->setMovable(false);
    tool_bar->setStyleSheet("QToolBar { spacing: 10px; padding-left: 16px; }");

    const auto create_double_line_edit = [tool_bar](const QString& text, const int width)
    {
        auto line_edit = std::make_unique<QLineEdit>(text, tool_bar);
        line_edit->setFixedWidth(width);
        auto validator = std::make_unique<QDoubleValidator>();
        validator->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), 6);
        validator->setNotation(QDoubleValidator::StandardNotation);
        validator->setLocale(QLocale::c());
        validator->setParent(line_edit.get());
        line_edit->setValidator(validator.release());
        return line_edit.release();
    };

    const auto read_float = [](const QLineEdit* edit)
    {
        bool ok = false;
        const float value = QLocale::c().toFloat(edit->text(), &ok);
        return ok ? value : edit->text().toFloat();
    };

    // Reset
    const QAction *action_reset = tool_bar->addAction("Reset");
    if (QWidget *reset_widget = tool_bar->widgetForAction(const_cast<QAction*>(action_reset)))
    {
        QFont f = reset_widget->font();
        f.setPointSizeF(f.pointSizeF() * 1.3);
        reset_widget->setFont(f);
    }
    if (QWidget *reset_button = tool_bar->widgetForAction(const_cast<QAction*>(action_reset)))
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
    connect(action_reset, &QAction::triggered, this, [this, scene]
    {
        scene->reset_all();

        camera_position_x_line_edit_->setText("3.0");
        camera_position_y_line_edit_->setText("3.5");
        camera_position_z_line_edit_->setText("15.0");

        camera_rotation_x_line_edit_->setText("-15");
        camera_rotation_y_line_edit_->setText("15");
        camera_rotation_z_line_edit_->setText("0");

        if (color_mode_combo_box_)
        {
            const QSignalBlocker blocker(color_mode_combo_box_);
            color_mode_combo_box_->setCurrentIndex(static_cast<int>(View::ColorMode::Uniform));
        }
    });

    tool_bar->addSeparator();
    auto cam_position_label = std::make_unique<QLabel>("Camera position (x, y, z):", tool_bar);
    {
        QFont f = cam_position_label->font();
        f.setPointSizeF(f.pointSizeF() * 1.2);
        cam_position_label->setFont(f);
    }
    tool_bar->addWidget(cam_position_label.release());
    camera_position_x_line_edit_ = create_double_line_edit("3.0", 55);
    camera_position_y_line_edit_ = create_double_line_edit("3.5", 55);
    camera_position_z_line_edit_ = create_double_line_edit("15.0", 55);
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
    auto cam_rotation_label = std::make_unique<QLabel>("Camera rotation (x, y, z):", tool_bar);
    {
        QFont f = cam_rotation_label->font();
        f.setPointSizeF(f.pointSizeF() * 1.2);
        cam_rotation_label->setFont(f);
    }
    tool_bar->addWidget(cam_rotation_label.release());
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

    connect(scene, &View::cameraPositionChanged, this,
            [this](const float x, const float y, const float z)
            {
                const auto format = [](const float value)
                {
                    return QLocale::c().toString(value, 'f', 3);
                };
                camera_position_x_line_edit_->setText(format(x));
                camera_position_y_line_edit_->setText(format(y));
                camera_position_z_line_edit_->setText(format(z));
            });

    connect(scene, &View::cameraRotationChanged, this,
            [this](const float x, const float y, const float z)
            {
                const auto format = [](const float value)
                {
                    return QLocale::c().toString(value, 'f', 1);
                };
                camera_rotation_x_line_edit_->setText(format(x));
                camera_rotation_y_line_edit_->setText(format(y));
                camera_rotation_z_line_edit_->setText(format(z));
            });

    tool_bar->addSeparator();
    const QAction *object_import = tool_bar->addAction("Object");
    if (QWidget *object_widget = tool_bar->widgetForAction(const_cast<QAction*>(object_import)))
    {
        QFont f = object_widget->font();
        f.setPointSizeF(f.pointSizeF() * 1.2);
        object_widget->setFont(f);
    }
    if (QWidget *object_button = tool_bar->widgetForAction(const_cast<QAction*>(object_import)))
    {
        object_button->setObjectName("objectButton");
        object_button->setStyleSheet(
            "#objectButton {"
            "  background-color: #43a047;"
            "  color: white;"
            "  padding: 6px 12px;"
            "  border-radius: 4px;"
            "  font-weight: 600;"
            "}"
            "#objectButton:hover {"
            "  background-color: #2e7d32;"
            "}"
            "#objectButton:pressed {"
            "  background-color: #1b5e20;"
            "}"
        );
    }
    connect(object_import, &QAction::triggered, this, [this, scene]
    {
        const QString file_path = QFileDialog::getOpenFileName(
            this,
            tr("Import OBJ"),
            QString(),
            tr("OBJ Files (*.obj)")
        );
        if (file_path.isEmpty()) return;
        if (!scene->load_object(file_path))
        {
            QMessageBox::warning(this, tr("Import failed"), tr("Unable to load the selected OBJ file."));
        }
    });

    // Toolbar 2: Help (full-width below)
    addToolBarBreak();  // Place next toolbar on a new row

    QToolBar* help_tool_bar = addToolBar("Help");
    help_tool_bar->setMovable(false);

    help_tool_bar->addSeparator();
    auto *color_source_label = new QLabel("Color source:", help_tool_bar);
    {
        QFont f = color_source_label->font();
        f.setPointSizeF(f.pointSizeF() * 1.05);
        color_source_label->setFont(f);
    }
    color_source_label->setStyleSheet("padding:0 4px;");
    help_tool_bar->addWidget(color_source_label);

    color_mode_combo_box_ = new QComboBox(help_tool_bar);
    color_mode_combo_box_->addItems({QStringLiteral("Uniform"),
                                     QStringLiteral("Position"),
                                     QStringLiteral("Normal"),
                                     QStringLiteral("UV")});
    color_mode_combo_box_->setCurrentIndex(static_cast<int>(View::ColorMode::Uniform));
    color_mode_combo_box_->setFixedWidth(140);
    help_tool_bar->addWidget(color_mode_combo_box_);
    connect(color_mode_combo_box_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [scene](const int index)
            {
                const int clamped = std::clamp(index, 0, 3);
                scene->set_color_mode(static_cast<View::ColorMode>(clamped));
            });

    help_tool_bar->addSeparator();

    const QString help_text =
        "Left mouse button: Orbit scene / deselect   |   Middle mouse button (hold): Orbit scene   |   Right mouse button (+Shift): Pan or drag selected object\n"
        "Mouse wheel: Dolly scene (no selection) or scale selected object   |   Double-click: Select object   |   W/A/S/D, R/F: Move camera   |   I/K, J/L, U/O: Rotate camera";

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