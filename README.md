# engine2

**engine2 v0.2** — собственное ядро 3D-движка на **C++20** с OpenGL 3.3 Core. Это расширяемая база для фотореалистичного рендеринга, **пока не готовый игровой движок уровня Unreal Engine**.

## Возможности

- Собственный цикл приложения, камера от первого лица, обработка ввода.
- Процедурные сферы и плоскость, VAO/VBO/EBO, матрицы трансформаций.
- **glTF 2.0 (.gltf и .glb)**: треугольные меши, индексы, UV0, нормали, тангенты и их генерация, иерархия узлов, несколько экземпляров меша.
- **Текстурные PBR-материалы** glTF: base color (sRGB), metallic/roughness (каналы B/G), normal map, occlusion, emissive. Поддерживаются фильтры и wrap-режимы sampler; mipmaps.
- Cook–Torrance **GGX PBR**, directional shadow map 2048×2048 с 3×3 PCF, double-sided surfaces.
- Приближённое ambient-освещение, ACES filmic tonemapping и гамма-коррекция.
- Сцена с 25 PBR-сферами по умолчанию, загрузка внешней модели через CLI.
- CMake, GLFW, GLM, glad, tinygltf и автоматические CTest-smoke tests в Linux CI.

Сторонние библиотеки используются для окна, математики, OpenGL-загрузчика и **парсинга файла glTF**. GPU-ресурсы, геометрическая подготовка, материалы, шейдеры и рендерер реализованы в самом engine2.

## Сборка

Требования: CMake 3.24+, компилятор C++20, Python 3, Git, драйвер **OpenGL 3.3 Core**, интернет при первой конфигурации CMake. На Ubuntu установите `libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/engine2
```

Для Windows с Visual Studio: `cmake -S . -B build`, затем `cmake --build build --config Release`. На macOS нужен forward-compatible контекст OpenGL 3.3 Core.

## Загрузка glTF/GLB

```bash
./build/engine2 --model /absolute/path/to/model.glb
./build/engine2 --model tests/data/textured-triangle.gltf --frames 3
ctest --test-dir build --output-on-failure
```

Аргумент `--model` заменяет демонстрационную сцену импортированной. Модель должна содержать хотя бы один непрозрачный треугольный примитив. Переместите камеру к объекту при необходимости; автоматическое кадрирование ещё не реализовано.

Шейдеры копируются в `shaders` рядом с исполняемым файлом. При перемещении программы перенесите также каталог шейдеров.

## Управление

| Клавиша | Действие |
| --- | --- |
| W / A / S / D | Перемещение |
| Space / Left Ctrl | Вверх / вниз |
| Мышь | Осмотр |
| Shift | Ускорение |
| Tab | Освободить / захватить курсор |
| Колесо мыши | Угол обзора |
| Esc | Выход |

## Ограничения

Пока поддерживаются **непрозрачные glTF 2.0 TRIANGLES** с UV0 и обычными accessor-данными. Скининг, морфинг, анимации, разреженные accessors, UV1, alpha mask/blend, KHR-расширения и компрессия мешей не реализованы. Модели с прозрачными материалами пропускаются. В сцене пока один направленный источник света и приближённый заполняющий свет без HDR/IBL. Для реального фотореализма нужны HDR-окружения, GI, физически корректная экспозиция, продвинутая постобработка и оптимизация. См. [план развития](docs/ROADMAP.md).

```text
include/engine2/   C++ API: Camera, Mesh, Material, Texture, GltfLoader, Renderer
src/               Реализация и демо-приложение
shaders/           GLSL 330: GGX PBR + shadow pass
tests/data/        Малый glTF 2.0 asset со встроенными текстурами
docs/ROADMAP.md    Этапы дальнейшей разработки
.github/workflows/ Сборка и CTest в CI
```
