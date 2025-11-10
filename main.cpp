#include "main_window.h"    // Main window class declaration/definition

#include <QApplication> // Qt application runtime (event loop, rendering integration)
#include <QSurfaceFormat>   // Request an OpenGL context format (version/profile/buffers)

#include <QIcon>


int main(int argc, char *argv[])    // Standard Qt/desktop app entry point
{
    // Request an OpenGL 4.6 Core context before creating the app
    QSurfaceFormat surface_format;  // Create a format descriptor used as the default
    surface_format.setRenderableType(QSurfaceFormat::OpenGL);   // Ask for a desktop OpenGL context
    surface_format.setVersion(4, 6);    // Request OpenGL version 4.6 (major=4, minor=6) — not guaranteed
    surface_format.setProfile(QSurfaceFormat::CoreProfile); // Ask for a Core profile (no deprecated fixed-function pipeline)
    surface_format.setDepthBufferSize(24);  // Ask for a 24-bit depth buffer (useful when doing 3D)
    surface_format.setSamples(8);   // Anti-aliasing; 8 samples per pixel
    QSurfaceFormat::setDefaultFormat(surface_format);   // Make this the default for all windows/contexts created afterward

    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);    // Force desktop OpenGL
    QApplication app(argc, argv);   // Construct the Qt application (now picks up the default GL format)
    QApplication::setWindowIcon(QIcon(":/icons/Icon/Icon.png"));
    MainWindow w;   // Create main window
    w.setWindowIcon(QIcon(":/icons/Icon/Icon.png"));
    w.resize(1250, 720);    // Initial window size (pixels)
    w.show();   // Show the window (triggers widget creation and, later, GL context creation)
    return QApplication::exec();    // Enter Qt’s event loop (blocks until the app quits)
}
