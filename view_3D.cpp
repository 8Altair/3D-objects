#include "view_3D.h"

#include <QDebug>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <string>

namespace {
constexpr float kGroundPlaneY = -2.0f + 0.15f;
constexpr float kGroundExtent = 12.0f;
constexpr float kMinObjectScale = 0.25f;
constexpr float kMaxObjectScale = 8.0f;
}

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
        glm::mat4 Mg(1.0f);
        Mg = glm::translate(Mg, glm::vec3(0.0f, -2.0f, 0.0f));   // Slightly below origin
        Mg = glm::scale(Mg, glm::vec3(kGroundExtent, 0.30f, kGroundExtent));
        draw_cube(Mg, glm::vec4(15.0f/255.0f, 43.0f/255.0f, 70.0f/255.0f, 1.0f),
                  ColorMode::Uniform);
        glLineWidth(2.0f);
        draw_cube_edges(Mg, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        glLineWidth(1.0f);
    }

    int object_index = 0;
    for (const auto &object : imported_objects_)
    {
        const bool is_selected = object_index == selected_object_index_;
        const float r = is_selected ? 0.95f : 0.6f + 0.15f * static_cast<float>(object_index % 3);
        const float g = is_selected ? 0.85f : 0.65f + 0.12f * static_cast<float>((object_index + 1) % 3);
        const float b = is_selected ? 0.35f : 0.75f;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), object.translation);
        draw_mesh(object, model, glm::vec4(r, g, b, 1.0f),
                  color_mode_);
        object_index++;
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

void View::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    last_mouse = event->pos();

    if (event->button() == Qt::RightButton)
    {
        if (selected_object_index_ >= 0 && selected_object_index_ < static_cast<int>(imported_objects_.size()))
        {
            if (glm::vec3 hit; intersect_ground_plane(event->pos(), hit))
            {
                dragging_object_ = true;
                drag_offset_ = imported_objects_[selected_object_index_].translation - hit;
            }
            else
            {
                dragging_object_ = false;
                panning = true;
            }
        }
        else
        {
            panning = true;
        }
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        if (selected_object_index_ >= 0 && selected_object_index_ < static_cast<int>(imported_objects_.size()))
        {
            if (const int hit_index = pick_object(event->pos()); hit_index < 0)
            {
                selected_object_index_ = -1;
                focus_point_ = {0.0f, 0.0f, 0.0f};
                update();
            }
        }
        rotating = true;
        return;
    }

    if (event->button() == Qt::MiddleButton)
    {
        scrolling_navigation_ = true;
    }
}

void View::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        rotating = false;
        return;
    }

    if (event->button() == Qt::RightButton)
    {
        if (dragging_object_)
        {
            dragging_object_ = false;
        }
        else
        {
            panning = false;
        }
        return;
    }

    if (event->button() == Qt::MiddleButton)
    {
        scrolling_navigation_ = false;
    }
}

void View::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint distance = event->pos() - last_mouse;
    last_mouse = event->pos();

    if (dragging_object_ && selected_object_index_ >= 0 &&
        selected_object_index_ < static_cast<int>(imported_objects_.size()))
    {
        if (glm::vec3 hit; intersect_ground_plane(event->pos(), hit))
        {
            auto &object = imported_objects_[selected_object_index_];
            glm::vec3 new_translation = hit + drag_offset_;
            new_translation.y = kGroundPlaneY;
            object.translation = new_translation;
            update();
        }
        return;
    }

    const auto dx = static_cast<float>(distance.x());
    const auto dy = static_cast<float>(distance.y());

    if (scrolling_navigation_)
    {
        constexpr float orbit_speed = 0.005f;
        constexpr float min_radius = 0.25f;

        glm::vec3 offset = cam_position - focus_point_;
        const float height = offset.y;
        const float radius = std::max(glm::length(glm::vec2(offset.x, offset.z)), min_radius);

        float yaw = glm::radians(cam_rotation_degree.y);
        yaw -= dx * orbit_speed;

        offset.x = radius * std::sin(yaw);
        offset.y = height;
        offset.z = radius * std::cos(yaw);

        cam_position = focus_point_ + offset;
        cam_rotation_degree.y = glm::degrees(yaw);
        emit_camera_state();
        update();
        return;
    }

    if (rotating)   // Orbit camera
    {
        cam_rotation_degree.y += 0.3f * dx;
        cam_rotation_degree.x += 0.3f * dy;
        emit_camera_state();
        update();
        return;
    }

    if (panning)    // Pan camera (XZ plane; hold Shift for vertical)
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

void View::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (const int hit_index = pick_object(event->pos()); hit_index >= 0)
        {
            selected_object_index_ = hit_index;
            focus_point_ = imported_objects_[hit_index].translation;
            dragging_object_ = false;
            rotating = false;
            update();
            return;
        }
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void View::wheelEvent(QWheelEvent* event)
{
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;
    if (std::abs(steps) < std::numeric_limits<float>::epsilon())
    {
        return;
    }

    if (selected_object_index_ >= 0 && selected_object_index_ < static_cast<int>(imported_objects_.size()))
    {
        auto &object = imported_objects_[selected_object_index_];
        const float factor = std::pow(1.1f, steps);
        object.scale = std::clamp(object.scale * factor, kMinObjectScale, kMaxObjectScale);
        update();
        return;
    }

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
    selected_object_index_ = -1;
    dragging_object_ = false;
    rotating = false;
    panning = false;
    scrolling_navigation_ = false;
    focus_point_ = {0.0f, 0.0f, 0.0f};
    color_mode_ = ColorMode::Uniform;
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

    struct VertexData
    {
        glm::vec3 position{};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec2 uv{0.0f, 0.0f};
    };

    std::vector<VertexData> vertices;
    vertices.reserve(mesh->mNumFaces * 3);

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();
    float max_radius_sq = 0.0f;
    const bool has_normals = mesh->HasNormals();
    const bool has_uvs = mesh->HasTextureCoords(0);

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

            VertexData data;
            data.position = {vertex.x, vertex.y, vertex.z};
            if (has_normals)
            {
                const aiVector3D &normal = mesh->mNormals[vertex_index];
                data.normal = {normal.x, normal.y, normal.z};
            }
            if (has_uvs)
            {
                const aiVector3D &uv = mesh->mTextureCoords[0][vertex_index];
                data.uv = {uv.x, uv.y};
            }
            vertices.push_back(data);
        }
    }

    if (vertices.empty())
    {
        qWarning() << "OBJ contains no triangles.";
        return false;
    }

    const float center_x = 0.5f * (min_x + max_x);
    const float center_z = 0.5f * (min_z + max_z);
    for (auto &vertex : vertices)
    {
        vertex.position.x -= center_x;
        vertex.position.y -= min_y;
        vertex.position.z -= center_z;
        max_radius_sq = std::max(max_radius_sq, glm::dot(vertex.position, vertex.position));
    }

    ImportedObject object;
    object.vertex_count = static_cast<GLsizei>(vertices.size());
    object.base_footprint = std::max({1.0f, max_x - min_x, max_z - min_z}) + 0.5f;
    object.radius = std::sqrt(max_radius_sq);

    std::vector<float> interleaved;
    interleaved.reserve(vertices.size() * 8);
    for (const auto & [position, normal, uv] : vertices)
    {
        interleaved.push_back(position.x);
        interleaved.push_back(position.y);
        interleaved.push_back(position.z);
        interleaved.push_back(normal.x);
        interleaved.push_back(normal.y);
        interleaved.push_back(normal.z);
        interleaved.push_back(uv.x);
        interleaved.push_back(uv.y);
    }

    makeCurrent();

    glGenVertexArrays(1, &object.vao);
    glBindVertexArray(object.vao);

    glGenBuffers(1, &object.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, object.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)), interleaved.data(), GL_STATIC_DRAW);

    constexpr GLsizei stride = 8 * sizeof(GLfloat);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(6 * sizeof(GLfloat)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glm::vec3 desired_translation{0.0f, kGroundPlaneY, 0.0f};

    const auto overlaps = [this, &object](const glm::vec3 &position)
    {
        constexpr float epsilon = 0.05f;
        const float new_radius = object.base_footprint * object.scale * 0.5f;
        return std::ranges::any_of(imported_objects_,
                                   [&](const ImportedObject &existing)
                                   {
                                       const float existing_radius = existing.base_footprint * existing.scale * 0.5f;
                                       const glm::vec2 delta(existing.translation.x - position.x,
                                                             existing.translation.z - position.z);
                                       return glm::length(delta) < existing_radius + new_radius + epsilon;
                                   });
    };

    while (overlaps(desired_translation))
    {
        desired_translation.x += object.base_footprint * object.scale;
    }

    object.translation = desired_translation;
    imported_objects_.push_back(object);

    doneCurrent();
    update();
    return true;
}

void View::set_color_mode(const ColorMode mode)
{
    if (color_mode_ == mode) return;
    color_mode_ = mode;
    update();
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
    selected_object_index_ = -1;
    dragging_object_ = false;
}

void View::setup_shaders()
{
    static auto vertex_shader_source = R"(#version 450 core
    layout(location = 0) in vec3 position;
    layout(location = 1) in vec3 normal;
    layout(location = 2) in vec2 texcoord;

    uniform mat4 model;
    uniform mat4 mvp;
    uniform mat3 normal_matrix;

    out vec3 vWorldPosition;
    out vec3 vNormal;
    out vec2 vTexCoord;

    void main()
    {
        vec4 world_position = model * vec4(position, 1.0);
        vWorldPosition = world_position.xyz;
        vNormal = normalize(normal_matrix * normal);
        vTexCoord = texcoord;
        gl_Position = mvp * vec4(position, 1.0);
    }
    )";

    static auto fragment_shader_source = R"(#version 450 core
    layout(location = 0) out vec4 FragColor;

    in vec3 vWorldPosition;
    in vec3 vNormal;
    in vec2 vTexCoord;

    uniform vec4 color;
    uniform int color_mode;

    vec3 encode_position()
    {
        float length_value = length(vWorldPosition);
        if (length_value > 1e-5)
        {
            vec3 normalized = clamp(vWorldPosition / length_value, vec3(-1.0), vec3(1.0));
            return 0.5 + 0.5 * normalized;
        }
        return vec3(0.5);
    }

    vec3 encode_normal()
    {
        float length_value = length(vNormal);
        vec3 normalized = length_value > 1e-5 ? normalize(vNormal) : vec3(0.0, 1.0, 0.0);
        return 0.5 + 0.5 * normalized;
    }

    vec3 encode_uv()
    {
        vec2 wrapped = fract(vTexCoord);
        return vec3(wrapped, 0.5);
    }

    void main()
    {
        vec3 final_color = color.rgb;

        if (color_mode == 1)
        {
            final_color = encode_position();
        }
        else if (color_mode == 2)
        {
            final_color = encode_normal();
        }
        else if (color_mode == 3)
        {
            final_color = encode_uv();
        }

        if (color_mode != 0)
        {
            final_color = mix(final_color, color.rgb, 0.35);
        }

        FragColor = vec4(final_color, color.a);
    }
    )";

    shader_program_id = glCreateProgram();
    const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, nullptr);
    glCompileShader(vertex_shader);
    glAttachShader(shader_program_id, vertex_shader);
    const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, nullptr);
    glCompileShader(fragment_shader);
    glAttachShader(shader_program_id, fragment_shader);
    glLinkProgram(shader_program_id);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    uniform_location_mvp = glGetUniformLocation(shader_program_id, "mvp");
    uniform_location_color = glGetUniformLocation(shader_program_id, "color");
    uniform_location_model = glGetUniformLocation(shader_program_id, "model");
    uniform_location_normal_matrix = glGetUniformLocation(shader_program_id, "normal_matrix");
    uniform_location_color_mode = glGetUniformLocation(shader_program_id, "color_mode");
}

void View::setup_geometry()
{
    constexpr GLfloat unit_cube_vertices[] =
    {
        // position               normal                 uv
        // Front face (-Z)
        -0.5f, -0.5f, -0.5f,      0.0f,  0.0f, -1.0f,    0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,      0.0f,  0.0f, -1.0f,    0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,      0.0f,  0.0f, -1.0f,    1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,      0.0f,  0.0f, -1.0f,    0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,      0.0f,  0.0f, -1.0f,    1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,      0.0f,  0.0f, -1.0f,    1.0f, 0.0f,

        // Right face (+X)
         0.5f, -0.5f, -0.5f,      1.0f,  0.0f,  0.0f,    0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,      1.0f,  0.0f,  0.0f,    0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,      1.0f,  0.0f,  0.0f,    1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,      1.0f,  0.0f,  0.0f,    0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,      1.0f,  0.0f,  0.0f,    1.0f, 1.0f,
         0.5f, -0.5f,  0.5f,      1.0f,  0.0f,  0.0f,    1.0f, 0.0f,

        // Back face (+Z)
        -0.5f, -0.5f,  0.5f,      0.0f,  0.0f,  1.0f,    0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,      0.0f,  0.0f,  1.0f,    1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,      0.0f,  0.0f,  1.0f,    1.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,      0.0f,  0.0f,  1.0f,    0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,      0.0f,  0.0f,  1.0f,    1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,      0.0f,  0.0f,  1.0f,    0.0f, 1.0f,

        // Left face (-X)
        -0.5f, -0.5f,  0.5f,     -1.0f,  0.0f,  0.0f,    0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,     -1.0f,  0.0f,  0.0f,    0.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,     -1.0f,  0.0f,  0.0f,    1.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,     -1.0f,  0.0f,  0.0f,    0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,     -1.0f,  0.0f,  0.0f,    1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,     -1.0f,  0.0f,  0.0f,    1.0f, 0.0f,

        // Top face (+Y)
        -0.5f,  0.5f, -0.5f,      0.0f,  1.0f,  0.0f,    0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,      0.0f,  1.0f,  0.0f,    0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,      0.0f,  1.0f,  0.0f,    1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,      0.0f,  1.0f,  0.0f,    0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,      0.0f,  1.0f,  0.0f,    1.0f, 1.0f,
         0.5f,  0.5f, -0.5f,      0.0f,  1.0f,  0.0f,    1.0f, 0.0f,

        // Bottom face (-Y)
        -0.5f, -0.5f, -0.5f,      0.0f, -1.0f,  0.0f,    0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,      0.0f, -1.0f,  0.0f,    1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,      0.0f, -1.0f,  0.0f,    1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,      0.0f, -1.0f,  0.0f,    0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,      0.0f, -1.0f,  0.0f,    1.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,      0.0f, -1.0f,  0.0f,    0.0f, 1.0f
    };

    glGenVertexArrays(1, &vertex_array_object);
    glBindVertexArray(vertex_array_object);

    glGenBuffers(1, &vertex_buffer_object);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_object);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unit_cube_vertices), unit_cube_vertices, GL_STATIC_DRAW);

    constexpr GLsizei stride = 8 * sizeof(GLfloat);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(6 * sizeof(GLfloat)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

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

void View::draw_cube(const glm::mat4 &model, const glm::vec4 &color, const ColorMode mode) // Draw one cube with a given transform and RGBA color
{
    const glm::mat4 mvp = projection * view_matrix * model;    // Build the final transform: Model→View→Projection (rightmost applied first)
    const auto normal_matrix = glm::mat3(glm::transpose(glm::inverse(model)));

    if (uniform_location_mvp >= 0) glUniformMatrix4fv(uniform_location_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
    if (uniform_location_model >= 0) glUniformMatrix4fv(uniform_location_model, 1, GL_FALSE, glm::value_ptr(model));
    if (uniform_location_normal_matrix >= 0) glUniformMatrix3fv(uniform_location_normal_matrix, 1, GL_FALSE, glm::value_ptr(normal_matrix));
    if (uniform_location_color >= 0) glUniform4f(uniform_location_color, color.r, color.g, color.b, color.a);
    if (uniform_location_color_mode >= 0) glUniform1i(uniform_location_color_mode, static_cast<int>(mode));

    glDrawArrays(GL_TRIANGLES, 0, 36);  // Draw 36 vertices (12 triangles) for one cube
}

void View::draw_cube_edges(const glm::mat4 &model, const glm::vec4 &color)
{
    const glm::mat4 mvp = projection * view_matrix * model;
    const auto normal_matrix = glm::mat3(glm::transpose(glm::inverse(model)));

    glBindVertexArray(edge_vertex_array_object);
    if (uniform_location_mvp >= 0) glUniformMatrix4fv(uniform_location_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
    if (uniform_location_model >= 0) glUniformMatrix4fv(uniform_location_model, 1, GL_FALSE, glm::value_ptr(model));
    if (uniform_location_normal_matrix >= 0) glUniformMatrix3fv(uniform_location_normal_matrix, 1, GL_FALSE, glm::value_ptr(normal_matrix));
    if (uniform_location_color >= 0) glUniform4f(uniform_location_color, color.r, color.g, color.b, color.a);
    const auto previous_mode = static_cast<GLint>(color_mode_);
    if (uniform_location_color_mode >= 0) glUniform1i(uniform_location_color_mode, static_cast<int>(ColorMode::Uniform));
    glDrawArrays(GL_LINES, 0, 12 * 2);
    if (uniform_location_color_mode >= 0) glUniform1i(uniform_location_color_mode, previous_mode);
    glBindVertexArray(vertex_array_object);
}

void View::draw_mesh(const ImportedObject &object, const glm::mat4 &model, const glm::vec4 &color, const ColorMode mode)
{
    if (object.vertex_count <= 0) return;
    const glm::mat4 scaled_model = glm::scale(model, glm::vec3(object.scale));
    const glm::mat4 mvp = projection * view_matrix * scaled_model;
    const auto normal_matrix = glm::mat3(glm::transpose(glm::inverse(scaled_model)));
    glBindVertexArray(object.vao);
    if (uniform_location_mvp >= 0) glUniformMatrix4fv(uniform_location_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
    if (uniform_location_model >= 0) glUniformMatrix4fv(uniform_location_model, 1, GL_FALSE, glm::value_ptr(scaled_model));
    if (uniform_location_normal_matrix >= 0) glUniformMatrix3fv(uniform_location_normal_matrix, 1, GL_FALSE, glm::value_ptr(normal_matrix));
    if (uniform_location_color >= 0) glUniform4f(uniform_location_color, color.r, color.g, color.b, color.a);
    if (uniform_location_color_mode >= 0) glUniform1i(uniform_location_color_mode, static_cast<int>(mode));
    glDrawArrays(GL_TRIANGLES, 0, object.vertex_count);
    glBindVertexArray(0);
}

bool View::compute_ray(const QPoint &position, glm::vec3 &origin, glm::vec3 &direction) const
{
    if (width() <= 0 || height() <= 0) return false;

    const float ndc_x = 2.0f * static_cast<float>(position.x()) / static_cast<float>(width()) - 1.0f;
    const float ndc_y = 1.0f - 2.0f * static_cast<float>(position.y()) / static_cast<float>(height());

    const glm::vec4 ray_clip(ndc_x, ndc_y, -1.0f, 1.0f);
    const glm::mat4 inverse_projection = glm::inverse(projection);
    glm::vec4 ray_eye = inverse_projection * ray_clip;
    ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0f, 0.0f);

    const glm::mat4 inverse_view = glm::inverse(build_view_matrix());
    const glm::vec4 ray_world = inverse_view * ray_eye;
    direction = glm::normalize(glm::vec3(ray_world));
    origin = cam_position;
    return true;
}

bool View::intersect_ground_plane(const QPoint &position, glm::vec3 &hit_point) const
{
    glm::vec3 origin, direction;
    if (!compute_ray(position, origin, direction)) return false;

    const float denom = direction.y;
    if (std::abs(denom) < 1e-4f) return false;

    const float t = (kGroundPlaneY - origin.y) / denom;
    if (t < 0.0f) return false;

    hit_point = origin + t * direction;
    hit_point.y = kGroundPlaneY;
    return true;
}

int View::pick_object(const QPoint &position) const
{
    glm::vec3 origin, direction;
    if (!compute_ray(position, origin, direction)) return -1;

    int best_index = -1;
    float closest_t = std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < imported_objects_.size(); ++i)
    {
        const auto &object = imported_objects_[i];
        const glm::vec3 center = object.translation;
        const float radius = object.radius * object.scale;
        const glm::vec3 oc = origin - center;

        const float a = glm::dot(direction, direction);
        const float b = 2.0f * glm::dot(oc, direction);
        const float c = glm::dot(oc, oc) - radius * radius;
        const float discriminant = b * b - 4.0f * a * c;
        if (discriminant < 0.0f) continue;

        const float sqrt_discriminant = std::sqrt(discriminant);
        float t = (-b - sqrt_discriminant) / (2.0f * a);
        if (t < 0.0f)
        {
            t = (-b + sqrt_discriminant) / (2.0f * a);
        }
        if (t < 0.0f) continue;
        if (t < closest_t)
        {
            closest_t = t;
            best_index = static_cast<int>(i);
        }
    }

    return best_index;
}
