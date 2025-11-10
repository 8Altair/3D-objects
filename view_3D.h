#ifndef VIEW_3D_H
#define VIEW_3D_H

#include <QOpenGLWidget>    // QOpenGLWidget base class (custom GL widget)
#include <QOpenGLFunctions_4_5_Core>    // Provides GL 4.5 core functions after initializeOpenGLFunctions()

#include <glm/glm.hpp>  // GLM core types (mat4, vec4, vec3, etc.)
#include <glm/gtc/matrix_transform.hpp> // GLM transformations (translate, rotate, scale, ortho)
#include <glm/gtc/type_ptr.hpp> // glm::value_ptr for sending matrices to shader

#include <QString>
#include <vector>

// NOLINTNEXTLINE(readability-duplicate-include)
#include <QMouseEvent>
#include <QKeyEvent>
// NOLINTNEXTLINE(readability-duplicate-include)
#include <QWheelEvent>


QT_BEGIN_NAMESPACE


QT_END_NAMESPACE

class QOpenGLShaderProgram; // Forward declare shader program (a unique_ptr is kept to it)

class View final : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core // Class View inherits QOpenGLWidget and QOpenGLFunctions_4_5_Core
{
    Q_OBJECT

public:
    enum class ColorMode : int
    {
        Uniform = 0,
        Position = 1,
        Normal = 2,
        UV = 3
    };

    explicit View(QWidget *parent = nullptr);   // Constructor
    ~View() override;   // Destructor

    // Quick setters used by the toolbar (apply + repaint)
    void set_cam_position(float x, float y, float z) { cam_position = {x,y,z}; emit_camera_state(); update(); }
    void set_cam_rotation(float x, float y, float z) { cam_rotation_degree = {x,y,z}; emit_camera_state(); update(); }
    bool load_object(const QString &file_path);
    void set_color_mode(ColorMode mode);

    void reset_all();

signals:
    void cameraPositionChanged(float x, float y, float z);
    void cameraRotationChanged(float x, float y, float z);

private:
    struct ImportedObject
    {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei vertex_count = 0;
        glm::vec3 translation{};
        float base_footprint = 1.0f;
        float radius = 1.0f;
        float scale = 1.0f;
    };

    using QOpenGLFunctions_4_5_Core::glAttachShader;
    using QOpenGLFunctions_4_5_Core::glBindBuffer;
    using QOpenGLFunctions_4_5_Core::glBindVertexArray;
    using QOpenGLFunctions_4_5_Core::glBufferData;
    using QOpenGLFunctions_4_5_Core::glClear;
    using QOpenGLFunctions_4_5_Core::glClearColor;
    using QOpenGLFunctions_4_5_Core::glCompileShader;
    using QOpenGLFunctions_4_5_Core::glCreateProgram;
    using QOpenGLFunctions_4_5_Core::glCreateShader;
    using QOpenGLFunctions_4_5_Core::glDeleteBuffers;
    using QOpenGLFunctions_4_5_Core::glDeleteProgram;
    using QOpenGLFunctions_4_5_Core::glDeleteShader;
    using QOpenGLFunctions_4_5_Core::glDeleteVertexArrays;
    using QOpenGLFunctions_4_5_Core::glDrawArrays;
    using QOpenGLFunctions_4_5_Core::glEnable;
    using QOpenGLFunctions_4_5_Core::glEnableVertexAttribArray;
    using QOpenGLFunctions_4_5_Core::glGenBuffers;
    using QOpenGLFunctions_4_5_Core::glGenVertexArrays;
    using QOpenGLFunctions_4_5_Core::glGetUniformLocation;
    using QOpenGLFunctions_4_5_Core::glLineWidth;
    using QOpenGLFunctions_4_5_Core::glLinkProgram;
    using QOpenGLFunctions_4_5_Core::glShaderSource;
    using QOpenGLFunctions_4_5_Core::glUniform1i;
    using QOpenGLFunctions_4_5_Core::glUniform4f;
    using QOpenGLFunctions_4_5_Core::glUniformMatrix3fv;
    using QOpenGLFunctions_4_5_Core::glUniformMatrix4fv;
    using QOpenGLFunctions_4_5_Core::glUseProgram;
    using QOpenGLFunctions_4_5_Core::glVertexAttribPointer;
    using QOpenGLFunctions_4_5_Core::glViewport;

    GLuint shader_program_id = 0;   // OpenGL shader program ID (compiled+linked GLSL program); it identifies the linked vertex + fragment shader pair used for rendering
    GLint uniform_location_mvp = -1;    // Uniform location for MVP matrix (cached after link)
    GLint uniform_location_color = -1;  // Uniform location for per-draw color (cached after link)
    GLint uniform_location_model = -1;
    GLint uniform_location_normal_matrix = -1;
    GLint uniform_location_color_mode = -1;

    // Raw GL objects
    GLuint vertex_array_object = 0;   // Vertex Array Object handle
    GLuint vertex_buffer_object = 0;   // Vertex Buffer Object handle
    GLuint edge_vertex_array_object = 0;   // Wireframe VAO
    GLuint edge_vertex_buffer_object = 0;  // Wireframe VBO

    std::vector<ImportedObject> imported_objects_;
    int selected_object_index_ = -1;
    bool dragging_object_ = false;
    glm::vec3 drag_offset_{};

    glm::mat4 projection{};  // Projection matrix
    glm::mat4 view_matrix{}; // View matrix (camera)

    glm::vec3 cam_position = {3.0f, 3.5f, 15.0f};      // Camera position
    glm::vec3 cam_rotation_degree = { -15.0f, 15.0f, 0.0f }; // pitch,yaw,roll (deg), small tilt
    glm::vec3 focus_point_{0.0f, 0.0f, 0.0f};

    QPoint last_mouse;
    bool rotating = false;   // LMB: orbit camera
    bool panning  = false;   // RMB: pan camera
    bool scrolling_navigation_ = false;
    ColorMode color_mode_ = ColorMode::Uniform;

    void emit_camera_state();
    [[nodiscard]] glm::mat4 build_view_matrix() const;
    void update_projection(int w, int h);

    void setup_shaders();   // Create, compile, link shaders; fetch uniform locations
    void setup_geometry();  // Create VAO/VBO and upload unit-cube vertex data
    void draw_cube(const glm::mat4 &model, const glm::vec4 &color, ColorMode mode);  // Set uniforms and draw 36 vertices for one cube
    void draw_cube_edges(const glm::mat4 &model, const glm::vec4 &color);
    void draw_mesh(const ImportedObject &object, const glm::mat4 &model, const glm::vec4 &color, ColorMode mode);
    void delete_imported_objects();
    [[nodiscard]] bool compute_ray(const QPoint &position, glm::vec3 &origin, glm::vec3 &direction) const;
    [[nodiscard]] bool intersect_ground_plane(const QPoint &position, glm::vec3 &hit_point) const;
    [[nodiscard]] int pick_object(const QPoint &position) const;

protected:
    void initializeGL() override;   // Called once: load GL functions, create buffers/shaders, states
    void resizeGL(int w, int h) override;   // Called on resize: update viewport/projection
    void paintGL() override;    // Called to render a frame: bind VAO, set uniforms, draw

    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
};


#endif //VIEW_3D_H