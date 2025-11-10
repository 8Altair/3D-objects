#include "view_3D.h"

#include <QDebug>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>


View::View(QWidget *parent) : QOpenGLWidget(parent)
{
    setMinimumSize(400, 300);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

View::~View()
{
    makeCurrent();    // Make this widget's OpenGL context current; required so GL calls below operate on the right context

    delete_imported_objects();

    /* If a vertex buffer (VBO) was created (non-zero ID), delete it from GPU memory to free VRAM
       Then reset its handle to 0 (the “no buffer” default value). */
    if (vertex_buffer_object) glDeleteBuffers(1, &vertex_buffer_object); vertex_buffer_object = 0;
    /* If a vertex array object (VAO) exists, delete it to release GPU state resources.
       Reset the handle to 0 to mark it invalid/unused. */
    if (vertex_array_object) glDeleteVertexArrays(1, &vertex_array_object); vertex_array_object = 0;
    if (edge_vertex_buffer_object) glDeleteBuffers(1, &edge_vertex_buffer_object); edge_vertex_buffer_object = 0;
    if (edge_vertex_array_object) glDeleteVertexArrays(1, &edge_vertex_array_object); edge_vertex_array_object = 0;
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

void View::emit_camera_state()
{
    emit cameraPositionChanged(cam_position.x, cam_position.y, cam_position.z);
    emit cameraRotationChanged(cam_rotation_degree.x, cam_rotation_degree.y, cam_rotation_degree.z);
}

void View::update_projection(const int w, const int h)
{
    const float aspect = h > 0 ? static_cast<float>(w)/static_cast<float>(h) : 1.0f;
    projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
}

void View::resizeGL(const int w, const int h)
{
    glViewport(0,0,w,h);
    update_projection(w,h);
}

void View::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader_program_id);
    glBindVertexArray(vertex_array_object);

    // Update camera matrix every frame (allows live control)
    view_matrix = build_view_matrix();

    // Ground plane
    {
        constexpr float ground_extent = 12.0f;
        glm::mat4 Mg(1.0f);
        Mg = glm::translate(Mg, glm::vec3(0.0f, -2.0f, 0.0f));   // Slightly below origin
        Mg = glm::scale(Mg, glm::vec3(ground_extent, 0.30f, ground_extent));
        draw_cube(Mg, glm::vec4(15.0f/255.0f, 43.0f/255.0f, 70.0f/255.0f, 1.0f));
        glLineWidth(2.0f);
        draw_cube_edges(Mg, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        glLineWidth(1.0f);
    }

    int object_index = 0;
    for (const auto &object : imported_objects_)
    {
        const float r = 0.6f + 0.15f * static_cast<float>(object_index % 3);
        const float g = 0.65f + 0.12f * static_cast<float>((object_index + 1) % 3);
        constexpr float b = 0.75f;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), object.translation);
        draw_mesh(object, model, glm::vec4(r, g, b, 1.0f));
        object_index++;
    }

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

    if (rotating)   // Orbit camera
    {
        cam_rotation_degree.y += 0.3f * dx;
        cam_rotation_degree.x += 0.3f * dy;
        emit_camera_state();
        update(); return;
    }

    if (panning)    // Pan camera (XZ plane; hold Shift for Y)
    {
        if (event->modifiers() & Qt::ShiftModifier)
        {
            cam_position.y += -0.01f * dy;
        }
        else
        {
            cam_position.x +=  0.01f * dx;
            cam_position.z +=  0.01f * dy;
        }
        emit_camera_state();
        update();
    }
}

void View::wheelEvent(QWheelEvent* event)
{
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;
    cam_position.z += -0.5f * steps;
    emit_camera_state();
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

        default: return;
    }
    emit_camera_state();
    update();
}

void View::reset_all()
{
    makeCurrent();
    delete_imported_objects();
    doneCurrent();

    cam_position = {3.0f, 3.5f, 15.0f};
    cam_rotation_degree = {-15.0f, 15.0f, 0.0f};
    update_projection(width(), height());
    emit_camera_state();
    update();
}

bool View::load_object(const QString &file_path)
{
    Assimp::Importer importer;
    constexpr unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_ImproveCacheLocality;

    const aiScene *scene = importer.ReadFile(file_path.toStdString(), flags);
    if (!scene || !scene->HasMeshes())
    {
        qWarning() << "Assimp failed to load OBJ:" << QString::fromStdString(importer.GetErrorString());
        return false;
    }

    const aiMesh *mesh = scene->mMeshes[0];
    if (!mesh || !mesh->HasPositions())
    {
        qWarning() << "OBJ mesh has no positions.";
        return false;
    }

    std::vector<float> vertices;
    vertices.reserve(mesh->mNumFaces * 3 * 3);

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();

    for (unsigned int faceIndex(0); faceIndex < mesh->mNumFaces; faceIndex++)
    {
        const aiFace &face = mesh->mFaces[faceIndex];
        if (face.mNumIndices < 3) continue;

        for (unsigned int i(0); i < face.mNumIndices; i++)
        {
            const unsigned int vertex_index = face.mIndices[i];
            if (vertex_index >= mesh->mNumVertices) continue;

            const aiVector3D &vertex = mesh->mVertices[vertex_index];

            min_x = std::min(min_x, vertex.x);
            min_y = std::min(min_y, vertex.y);
            min_z = std::min(min_z, vertex.z);
            max_x = std::max(max_x, vertex.x);
            max_y = std::max(max_y, vertex.y);
            max_z = std::max(max_z, vertex.z);

            vertices.push_back(vertex.x);
            vertices.push_back(vertex.y);
            vertices.push_back(vertex.z);
        }
    }

    if (vertices.empty())
    {
        qWarning() << "OBJ contains no triangles.";
        return false;
    }

    const float center_x = 0.5f * (min_x + max_x);
    const float center_z = 0.5f * (min_z + max_z);
    for (std::size_t i = 0; i < vertices.size(); i += 3)
    {
        vertices[i + 0] -= center_x;
        vertices[i + 1] -= min_y;
        vertices[i + 2] -= center_z;
    }

    ImportedObject object;
    object.vertex_count = static_cast<GLsizei>(vertices.size() / 3);
    object.footprint = std::max({1.0f, max_x - min_x, max_z - min_z}) + 0.5f;

    makeCurrent();

    glGenVertexArrays(1, &object.vao);
    glBindVertexArray(object.vao);

    glGenBuffers(1, &object.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, object.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    constexpr float ground_top = -2.0f + 0.15f;
    glm::vec3 desired_translation{0.0f, ground_top, 0.0f};

    const auto overlaps = [this](const glm::vec3 &position)
    {
        constexpr float epsilon = 0.05f;
        return std::ranges::any_of(imported_objects_,
                                   [&](const ImportedObject &existing)
                                   {
                                       return std::abs(existing.translation.x - position.x) < epsilon &&
                                           std::abs(existing.translation.z - position.z) < epsilon;
                                   });
    };

    while (overlaps(desired_translation))
    {
        desired_translation.x += object.footprint;
    }

    object.translation = desired_translation;
    imported_objects_.push_back(object);

    doneCurrent();
    update();
    return true;
}

void View::delete_imported_objects()
{
    for (auto &object : imported_objects_)
    {
        if (object.vbo)
        {
            glDeleteBuffers(1, &object.vbo);
            object.vbo = 0;
        }
        if (object.vao)
        {
            glDeleteVertexArrays(1, &object.vao);
            object.vao = 0;
        }
    }
    imported_objects_.clear();
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

    // Edge (wireframe) geometry: 12 segments describing cube edges
    constexpr GLfloat cube_edge_vertices[12 * 2 * 3] =
    {
        // Bottom rectangle
        -0.5f, -0.5f, -0.5f,   0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,   0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f,  -0.5f, -0.5f,  0.5f,
        -0.5f, -0.5f,  0.5f,  -0.5f, -0.5f, -0.5f,
        // Top rectangle
        -0.5f,  0.5f, -0.5f,   0.5f,  0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,   0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,  -0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f,  -0.5f,  0.5f, -0.5f,
        // Vertical edges
        -0.5f, -0.5f, -0.5f,  -0.5f,  0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,   0.5f,  0.5f, -0.5f,
         0.5f, -0.5f,  0.5f,   0.5f,  0.5f,  0.5f,
        -0.5f, -0.5f,  0.5f,  -0.5f,  0.5f,  0.5f
    };

    glGenVertexArrays(1, &edge_vertex_array_object);
    glBindVertexArray(edge_vertex_array_object);

    glGenBuffers(1, &edge_vertex_buffer_object);
    glBindBuffer(GL_ARRAY_BUFFER, edge_vertex_buffer_object);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube_edge_vertices), cube_edge_vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), nullptr);

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

void View::draw_cube_edges(const glm::mat4 &model, const glm::vec4 &color)
{
    const glm::mat4 mvp = projection * view_matrix * model;

    glBindVertexArray(edge_vertex_array_object);
    glUniformMatrix4fv(uniform_location_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform4f(uniform_location_color, color.r, color.g, color.b, color.a);
    glDrawArrays(GL_LINES, 0, 12 * 2);
    glBindVertexArray(vertex_array_object);
}

void View::draw_mesh(const ImportedObject &object, const glm::mat4 &model, const glm::vec4 &color)
{
    if (object.vertex_count <= 0) return;
    const glm::mat4 mvp = projection * view_matrix * model;
    glBindVertexArray(object.vao);
    glUniformMatrix4fv(uniform_location_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform4f(uniform_location_color, color.r, color.g, color.b, color.a);
    glDrawArrays(GL_TRIANGLES, 0, object.vertex_count);
    glBindVertexArray(0);
}
