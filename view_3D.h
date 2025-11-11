#ifndef VIEW_3D_H // Guard against multiple inclusion
#define VIEW_3D_H // Begin include guard

#include <QOpenGLWidget>    // QOpenGLWidget base class (custom GL widget)
#include <QOpenGLFunctions_4_5_Core>    // Provides GL 4.5 core functions after initializeOpenGLFunctions()

#include <glm/glm.hpp>  // GLM core types (mat4, vec4, vec3, etc.)
#include <glm/gtc/matrix_transform.hpp> // GLM transformations (translate, rotate, scale, ortho)
#include <glm/gtc/type_ptr.hpp> // glm::value_ptr for sending matrices to shader

#include <QString> // Qt string helper used for UI communication
#include <vector> // STL container storing imported objects

// NOLINTNEXTLINE(readability-duplicate-include)
#include <QMouseEvent> // Qt mouse event type for interactions
#include <QKeyEvent> // Qt keyboard event type for interactions
// NOLINTNEXTLINE(readability-duplicate-include)
#include <QWheelEvent> // Qt wheel event type for zoom/orbit


QT_BEGIN_NAMESPACE // Required by Qt build system for namespace handling


QT_END_NAMESPACE // Complement QT_BEGIN_NAMESPACE

class QOpenGLShaderProgram; // Forward declare shader program (a unique_ptr is kept to it)

class View final : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core // Class View inherits QOpenGLWidget and QOpenGLFunctions_4_5_Core
{
    Q_OBJECT // Enable signals/slots for this widget

public: // Public interface exposed to UI
    enum class ColorMode : int
    {
        Uniform = 0, // Solid object color
        Position = 1, // Color changes with world position
        Normal = 2, // Color shows surface direction
        UV = 3, // Color shows texture coordinates
        PositionNormal = 4 // Color mixes position and normal
    };

    explicit View(QWidget *parent = nullptr);   // Constructor
    ~View() override;   // Destructor

    // Quick setters used by the toolbar (apply + repaint)
    void set_cam_position(float x, float y, float z) { cam_position = {x,y,z}; emit_camera_state(); update(); }
    void set_cam_rotation(float x, float y, float z) { cam_rotation_degree = {x,y,z}; emit_camera_state(); update(); }
    bool load_object(const QString &file_path); // Import OBJ mesh into scene
    void set_color_mode(ColorMode mode); // Update fragment shading data-source

    void reset_all(); // Clear scene and restore defaults

signals: // Qt signal definitions follow
    void cameraPositionChanged(float x, float y, float z); // Signal toolbar when camera position updates
    void cameraRotationChanged(float x, float y, float z); // Signal toolbar when camera rotation updates

private: // Internal helpers and state
    struct ImportedObject
    {
        GLuint vao = 0; // VAO handle for mesh
        GLuint vbo = 0; // VBO handle storing interleaved attributes
        GLsizei vertex_count = 0; // Number of vertices to render
        glm::vec3 translation{}; // World-space position of mesh
        float base_footprint = 1.0f; // Base footprint used for placement spacing
        float radius = 1.0f; // Bounding radius used for picking
        float scale = 1.0f; // Current uniform scale factor
    };

    using QOpenGLFunctions_4_5_Core::glAttachShader; // Expose shader attachment helper
    using QOpenGLFunctions_4_5_Core::glBindBuffer; // Expose buffer binding helper
    using QOpenGLFunctions_4_5_Core::glBindVertexArray; // Expose VAO binding helper
    using QOpenGLFunctions_4_5_Core::glBufferData; // Expose buffer upload helper
    using QOpenGLFunctions_4_5_Core::glClear; // Expose framebuffer clear helper
    using QOpenGLFunctions_4_5_Core::glClearColor; // Expose clear color setter
    using QOpenGLFunctions_4_5_Core::glCompileShader; // Expose shader compilation helper
    using QOpenGLFunctions_4_5_Core::glCreateProgram; // Expose program creation helper
    using QOpenGLFunctions_4_5_Core::glCreateShader; // Expose shader creation helper
    using QOpenGLFunctions_4_5_Core::glDeleteBuffers; // Expose buffer destruction helper
    using QOpenGLFunctions_4_5_Core::glDeleteProgram; // Expose program destruction helper
    using QOpenGLFunctions_4_5_Core::glDeleteShader; // Expose shader destruction helper
    using QOpenGLFunctions_4_5_Core::glDeleteVertexArrays; // Expose VAO destruction helper
    using QOpenGLFunctions_4_5_Core::glDrawArrays; // Expose array drawing helper
    using QOpenGLFunctions_4_5_Core::glEnable; // Expose capability toggling helper
    using QOpenGLFunctions_4_5_Core::glEnableVertexAttribArray; // Expose attribute enable helper
    using QOpenGLFunctions_4_5_Core::glGenBuffers; // Expose buffer generation helper
    using QOpenGLFunctions_4_5_Core::glGenVertexArrays; // Expose VAO generation helper
    using QOpenGLFunctions_4_5_Core::glGetUniformLocation; // Expose uniform lookup helper
    using QOpenGLFunctions_4_5_Core::glLineWidth; // Expose line width state helper
    using QOpenGLFunctions_4_5_Core::glLinkProgram; // Expose program linking helper
    using QOpenGLFunctions_4_5_Core::glShaderSource; // Expose shader source upload helper
    using QOpenGLFunctions_4_5_Core::glUniform1i; // Expose integer uniform setter
    using QOpenGLFunctions_4_5_Core::glUniform4f; // Expose vec4 uniform setter
    using QOpenGLFunctions_4_5_Core::glUniformMatrix3fv; // Expose mat3 uniform setter
    using QOpenGLFunctions_4_5_Core::glUniformMatrix4fv; // Expose mat4 uniform setter
    using QOpenGLFunctions_4_5_Core::glUseProgram; // Expose program binding helper
    using QOpenGLFunctions_4_5_Core::glVertexAttribPointer; // Expose attribute layout helper
    using QOpenGLFunctions_4_5_Core::glViewport; // Expose viewport setter

    GLuint shader_program_id = 0;   // OpenGL shader program ID (compiled+linked GLSL program); it identifies the linked vertex + fragment shader pair used for rendering
    GLint uniform_location_mvp = -1;    // Uniform location for MVP matrix (cached after link)
    GLint uniform_location_color = -1;  // Uniform location for per-draw color (cached after link)
    GLint uniform_location_model = -1; // Cached handle for model matrix uniform
    GLint uniform_location_normal_matrix = -1; // Cached handle for normal matrix uniform
    GLint uniform_location_color_mode = -1; // Cached handle for color mode uniform

    // Raw GL objects
    GLuint vertex_array_object = 0;   // Vertex Array Object handle
    GLuint vertex_buffer_object = 0;   // Vertex Buffer Object handle
    GLuint edge_vertex_array_object = 0;   // Wireframe VAO
    GLuint edge_vertex_buffer_object = 0;  // Wireframe VBO

    std::vector<ImportedObject> imported_objects_; // List of scene meshes
    int selected_object_index_ = -1; // Index of selected object
    bool dragging_object_ = false; // Indicates active object drag
    glm::vec3 drag_offset_{}; // Offset between drag ray and object center

    glm::mat4 projection{};  // Projection matrix
    glm::mat4 view_matrix{}; // View matrix (camera)

    glm::vec3 cam_position = {3.0f, 3.5f, 15.0f};      // Camera position vector
    glm::vec3 cam_rotation_degree = { -15.0f, 15.0f, 0.0f }; // Camera Euler rotation in degrees
    glm::vec3 focus_point_{0.0f, 0.0f, 0.0f}; // Current camera orbit target

    QPoint last_mouse; // Previous mouse position for deltas
    bool rotating = false;   // LMB: orbit camera
    bool panning = false;   // RMB: pan camera
    bool scrolling_navigation_ = false; // Middle-mouse orbit state
    ColorMode color_mode_ = ColorMode::Uniform; // Active color mode enumeration

    void emit_camera_state(); // Emit signals with current camera state
    [[nodiscard]] glm::mat4 build_view_matrix() const; // Construct camera view matrix
    void update_projection(int w, int h); // Recalculate projection matrix

    void setup_shaders();   // Create, compile, link shaders; fetch uniform locations
    void setup_geometry();  // Create VAO/VBO and upload unit-cube vertex data
    void draw_cube(const glm::mat4 &model, const glm::vec4 &color, ColorMode mode);  // Set uniforms and draw 36 vertices for one cube
    void draw_cube_edges(const glm::mat4 &model, const glm::vec4 &color); // Draw cube wireframe
    void draw_mesh(const ImportedObject &object, const glm::mat4 &model, const glm::vec4 &color, ColorMode mode); // Draw imported mesh instance
    void delete_imported_objects(); // Release GPU resources for all meshes
    [[nodiscard]] bool compute_ray(const QPoint &position, glm::vec3 &origin, glm::vec3 &direction) const; // Build picking ray from screen point
    [[nodiscard]] bool intersect_ground_plane(const QPoint &position, glm::vec3 &hit_point) const; // Ray-test against ground plane
    [[nodiscard]] int pick_object(const QPoint &position) const; // Return index of mesh hit by ray

protected: // Overridden event handlers
    void initializeGL() override;   // Called once: load GL functions, create buffers/shaders, states
    void resizeGL(int w, int h) override;   // Called on resize: update viewport/projection
    void paintGL() override;    // Called to render a frame: bind VAO, set uniforms, draw

    void mousePressEvent(QMouseEvent*) override; // Handle mouse button press events
    void mouseReleaseEvent(QMouseEvent*) override; // Handle mouse button release events
    void mouseMoveEvent(QMouseEvent*) override; // Handle mouse movement events
    void mouseDoubleClickEvent(QMouseEvent*) override; // Handle double-click events
    void wheelEvent(QWheelEvent*) override; // Handle wheel scroll events
    void keyPressEvent(QKeyEvent*) override; // Handle keyboard press events
};


#endif //VIEW_3D_H // End include guard