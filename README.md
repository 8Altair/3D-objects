# 3D Objects

## Overview
**3D Objects** is a **Qt 6** desktop application that integrates **OpenGL 4.6 Core Profile** into a Qt Widgets interface.  
It renders a 3D scene consisting of a flat ground plane and any number of user‑imported 3D models.  
Models are loaded at runtime from **OBJ files** using the **Assimp** library, so you can drop arbitrary meshes into the scene without recompiling.  
Each imported mesh gets its own VAO/VBO and can be individually selected, transformed, and recolored.

---

## Features

- Modern **OpenGL 4.6 Core Profile** (no deprecated functions)
- Dynamic loading of OBJ files using **Assimp**
- Flat **ground plane** as a base for all models
- **Camera controls**:
  - Orbit, pan, and dolly with mouse and keyboard
- **Per-object manipulation**:
  - Select, translate (drag), and scale individual objects
- **Independent VAO/VBO per object**
- **Coloring modes** based on vertex attributes:
  - Uniform color
  - Position (world space)
  - Normal direction
  - UV coordinates
- **GLSL 4.50 shaders** using in/out varyings and uniforms
- Built using **CMake**, **GLM**, **Qt 6**, and **Assimp**

---

## Prerequisites

| Dependency       | Minimum Version | Purpose                                    |
|------------------|------------------|--------------------------------------------|
| **Qt 6**         | 6.5+             | Core / GUI / Widgets / OpenGL              |
| **CMake**        | 3.21+            | Build system                               |
| **GLM**          | Latest           | Matrix and vector math                     |
| **Assimp**       | 5.3+             | Import OBJ models                          |
| **C++ Compiler** | C++20/C++23      | MSVC, MinGW, Clang, or GCC                 |

---

## How It Works

### Object Loading

- Press **Object** to import an OBJ file.
- Geometry is loaded via Assimp and centered above the ground.
- Each object gets its own VAO/VBO and is rendered independently.

### Ground

- Rendered by scaling a unit cube.
- Positioned at `y = -2.0` and sized to cover a large flat area.

### Object Transformations

- Select an object by **double-clicking**.
- Drag to **move** it along the ground.
- Use the **mouse wheel** to **scale**.
- Only one object can be selected at a time.

### Coloring Modes

- Use the dropdown to choose:
  - Uniform
  - Position
  - Normal
  - UV
  - Position + Normal
- Colors are blended with a base tint per object.

---

## Controls

### Mouse

| Action                  | Effect                                    |
|-------------------------|-------------------------------------------|
| Left-drag               | Orbit scene (or deselect object)          |
| Middle-drag            | Orbit scene                                |
| Right-drag             | Pan camera or drag selected object         |
| Shift + Right-drag      | Move vertically                            |
| Mouse wheel             | Dolly or scale selected object             |
| Double-click            | Select object                              |

### Keyboard

| Keys         | Action                       |
|--------------|------------------------------|
| W / S        | Move camera forward/back     |
| A / D        | Move camera left/right       |
| R / F        | Move camera up/down          |
| I / K        | Pitch camera up/down         |
| J / L        | Yaw camera left/right        |
| U / O        | Roll camera                  |
| Backspace    | Delete selected object       |

---

## Project Structure

```
3D-objects/
├─ CMakeLists.txt
├─ main.cpp
├─ main_window.(h|cpp|ui)
├─ view_3D.(h|cpp)
├─ shaders/
└─ resources.qrc
```

---

## Shader Pipeline (GLSL 4.50)

### Vertex Shader

```glsl
#version 450 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;
uniform mat4 mvp;
uniform mat4 model;
uniform mat3 normal_matrix;
out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    vWorldPosition = vec3(model * vec4(position, 1.0));
    vNormal = normalize(normal_matrix * normal);
    vTexCoord = texcoord;
}
```

### Fragment Shader

```glsl
#version 450 core
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
uniform int color_mode;
uniform vec4 color;
out vec4 FragColor;

void main() {
    vec4 final_color = color;

    if (color_mode == 1) {
        final_color.rgb = normalize(vWorldPosition) * 0.5 + 0.5;
    } else if (color_mode == 2) {
        final_color.rgb = normalize(vNormal) * 0.5 + 0.5;
    } else if (color_mode == 3) {
        final_color.rgb = vec3(fract(vTexCoord), 0.5);
    }

    FragColor = mix(final_color, color, 0.35);
}
```

## Notes

- A small height offset avoids z-fighting between objects and the ground.
- Right-click on empty space to deselect.
