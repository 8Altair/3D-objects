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

namespace // Anonymous namespace holding file-level constants for scene layout
{
constexpr float kGroundPlaneY = -2.0f + 0.15f; // Height of ground plane relative to world origin
constexpr float kGroundExtent = 12.0f; // Half-extent of ground plane cube
constexpr float kMinObjectScale = 0.25f; // Clamp for minimum object scale factor
constexpr float kMaxObjectScale = 8.0f; // Clamp for maximum object scale factor
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
    V = glm::translate(V, -cam_position); // Translate world opposite to camera position
    return V; // Return camera view matrix used for rendering
}

void View::emit_camera_state()
{
    emit cameraPositionChanged(cam_position.x, cam_position.y, cam_position.z); // Notify UI controls of position change
    emit cameraRotationChanged(cam_rotation_degree.x, cam_rotation_degree.y, cam_rotation_degree.z); // Notify UI controls of rotation change
}

void View::update_projection(const int w, const int h)
{
    const float aspect = h > 0 ? static_cast<float>(w)/static_cast<float>(h) : 1.0f; // Safe aspect ratio computation
    projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f); // Rebuild perspective projection to match viewport
}

void View::resizeGL(const int w, const int h)
{
    glViewport(0,0,w,h); // Update GL viewport to new widget dimensions
    update_projection(w,h); // Refresh projection matrix for updated aspect ratio
}

void View::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear frame for fresh render

    glUseProgram(shader_program_id); // Bind active shader program
    glBindVertexArray(vertex_array_object); // Bind default VAO containing cube geometry

    // Update camera matrix every frame (allows live control)
    view_matrix = build_view_matrix(); // Recompute view matrix using latest camera transform

    // Ground plane
    {
        glm::mat4 Mg(1.0f); // Initialize ground model matrix
        Mg = glm::translate(Mg, glm::vec3(0.0f, -2.0f, 0.0f));   // Slightly below origin
        Mg = glm::scale(Mg, glm::vec3(kGroundExtent, 0.30f, kGroundExtent)); // Scale ground to desired footprint
        draw_cube(Mg, glm::vec4(15.0f/255.0f, 43.0f/255.0f, 70.0f/255.0f, 1.0f),
                  ColorMode::Uniform);
        glLineWidth(2.0f); // Emphasize wireframe edges around ground
        draw_cube_edges(Mg, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)); // Render ground outline
        glLineWidth(1.0f); // Restore default line width for remainder
    }

    int object_index = 0; // Track object index for coloring/selection
    for (const auto &object : imported_objects_) // Iterate through imported meshes
    {
        const bool is_selected = object_index == selected_object_index_; // Determine selection state
        const float r = is_selected ? 0.95f : 0.6f + 0.15f * static_cast<float>(object_index % 3); // Pick stable color ramp
        const float g = is_selected ? 0.85f : 0.65f + 0.12f * static_cast<float>((object_index + 1) % 3); // Tweak green per index
        const float b = is_selected ? 0.35f : 0.75f; // Accent color used when selected
        glm::mat4 model = glm::translate(glm::mat4(1.0f), object.translation); // Build model matrix from object state
        draw_mesh(object, model, glm::vec4(r, g, b, 1.0f),
                  color_mode_);
        object_index++;
    }

    glBindVertexArray(0); // Unbind VAO to avoid accidental state leakage
    glUseProgram(0); // Unbind shader for cleanliness
}

void View::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason); // Ensure widget retains keyboard focus during interaction
    last_mouse = event->pos(); // Cache current mouse position for delta calculations

    if (event->button() == Qt::RightButton)
    {
        if (selected_object_index_ >= 0 && selected_object_index_ < static_cast<int>(imported_objects_.size()))
        {
            if (glm::vec3 hit; intersect_ground_plane(event->pos(), hit))
            {
                dragging_object_ = true; // Begin drag state when ground intersection succeeds
                drag_offset_ = imported_objects_[selected_object_index_].translation - hit; // Maintain offset so object sticks to cursor
            }
            else
            {
                dragging_object_ = false; // No ground intersection, fall back to panning
                panning = true; // Engage camera panning instead
            }
        }
        else
        {
            panning = true; // No selection => default to camera panning
        }
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        if (selected_object_index_ >= 0 && selected_object_index_ < static_cast<int>(imported_objects_.size()))
        {
            if (const int hit_index = pick_object(event->pos()); hit_index < 0)
            {
                selected_object_index_ = -1; // Clear selection when click misses current object
                focus_point_ = {0.0f, 0.0f, 0.0f}; // Reset focus to origin for camera orbit
                update(); // Refresh render to drop highlight
            }
        }
        rotating = true; // Left button initiates camera orbit
        return;
    }

    if (event->button() == Qt::MiddleButton)
    {
        scrolling_navigation_ = true; // Middle button engages Blender-style orbit
    }
}

void View::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        rotating = false; // Stop orbit navigation when left button lifts
        return;
    }

    if (event->button() == Qt::RightButton)
    {
        if (dragging_object_)
        {
            dragging_object_ = false; // Finalize object drag on release
        }
        else
        {
            panning = false; // Stop camera panning when right button lifts
        }
        return;
    }

    if (event->button() == Qt::MiddleButton)
    {
        scrolling_navigation_ = false; // Stop Blender-like orbit after middle button releases
    }
}

void View::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint distance = event->pos() - last_mouse; // Compute screen-space delta
    last_mouse = event->pos(); // Update cached mouse position

    if (dragging_object_ && selected_object_index_ >= 0 &&
        selected_object_index_ < static_cast<int>(imported_objects_.size()))
    {
        if (glm::vec3 hit; intersect_ground_plane(event->pos(), hit))
        {
            auto &object = imported_objects_[selected_object_index_]; // Access actively dragged mesh
            glm::vec3 new_translation = hit + drag_offset_; // Maintain drag offset so object follows cursor smoothly
            new_translation.y = kGroundPlaneY; // Force object back to ground plane
            object.translation = new_translation; // Apply new position
            update(); // Redraw scene to reflect move
        }
        return;
    }

    const auto dx = static_cast<float>(distance.x()); // Horizontal delta used for camera control
    const auto dy = static_cast<float>(distance.y()); // Vertical delta used for camera control

    if (scrolling_navigation_)
    {
        constexpr float orbit_speed = 0.005f;
        constexpr float min_radius = 0.25f;

        glm::vec3 offset = cam_position - focus_point_; // Vector from focus to camera
        const float height = offset.y; // Preserve camera vertical offset
        const float radius = std::max(glm::length(glm::vec2(offset.x, offset.z)), min_radius); // Clamp radius to avoid collapsing orbit

        float yaw = glm::radians(cam_rotation_degree.y); // Convert current yaw to radians
        yaw -= dx * orbit_speed; // Apply horizontal orbit delta

        offset.x = radius * std::sin(yaw); // Recompute orbit position on X axis
        offset.y = height; // Maintain existing height
        offset.z = radius * std::cos(yaw); // Recompute orbit position on Z axis

        cam_position = focus_point_ + offset; // Update camera position around focus point
        cam_rotation_degree.y = glm::degrees(yaw); // Store new yaw in degrees for UI
        emit_camera_state(); // Sync updated camera state with UI
        update(); // Redraw scene with new camera pose
        return;
    }

    if (rotating)   // Orbit camera
    {
        cam_rotation_degree.y += 0.3f * dx; // Adjust yaw from horizontal movement
        cam_rotation_degree.x += 0.3f * dy; // Adjust pitch from vertical movement
        emit_camera_state(); // Update UI spin boxes
        update(); // Redraw using updated camera angles
        return;
    }

    if (panning)    // Pan camera (XZ plane; hold Shift for vertical)
    {
        if (event->modifiers() & Qt::ShiftModifier)
        {
            cam_position.y += -0.01f * dy; // Shift modifies vertical panning speed
        }
        else
        {
            cam_position.x +=  0.01f * dx; // Translate camera along X axis
            cam_position.z +=  0.01f * dy; // Translate camera along Z axis
        }
        emit_camera_state(); // Notify UI of position change
        update(); // Redraw with new camera position
    }
}

void View::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (const int hit_index = pick_object(event->pos()); hit_index >= 0)
        {
            selected_object_index_ = hit_index; // Select object under cursor
            focus_point_ = imported_objects_[hit_index].translation; // Set camera orbit focus to selected object
            dragging_object_ = false; // Stop any drag interaction
            rotating = false; // Reset rotation flag to avoid conflict
            update(); // Redraw with selection highlight
            return;
        }
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void View::wheelEvent(QWheelEvent* event)
{
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f; // Convert wheel delta to detent steps
    if (std::abs(steps) < std::numeric_limits<float>::epsilon())
    {
        return; // Ignore zero movement to prevent unnecessary redraws
    }

    if (selected_object_index_ >= 0 && selected_object_index_ < static_cast<int>(imported_objects_.size()))
    {
        auto &object = imported_objects_[selected_object_index_]; // Target currently selected object
        const float factor = std::pow(1.1f, steps); // Exponential scale factor for smooth resizing
        object.scale = std::clamp(object.scale * factor, kMinObjectScale, kMaxObjectScale); // Clamp scale within safe bounds
        update(); // Redraw scene to reflect new scale
        return;
    }

    cam_position.z += -0.5f * steps; // Dolly camera forward/backward when nothing is selected
    emit_camera_state(); // Sync UI with updated camera position
    update(); // Redraw scene with new camera distance
}

void View::keyPressEvent(QKeyEvent *event)
{
    const float move = event->modifiers() & Qt::ShiftModifier ? 0.25f : 0.1f; // Faster motion when Shift held
    constexpr float rotate  = 2.0f; // Fixed rotational step in degrees

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
    makeCurrent(); // Ensure GL context is current before touching GPU resources
    delete_imported_objects(); // Release all imported mesh resources
    doneCurrent(); // Release GL context so Qt can manage it

    cam_position = {3.0f, 3.5f, 15.0f}; // Restore default camera position
    cam_rotation_degree = {-15.0f, 15.0f, 0.0f}; // Restore default camera orientation
    selected_object_index_ = -1; // Clear selection state
    dragging_object_ = false; // Reset drag mode
    rotating = false; // Reset orbit mode
    panning = false; // Reset pan mode
    scrolling_navigation_ = false; // Reset middle-mouse orbit mode
    focus_point_ = {0.0f, 0.0f, 0.0f}; // Return focus point to origin
    color_mode_ = ColorMode::Uniform; // Return to default color mode
    update_projection(width(), height()); // Recompute projection in case viewport changed
    emit_camera_state(); // Notify UI of restored camera state
    update(); // Redraw scene with clean slate
}

bool View::load_object(const QString &file_path)
{
    Assimp::Importer importer; // Helper object used to parse mesh assets
    constexpr unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_ImproveCacheLocality; // Preprocess mesh for rendering efficiency

    const aiScene *scene = importer.ReadFile(file_path.toStdString(), flags); // Load OBJ scene from disk
    if (!scene || !scene->HasMeshes())
    {
        qWarning() << "Assimp failed to load OBJ:" << QString::fromStdString(importer.GetErrorString());
        return false;
    }

    const aiMesh *mesh = scene->mMeshes[0]; // Use first mesh; extend here for multi-mesh support
    if (!mesh || !mesh->HasPositions())
    {
        qWarning() << "OBJ mesh has no positions.";
        return false;
    }

    struct VertexData // Staging struct to simplify interleaved buffer creation
    {
        glm::vec3 position{};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec2 uv{0.0f, 0.0f};
    };

    std::vector<VertexData> vertices; // Temporary vertex list used to build VBO
    vertices.reserve(mesh->mNumFaces * 3); // Reserve to avoid reallocations

    float min_x = std::numeric_limits<float>::max(); // Bounding box accumulator (min X)
    float min_y = std::numeric_limits<float>::max(); // Bounding box accumulator (min Y)
    float min_z = std::numeric_limits<float>::max(); // Bounding box accumulator (min Z)
    float max_x = std::numeric_limits<float>::lowest(); // Bounding box accumulator (max X)
    float max_y = std::numeric_limits<float>::lowest(); // Bounding box accumulator (max Y)
    float max_z = std::numeric_limits<float>::lowest(); // Bounding box accumulator (max Z)
    float max_radius_sq = 0.0f; // Track the largest squared radius for pick sphere
    const bool has_normals = mesh->HasNormals(); // Determine if imported mesh provides normals
    const bool has_uvs = mesh->HasTextureCoords(0); // Determine if imported mesh provides UVs

    // Iterate faces to flatten mesh into triangle list suitable for GL_TRIANGLES
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

    if (vertices.empty()) // Abort when no triangle data was produced
    {
        qWarning() << "OBJ contains no triangles.";
        return false;
    }

    const float center_x = 0.5f * (min_x + max_x); // Compute horizontal center to recenter mesh
    const float center_z = 0.5f * (min_z + max_z); // Compute depth center to recenter mesh
    for (auto &vertex : vertices) // Normalize vertices so base sits on ground and center is at origin
    {
        vertex.position.x -= center_x;
        vertex.position.y -= min_y;
        vertex.position.z -= center_z;
        max_radius_sq = std::max(max_radius_sq, glm::dot(vertex.position, vertex.position));
    }

    ImportedObject object; // Prepare GPU resource descriptors for new mesh
    object.vertex_count = static_cast<GLsizei>(vertices.size()); // Store triangle vertex count
    object.base_footprint = std::max({1.0f, max_x - min_x, max_z - min_z}) + 0.5f; // Footprint guides placement spacing
    object.radius = std::sqrt(max_radius_sq); // Use radius for click picking

    std::vector<float> interleaved; // Flatten struct data into float stream
    interleaved.reserve(vertices.size() * 8); // 3 position + 3 normal + 2 UV
    for (const auto & [position, normal, uv] : vertices) // Copy attributes in interleaved order
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

    makeCurrent(); // Ensure OpenGL context is active before allocating buffers

    glGenVertexArrays(1, &object.vao); // Create VAO to store vertex format state
    glBindVertexArray(object.vao); // Bind VAO for configuration

    glGenBuffers(1, &object.vbo); // Create VBO storing vertex data
    glBindBuffer(GL_ARRAY_BUFFER, object.vbo); // Bind VBO for upload
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)), interleaved.data(), GL_STATIC_DRAW); // Upload vertex data

    constexpr GLsizei stride = 8 * sizeof(GLfloat);
    glEnableVertexAttribArray(0); // Enable position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr); // Describe position layout
    glEnableVertexAttribArray(1); // Enable normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(3 * sizeof(GLfloat))); // Describe normal layout
    glEnableVertexAttribArray(2); // Enable UV attribute
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(6 * sizeof(GLfloat))); // Describe UV layout

    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind VBO now that VAO stores state
    glBindVertexArray(0); // Unbind VAO to avoid accidental changes

    glm::vec3 desired_translation{0.0f, kGroundPlaneY, 0.0f}; // Start placement on ground at origin

    const auto overlaps = [this, &object](const glm::vec3 &position) // Helper to test placement overlap
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

    object.translation = desired_translation; // Finalize placement position
    imported_objects_.push_back(object); // Store configured object in scene list

    doneCurrent(); // Release GL context after allocation
    update(); // Request redraw to show new object
    return true;
}

void View::set_color_mode(const ColorMode mode)
{
    if (color_mode_ == mode) return; // Skip redundant updates
    color_mode_ = mode; // Store new color interpretation mode
    update(); // Trigger repaint to reflect change
}

void View::delete_imported_objects()
{
    for (auto &object : imported_objects_) // Iterate through loaded objects releasing GPU memory
    {
        if (object.vbo)
        {
            glDeleteBuffers(1, &object.vbo); // Destroy vertex buffer
            object.vbo = 0; // Reset handle to avoid double delete
        }
        if (object.vao)
        {
            glDeleteVertexArrays(1, &object.vao); // Destroy vertex array object
            object.vao = 0; // Reset handle to avoid double delete
        }
    }
    imported_objects_.clear(); // Remove all metadata records
    selected_object_index_ = -1; // Clear selection state because objects are gone
    dragging_object_ = false; // Ensure drag state is cleared
}

void View::setup_shaders()
{
    // Vertex shader generating varyings for fragment stage
    static auto vertex_shader_source = R"(#version 450 core
    // Vertex attributes supplied by VAO (position, normal, uv)
    layout(location = 0) in vec3 position;
    layout(location = 1) in vec3 normal;
    layout(location = 2) in vec2 texcoord;

    // Uniform transforms pushed from CPU side
    uniform mat4 model;
    uniform mat4 mvp;
    uniform mat3 normal_matrix;

    // Varyings forwarded to fragment shader
    out vec3 vWorldPosition;
    out vec3 vNormal;
    out vec2 vTexCoord;

    void main()
    {
        vec4 world_position = model * vec4(position, 1.0); // Transform vertex into world space
        vWorldPosition = world_position.xyz; // Preserve world-space position for color encoding
        vNormal = normalize(normal_matrix * normal); // Transform normal to world space
        vTexCoord = texcoord; // Pass UV straight through
        gl_Position = mvp * vec4(position, 1.0); // Project into clip space
    }
    )";

    // Fragment shader selecting color source
    static auto fragment_shader_source = R"(#version 450 core
    layout(location = 0) out vec4 FragColor;

    in vec3 vWorldPosition;
    in vec3 vNormal;
    in vec2 vTexCoord;

    // Uniforms set per draw call
    uniform vec4 color;
    uniform int color_mode;

    // Encode normalized world position into RGB for visualization
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

    // Encode normalized world-space normal into RGB (useful to inspect shading data)
    vec3 encode_normal()
    {
        float length_value = length(vNormal);
        vec3 normalized = length_value > 1e-5 ? normalize(vNormal) : vec3(0.0, 1.0, 0.0);
        return 0.5 + 0.5 * normalized;
    }

    // Encode UV coordinates into RG channels (reveals UV layout / seams)
    vec3 encode_uv()
    {
        vec2 wrapped = fract(vTexCoord);
        return vec3(wrapped, 0.5);
    }

    void main()
    {
        vec3 final_color = color.rgb; // Default color uses provided material tint

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
            final_color = mix(final_color, color.rgb, 0.35); // Blend attribute visualization with base tint
        }

        FragColor = vec4(final_color, color.a); // Output RGBA color for framebuffer
    }
    )";

    shader_program_id = glCreateProgram(); // Allocate shader program container
    const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER); // Create vertex shader object
    glShaderSource(vertex_shader, 1, &vertex_shader_source, nullptr); // Upload vertex shader source
    glCompileShader(vertex_shader); // Compile vertex shader
    glAttachShader(shader_program_id, vertex_shader); // Attach vertex shader to program
    const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER); // Create fragment shader object
    glShaderSource(fragment_shader, 1, &fragment_shader_source, nullptr); // Upload fragment shader source
    glCompileShader(fragment_shader); // Compile fragment shader
    glAttachShader(shader_program_id, fragment_shader); // Attach fragment shader to program
    glLinkProgram(shader_program_id); // Link shaders into executable program
    glDeleteShader(vertex_shader); // Free compiled vertex shader (program retains copy)
    glDeleteShader(fragment_shader); // Free compiled fragment shader

    uniform_location_mvp = glGetUniformLocation(shader_program_id, "mvp"); // Cache MVP uniform handle
    uniform_location_color = glGetUniformLocation(shader_program_id, "color"); // Cache color uniform handle
    uniform_location_model = glGetUniformLocation(shader_program_id, "model"); // Cache model matrix uniform handle
    uniform_location_normal_matrix = glGetUniformLocation(shader_program_id, "normal_matrix"); // Cache normal matrix uniform handle
    uniform_location_color_mode = glGetUniformLocation(shader_program_id, "color_mode"); // Cache color mode uniform handle
}

void View::setup_geometry()
{
    constexpr GLfloat unit_cube_vertices[] = // Interleaved position/normal/UV data for unit cube
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

    glGenVertexArrays(1, &vertex_array_object); // Create VAO for cube geometry
    glBindVertexArray(vertex_array_object); // Bind VAO to capture vertex format state

    glGenBuffers(1, &vertex_buffer_object); // Create VBO for cube vertices
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_object); // Bind cube VBO for data upload
    glBufferData(GL_ARRAY_BUFFER, sizeof(unit_cube_vertices), unit_cube_vertices, GL_STATIC_DRAW); // Upload cube vertex data once

    constexpr GLsizei stride = 8 * sizeof(GLfloat); // 3 position + 3 normal + 2 UV
    glEnableVertexAttribArray(0); // Enable position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr); // Describe position attribute layout
    glEnableVertexAttribArray(1); // Enable normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(3 * sizeof(GLfloat))); // Describe normal attribute layout
    glEnableVertexAttribArray(2); // Enable UV attribute
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(6 * sizeof(GLfloat))); // Describe UV attribute layout

    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind VBO now that VAO stores format
    glBindVertexArray(0); // Unbind VAO to avoid unintended modifications

    constexpr GLfloat cube_edge_vertices[12 * 2 * 3] = // Line segment endpoints outlining cube edges
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

    glGenVertexArrays(1, &edge_vertex_array_object); // Create VAO for edge lines
    glBindVertexArray(edge_vertex_array_object); // Bind edge VAO

    glGenBuffers(1, &edge_vertex_buffer_object); // Create VBO for edge vertices
    glBindBuffer(GL_ARRAY_BUFFER, edge_vertex_buffer_object); // Bind edge VBO for upload
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube_edge_vertices), cube_edge_vertices, GL_STATIC_DRAW); // Upload line segment data

    glEnableVertexAttribArray(0); // Enable position attribute for lines
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), nullptr); // Describe line vertex layout

    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind edge VBO
    glBindVertexArray(0); // Unbind edge VAO
}

void View::draw_cube(const glm::mat4 &model, const glm::vec4 &color, const ColorMode mode) // Draw one cube with a given transform and RGBA color
{
    const glm::mat4 mvp = projection * view_matrix * model;    // Build the final transform: Model→View→Projection (rightmost applied first)
    const auto normal_matrix = glm::mat3(glm::transpose(glm::inverse(model))); // Compute normal matrix for correct lighting

    if (uniform_location_mvp >= 0) glUniformMatrix4fv(uniform_location_mvp, 1, GL_FALSE, glm::value_ptr(mvp)); // Upload MVP transform
    if (uniform_location_model >= 0) glUniformMatrix4fv(uniform_location_model, 1, GL_FALSE, glm::value_ptr(model)); // Upload model transform
    if (uniform_location_normal_matrix >= 0) glUniformMatrix3fv(uniform_location_normal_matrix, 1, GL_FALSE, glm::value_ptr(normal_matrix)); // Upload normal matrix
    if (uniform_location_color >= 0) glUniform4f(uniform_location_color, color.r, color.g, color.b, color.a); // Upload base material color
    if (uniform_location_color_mode >= 0) glUniform1i(uniform_location_color_mode, static_cast<int>(mode)); // Upload color mode selection

    glDrawArrays(GL_TRIANGLES, 0, 36);  // Draw 36 vertices (12 triangles) for one cube
}

void View::draw_cube_edges(const glm::mat4 &model, const glm::vec4 &color)
{
    const glm::mat4 mvp = projection * view_matrix * model; // Compute edge MVP transform
    const auto normal_matrix = glm::mat3(glm::transpose(glm::inverse(model))); // Reuse model normal matrix for completeness

    glBindVertexArray(edge_vertex_array_object); // Bind edge VAO for drawing
    if (uniform_location_mvp >= 0) glUniformMatrix4fv(uniform_location_mvp, 1, GL_FALSE, glm::value_ptr(mvp)); // Upload MVP transform
    if (uniform_location_model >= 0) glUniformMatrix4fv(uniform_location_model, 1, GL_FALSE, glm::value_ptr(model)); // Upload model transform
    if (uniform_location_normal_matrix >= 0) glUniformMatrix3fv(uniform_location_normal_matrix, 1, GL_FALSE, glm::value_ptr(normal_matrix)); // Upload normal matrix
    if (uniform_location_color >= 0) glUniform4f(uniform_location_color, color.r, color.g, color.b, color.a); // Upload wireframe color
    const auto previous_mode = static_cast<GLint>(color_mode_); // Preserve current color mode
    if (uniform_location_color_mode >= 0) glUniform1i(uniform_location_color_mode, static_cast<int>(ColorMode::Uniform)); // Force uniform color for edges
    glDrawArrays(GL_LINES, 0, 12 * 2); // Render 12 line segments (24 vertices)
    if (uniform_location_color_mode >= 0) glUniform1i(uniform_location_color_mode, previous_mode); // Restore previous color mode
    glBindVertexArray(vertex_array_object); // Rebind default VAO for subsequent draws
}

void View::draw_mesh(const ImportedObject &object, const glm::mat4 &model, const glm::vec4 &color, const ColorMode mode)
{
    if (object.vertex_count <= 0) return; // Skip empty meshes
    const glm::mat4 scaled_model = glm::scale(model, glm::vec3(object.scale)); // Incorporate object scale into model matrix
    const glm::mat4 mvp = projection * view_matrix * scaled_model; // Compose MVP for object instance
    const auto normal_matrix = glm::mat3(glm::transpose(glm::inverse(scaled_model))); // Normal matrix after applying scale
    glBindVertexArray(object.vao); // Bind object's VAO to draw its geometry
    if (uniform_location_mvp >= 0) glUniformMatrix4fv(uniform_location_mvp, 1, GL_FALSE, glm::value_ptr(mvp)); // Upload MVP transform
    if (uniform_location_model >= 0) glUniformMatrix4fv(uniform_location_model, 1, GL_FALSE, glm::value_ptr(scaled_model)); // Upload model transform
    if (uniform_location_normal_matrix >= 0) glUniformMatrix3fv(uniform_location_normal_matrix, 1, GL_FALSE, glm::value_ptr(normal_matrix)); // Upload normal matrix
    if (uniform_location_color >= 0) glUniform4f(uniform_location_color, color.r, color.g, color.b, color.a); // Upload base color
    if (uniform_location_color_mode >= 0) glUniform1i(uniform_location_color_mode, static_cast<int>(mode)); // Upload color mode
    glDrawArrays(GL_TRIANGLES, 0, object.vertex_count); // Issue draw call for mesh
    glBindVertexArray(0); // Unbind VAO to avoid leaking state
}

bool View::compute_ray(const QPoint &position, glm::vec3 &origin, glm::vec3 &direction) const
{
    if (width() <= 0 || height() <= 0) return false; // Guard against invalid viewport size

    const float ndc_x = 2.0f * static_cast<float>(position.x()) / static_cast<float>(width()) - 1.0f; // Convert pixel X to NDC
    const float ndc_y = 1.0f - 2.0f * static_cast<float>(position.y()) / static_cast<float>(height()); // Convert pixel Y to NDC

    const glm::vec4 ray_clip(ndc_x, ndc_y, -1.0f, 1.0f); // Ray in clip space (pointing forward)
    const glm::mat4 inverse_projection = glm::inverse(projection); // Invert projection to go back to view space
    glm::vec4 ray_eye = inverse_projection * ray_clip; // Transform ray into eye space
    ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0f, 0.0f); // Set proper direction in eye space

    const glm::mat4 inverse_view = glm::inverse(build_view_matrix()); // Invert view matrix to world space
    const glm::vec4 ray_world = inverse_view * ray_eye; // Transform ray into world coordinates
    direction = glm::normalize(glm::vec3(ray_world)); // Normalize to get direction vector
    origin = cam_position; // Ray origin is camera position
    return true; // Ray successfully computed
}

bool View::intersect_ground_plane(const QPoint &position, glm::vec3 &hit_point) const
{
    glm::vec3 origin, direction; // Ray origin/direction in world space
    if (!compute_ray(position, origin, direction)) return false; // Early exit if ray cannot be computed

    const float denominator = direction.y; // Dot product with ground normal (0,1,0)
    if (std::abs(denominator) < 1e-4f) return false; // Reject near-parallel rays to avoid instability

    const float t = (kGroundPlaneY - origin.y) / denominator; // Solve parametric plane intersection
    if (t < 0.0f) return false; // Ignore intersections behind camera

    hit_point = origin + t * direction; // Compute intersection point
    hit_point.y = kGroundPlaneY; // Snap to exact plane height
    return true; // Intersection succeeded
}

int View::pick_object(const QPoint &position) const
{
    glm::vec3 origin, direction; // World-space picking ray
    if (!compute_ray(position, origin, direction)) return -1; // Abort if ray cannot be constructed

    int best_index = -1; // Track the closest hit object index
    float closest_t = std::numeric_limits<float>::max(); // Track nearest intersection distance

    for (std::size_t i(0); i < imported_objects_.size(); i++) // Iterate through scene objects
    {
        const auto &object = imported_objects_[i]; // Reference current object
        const glm::vec3 center = object.translation; // Sphere center at object position
        const float radius = object.radius * object.scale; // Sphere radius scaled with object
        const glm::vec3 origin_center = origin - center; // Vector from sphere center to ray origin

        const float a = glm::dot(direction, direction); // Quadratic coefficient a
        const float b = 2.0f * glm::dot(origin_center, direction); // Quadratic coefficient b
        const float c = glm::dot(origin_center, origin_center) - radius * radius; // Quadratic coefficient c
        const float discriminant = b * b - 4.0f * a * c; // Discriminant for ray-sphere intersection
        if (discriminant < 0.0f) continue; // Skip when ray misses sphere

        const float sqrt_discriminant = std::sqrt(discriminant); // Precompute sqrt for roots
        float t = (-b - sqrt_discriminant) / (2.0f * a); // First intersection root
        if (t < 0.0f)
        {
            t = (-b + sqrt_discriminant) / (2.0f * a); // Use second root if first behind ray origin
        }
        if (t < 0.0f) continue; // Ignore intersections behind camera
        if (t < closest_t)
        {
            closest_t = t; // Update nearest hit distance
            best_index = static_cast<int>(i); // Store index of best hit
        }
    }

    return best_index; // Return index of closest intersected object (-1 if none)
}
