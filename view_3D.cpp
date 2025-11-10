#include "view_3D.h"


View::View(QWidget *parent) : QOpenGLWidget(parent)
{
    setMinimumSize(400, 300);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

View::~View()
{
    makeCurrent();    // Make this widget's OpenGL context current; required so GL calls below operate on the right context

    /* If a vertex buffer (VBO) was created (non-zero ID), delete it from GPU memory to free VRAM
       Then reset its handle to 0 (the “no buffer” default value). */
    if (vertex_buffer_object) glDeleteBuffers(1, &vertex_buffer_object); vertex_buffer_object = 0;
    /* If a vertex array object (VAO) exists, delete it to release GPU state resources.
       Reset the handle to 0 to mark it invalid/unused. */
    if (vertex_array_object) glDeleteVertexArrays(1, &vertex_array_object); vertex_array_object = 0;
    /* If the shader program was successfully created, delete it from the GPU.
       Reset to 0 to indicate no active program is bound to this object anymore. */
    if (shader_program_id) glDeleteProgram(shader_program_id); shader_program_id = 0;

    doneCurrent();    // Release the current OpenGL context; Qt’s cleanup convention after finishing GL operations
}

void View::initializeGL()
{
    initializeOpenGLFunctions();    // Enables 4.5 core entry points via QOpenGLFunctions_4_5_Core

    glEnable(GL_DEPTH_TEST);   // Enable depth test
    glEnable(GL_MULTISAMPLE);   // Emable multisampling
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);    // Set the background color for the next frame (dark blue-gray)

    setup_shaders();
    setup_geometry();

    view_matrix = build_view_matrix(); // Initial camera
}

glm::mat4 View::build_view_matrix() const
{
    glm::mat4 V(1.0f);
    // V = R^{-1} * T^{-1}  (rotate world opposite to camera, then translate opposite)
    V = glm::rotate(V, glm::radians(-cam_rotation_degree.z), glm::vec3(0,0,1)); // Roll
    V = glm::rotate(V, glm::radians(-cam_rotation_degree.x), glm::vec3(1,0,0)); // Pitch
    V = glm::rotate(V, glm::radians(-cam_rotation_degree.y), glm::vec3(0,1,0)); // Yaw
    V = glm::translate(V, -cam_position);
    return V;
}

void View::update_projection(const int w, const int h)
{
    const float aspect = h > 0 ? static_cast<float>(w)/static_cast<float>(h) : 1.0f;
    if (use_perspective)
    {
        projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    }
    else
    {
        const float half_h = 4.0f, half_w = aspect * half_h;
        projection = glm::ortho(-half_w, half_w, -half_h, half_h, -100.0f, 100.0f);
    }
}

void View::resizeGL(const int w, const int h)
{
    glViewport(0,0,w,h);
    update_projection(w,h);
}

glm::mat4 View::build_shear_matrix() const
{
    // Angles are in degrees; use cot = cos/sin. If sin≈0, treat as 0 to avoid blow-up.
    auto cot = [](const float degrees) -> float
    {
        const float radians = glm::radians(degrees);
        const float sine = glm::sin(radians);
        if (glm::abs(sine) < 1e-6f) return 0.0f;
        return glm::cos(radians) / sine;
    };

    const float cot_theta = cot(shear_theta_degree);
    const float cot_phi = cot(shear_phi_degree);

    glm::mat4 H(1.0f);
    switch (shear_plane)
    {
        case ShearPlane::XY:    // Shear vs Z: x+=ct*z, y+=cp*z
            H[2][0] = 0.0f; H[0][2] = cot_theta;    // m[0][2]
            H[2][1] = 0.0f; H[1][2] = cot_phi;  // m[1][2]
            break;
        case ShearPlane::XZ: // Shear vs Y: x+=ct*y, z+=cp*y
            H[1][0] = 0.0f; H[0][1] = cot_theta;
            H[1][2] = 0.0f; H[2][1] = cot_phi;
            break;
        case ShearPlane::YZ:    // Shear vs X: y+=ct*x, z+=cp*x
            H[0][1] = 0.0f; H[1][0] = cot_theta;
            H[0][2] = 0.0f; H[2][0] = cot_phi;
            break;
    }
    return H;
}

// Mp = T * Rz * Ry * Rx * H * S   (so S then H then R then T are applied to points)
glm::mat4 View::build_pyramid_Mp() const
{
    const glm::mat4 S  = glm::scale(glm::mat4(1.0f), glm::vec3(pyramid_scale));
    const glm::mat4 H  = build_shear_matrix();
    const glm::mat4 Rx = glm::rotate(glm::mat4(1.0f), glm::radians(pyramid_rotation_degree.x), glm::vec3(1,0,0));
    const glm::mat4 Ry = glm::rotate(glm::mat4(1.0f), glm::radians(pyramid_rotation_degree.y), glm::vec3(0,1,0));
    const glm::mat4 Rz = glm::rotate(glm::mat4(1.0f), glm::radians(pyramid_rotation_degree.z), glm::vec3(0,0,1));
    const glm::mat4 T  = glm::translate(glm::mat4(1.0f), pyramid_position);

    return T * Rz * Ry * Rx * H * S;
}

void View::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader_program_id);
    glBindVertexArray(vertex_array_object);

    // Update camera matrix every frame (allows live control)
    view_matrix = build_view_matrix();

    // Ground: reuse cube VBO, shrink in Y
    {
        glm::mat4 Mg(1.0f);
        Mg = glm::translate(Mg, glm::vec3(0.0f, -2.0f, 0.0f));   // Place below pyramid
        Mg = glm::scale(Mg, glm::vec3(8.0f, 0.10f, 8.0f));      // Thin and wide
        draw_cube(Mg, glm::vec4(0.55f, 0.55f, 0.55f, 1.0f));
    }

    // Pyramid: 4-3-1 cubes, all via one VBO
    const glm::mat4 Mp = build_pyramid_Mp();

    auto Mk_at_position = [](const float x, const float y, const float z)
    {
        return glm::translate(glm::mat4(1.0f), glm::vec3(x,y,z));
    };

    // Layout params
    constexpr float size = 1.0f;      // Cube size (unit)
    constexpr float gap = 0.00f;   // Tiny spacing
    constexpr float D = size + gap;   // Spacing per axis
    constexpr float base_y = -1.5f;

    // Base row (4)
    draw_cube(Mp * Mk_at_position(-1.5f*D, base_y, 0.0f), glm::vec4(1.00f,0.20f,0.20f,1));
    draw_cube(Mp * Mk_at_position(-0.5f*D, base_y, 0.0f), glm::vec4(0.20f,1.00f,0.20f,1));
    draw_cube(Mp * Mk_at_position( 0.5f*D, base_y, 0.0f), glm::vec4(0.20f,0.60f,1.00f,1));
    draw_cube(Mp * Mk_at_position( 1.5f*D, base_y, 0.0f), glm::vec4(1.00f,0.60f,0.20f,1));

    // 2nd row (3)
    constexpr float y2 = base_y + D;
    draw_cube(Mp * Mk_at_position(-1.0f*D, y2, 0.0f), glm::vec4(0.80f,0.20f,1.00f,1));
    draw_cube(Mp * Mk_at_position( 0.0f*D, y2, 0.0f), glm::vec4(1.00f,0.90f,0.20f,1));
    draw_cube(Mp * Mk_at_position( 1.0f*D, y2, 0.0f), glm::vec4(0.20f,1.00f,0.80f,1));

    // Top (1)
    constexpr float y3 = y2 + D;
    draw_cube(Mp * Mk_at_position(0.0f, y3, 0.0f), glm::vec4(0.90f,0.30f,0.40f,1));

    glBindVertexArray(0);
    glUseProgram(0);
}

void View::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    last_mouse = event->pos();
    rotating = event->button() == Qt::LeftButton;
    panning  = event->button() == Qt::RightButton;
}
void View::mouseReleaseEvent(QMouseEvent*) { rotating = panning = false; }

void View::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint distance = event->pos() - last_mouse;
    last_mouse = event->pos();

    const auto dx = static_cast<float>(distance.x());
    const auto dy = static_cast<float>(distance.y());

    if (rotating)   // Rotate pyramid
    {
        pyramid_rotation_degree.y += 0.3f * dx;
        pyramid_rotation_degree.x += 0.3f * dy;
        update(); return;
    }

    if (panning)    // Translate pyramid (XZ plane; hold Shift for Y)
    {
        if (event->modifiers() & Qt::ShiftModifier)
        {
            pyramid_position.y += -0.01f * dy;
        }
        else
        {
            pyramid_position.x +=  0.01f * dx;
            pyramid_position.z += -0.01f * dy;
        }
        update();
    }
}

void View::wheelEvent(QWheelEvent* event)
{
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;

    if (const auto mods = event->modifiers(); mods & Qt::ControlModifier)
    {
        shear_theta_degree += 2.0f * steps; // θ
    }
    else if (mods & Qt::AltModifier || mods & Qt::ShiftModifier)
    {
        shear_phi_degree   += 2.0f * steps; // φ (Shift)
    }
    else
    {
        pyramid_scale = std::clamp(pyramid_scale * std::pow(1.05f, steps), 0.1f, 10.0f);
    }
    update();

}

void View::keyPressEvent(QKeyEvent* event)
{
    const float move = event->modifiers() & Qt::ShiftModifier ? 0.25f : 0.1f;
    constexpr float rotate  = 2.0f;

    switch (event->key())
    {
        // Camera translation (V)
        case Qt::Key_W: cam_position.z -= move; break;
        case Qt::Key_S: cam_position.z += move; break;
        case Qt::Key_A: cam_position.x -= move; break;
        case Qt::Key_D: cam_position.x += move; break;
        case Qt::Key_R: cam_position.y += move; break;
        case Qt::Key_F: cam_position.y -= move; break;

        // Camera rotation (V)
        case Qt::Key_J: cam_rotation_degree.y -= rotate; break; // yaw-
        case Qt::Key_L: cam_rotation_degree.y += rotate; break; // yaw+
        case Qt::Key_I: cam_rotation_degree.x -= rotate; break; // pitch up
        case Qt::Key_K: cam_rotation_degree.x += rotate; break; // pitch down
        case Qt::Key_U: cam_rotation_degree.z -= rotate; break; // roll-
        case Qt::Key_O: cam_rotation_degree.z += rotate; break; // roll+

        // Projection toggle
        case Qt::Key_P:
            use_perspective = !use_perspective;
            update_projection(width(), height());
            update();
            break;

        // Shear plane selection
        case Qt::Key_1: shear_plane = ShearPlane::XY; break;
        case Qt::Key_2: shear_plane = ShearPlane::XZ; break;
        case Qt::Key_3: shear_plane = ShearPlane::YZ; break;

        default: return;
    }
    update();
}

void View::reset_all()
{
    pyramid_scale = 1.0f;
    shear_theta_degree = 0.0f;
    shear_phi_degree = 0.0f;
    shear_plane = ShearPlane::XY;
    pyramid_rotation_degree = {0.0f, 0.0f, 0.0f};
    pyramid_position = {0.0f, 0.0f, 0.0f};

    cam_position = {3.0f, 2.5f, 11.0f};
    cam_rotation_degree = {-15.0f, 15.0f, 0.0f};

    use_perspective = true;
    update_projection(width(), height());
    update();
}

void View::setup_shaders()
{
    /* Vertex shader: transforms vertex position to clip space using MVP matrix */
    static auto vertex_shader_source = R"(#version 450 core
    layout(location = 0) in vec3 position;
    uniform mat4 mvp;
    void main()
    {
        gl_Position = mvp * vec4(position, 1.0);
    }
    )";

    /* Fragment shader: outputs a uniform color for the square */
    static auto fragment_shader_source = R"(#version 450 core
    uniform vec4 color;
    out vec4 FragColor;
    void main()
    {
        FragColor = color;
    }
    )";

    shader_program_id = glCreateProgram();  // Create a new OpenGL shader program
    const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, nullptr);
    glCompileShader(vertex_shader); // Compile vertex shader
    glAttachShader(shader_program_id, vertex_shader);   // Attach compiled vertex shader
    const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, nullptr);
    glCompileShader(fragment_shader);   // Compile fragment shader
    glAttachShader(shader_program_id, fragment_shader); // Attach compiled fragment shader
    glLinkProgram(shader_program_id);   // Link vertex + fragment into program
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    // Cache uniform locations
    uniform_location_mvp = glGetUniformLocation(shader_program_id, "mvp");
    uniform_location_color = glGetUniformLocation(shader_program_id, "color");
}

void View::setup_geometry()
{
    // Define a unit cube (side length 1) using 12 triangles (2 triangles per face).
    /* Each face's vertices are specified in counter-clockwise order as seen from outside, so that
       the outward face is considered the front side (for face culling if enabled). */
    constexpr GLfloat unit_cube_vertices[36 * 3] =
    {
        // Front face (-Z)
        -0.5f, -0.5f, -0.5f,  // Bottom-left-front
        -0.5f,  0.5f, -0.5f,  // Top-left-front
         0.5f,  0.5f, -0.5f,  // Top-right-front
        -0.5f, -0.5f, -0.5f,  // Bottom-left-front (reused)
         0.5f,  0.5f, -0.5f,  // Top-right-front
         0.5f, -0.5f, -0.5f,  // Bottom-right-front

        // Right face (+X)
         0.5f, -0.5f, -0.5f,  // Bottom-right-front
         0.5f,  0.5f, -0.5f,  // Top-right-front
         0.5f,  0.5f,  0.5f,  // Top-right-back
         0.5f, -0.5f, -0.5f,  // Bottom-right-front (reused)
         0.5f,  0.5f,  0.5f,  // Top-right-back
         0.5f, -0.5f,  0.5f,  // Bottom-right-back

        // Back face (+Z)
        -0.5f, -0.5f,  0.5f,  // Bottom-left-back
         0.5f, -0.5f,  0.5f,  // Bottom-right-back
         0.5f,  0.5f,  0.5f,  // Top-right-back
        -0.5f, -0.5f,  0.5f,  // Bottom-left-back  (reused)
         0.5f,  0.5f,  0.5f,  // Top-right-back
        -0.5f,  0.5f,  0.5f,  // Top-left-back

        // Left face (-X)
        -0.5f, -0.5f,  0.5f,  // Bottom-left-back
        -0.5f,  0.5f,  0.5f,  // Top-left-back
        -0.5f,  0.5f, -0.5f,  // Top-left-front
        -0.5f, -0.5f,  0.5f,  // Bottom-left-back  (reused)
        -0.5f,  0.5f, -0.5f,  // Top-left-front
        -0.5f, -0.5f, -0.5f,  // Bottom-left-front

        // Top face (+Y)
        -0.5f,  0.5f, -0.5f,  // Top-left-front
        -0.5f,  0.5f,  0.5f,  // Top-left-back
         0.5f,  0.5f,  0.5f,  // Top-right-back
        -0.5f,  0.5f, -0.5f,  // Top-left-front   (reused)
         0.5f,  0.5f,  0.5f,  // Top-right-back
         0.5f,  0.5f, -0.5f,  // Top-right-front

        // Bottom face (-Y)
        -0.5f, -0.5f, -0.5f,  // Bottom-left-front
         0.5f, -0.5f, -0.5f,  // Bottom-right-front
         0.5f, -0.5f,  0.5f,  // Bottom-right-back
        -0.5f, -0.5f, -0.5f,  // Bottom-left-front  (reused)
         0.5f, -0.5f,  0.5f,  // Bottom-right-back
        -0.5f, -0.5f,  0.5f   // Bottom-left-back
    };

    // Generate and bind VAO and VBO, then upload cube vertex data
    glGenVertexArrays(1, &vertex_array_object);
    glBindVertexArray(vertex_array_object);

    // VBO: upload vertex data once
    glGenBuffers(1, &vertex_buffer_object);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_object);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unit_cube_vertices), unit_cube_vertices,GL_STATIC_DRAW);

    // Vertex format: layout(location=0) = vec3 position, tightly packed
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,                      // Attribute index (matches shader layout(location=0))
        3,                      // 3 floats per vertex (x,y,z)
        GL_FLOAT,               // Data type
        GL_FALSE,               // No normalization
        3 * sizeof(GLfloat),    // Stride: tightly packed vec3
        nullptr                 // Offset into buffer
    );

    // Clean up binds
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void View::draw_cube(const glm::mat4 &model, const glm::vec4 &color) // Draw one cube with a given transform and RGBA color
{
    const glm::mat4 mvp = projection * view_matrix * model;    // Build the final transform: Model→View→Projection (rightmost applied first)

    // Send uniforms with raw GL calls
    glUniformMatrix4fv(uniform_location_mvp, 1, GL_FALSE, glm::value_ptr(mvp)); // Upload a 4x4 float matrix uniform to the current shader program
    /* Location of "mvp" uniform, number of matrices being sent (just one), do NOT transpose
       (QMatrix4x4 is already column-major for OpenGL), pointer to the 16 floats (contiguous) inside QMatrix4x4 */
    glUniform4f(uniform_location_color, color.r, color.g, color.b, color.a);    // Upload a vec4 uniform to the fragment shader (the color)
    // Location of "color" uniform, RGBA components as floats in [0,1]

    glDrawArrays(GL_TRIANGLES, 0, 36);  // Draw 36 vertices (12 triangles) for one cube
    /* Each draw call reuses the cube VBO: interpret the vertex data as 12 independent
       triangles (starting at vertex 0) that form a complete cube. */
}
