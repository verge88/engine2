# engine2

**engine2 v0.3** — собственное ядро 3D-движка на **C++20 / OpenGL 3.3 Core** с текстурными PBR-материалами, **HDR Image-Based Lighting** и постобработкой. Это фундамент для графического движка, не полноценный фотореалистичный Unreal Engine.

## Реализовано

- GLFW окно, собственный цикл приложения, FPS-камера и управление.
- VAO/VBO/EBO, процедурная геометрия, трансформации.
- **glTF 2.0 (.gltf и .glb)**: треугольники, меши, UV0, иерархия узлов, нормали, тангенты, генерация отсутствующих нормалей/тангентов.
- **PBR-текстуры** glTF: base color (sRGB), metallic/roughness (линейные B/G), normal map, occlusion, emissive.
- **Cook–Torrance / GGX** и 2048×2048 directional shadow map с PCF.
- **HDR environment IBL:** загрузка панорам Radiance `.hdr` или процедурный HDR-небосвод; precomputed diffuse irradiance cubemap, GGX prefiltered specular cubemap (5 mips), split-sum BRDF LUT. HDR skybox.
- **HDR-кадр** GL_RGBA16F, bright-pass / separable Gaussian bloom на половине разрешения, настраиваемая экспозиция, ACES filmic tonemapping.
- Захват кадра в PPM, автоматизированные smoke tests и проверка, что кадр непустой.

Библиотеки: GLFW (окно), glad (OpenGL), GLM (математика), tinygltf + stb (импорт glTF и HDR). Рендерер, подготовка карт IBL, GLSL и GPU-ресурсы реализованы непосредственно в engine2.

## Сборка

Требования: CMake 3.24+, C++20 компилятор, Python 3, Git, OpenGL 3.3 Core драйвер, интернет при первом запуске CMake. На Ubuntu установите `libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev xvfb`.

```bash
git clone https://github.com/verge88/engine2.git
cd engine2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/engine2
```

Windows (Visual Studio): `cmake -S . -B build`, `cmake --build build --config Release` и `build/Release/engine2.exe`. macOS: OpenGL 3.3 forward-compatible context. GLSL-файлы копируются в каталог `shaders` рядом с бинарником.

## Примеры

```bash
# Демонстрационная сетка из 25 PBR-материалов + процедурное HDR-небо
./build/engine2

# Собственная модель и HDR-окружение (equirectangular Radiance RGBE .hdr)
./build/engine2 --model /path/to/model.glb --environment /path/to/studio.hdr

# Экспозиция и интенсивность bloom
./build/engine2 --exposure 0.7 --bloom 0.3

# Рендер 2 кадров и снимок последнего в PPM (P6), для диагностики
./build/engine2 --frames 2 --screenshot frame.ppm

# Smoke-тесты, включая встроенную тестовую модель/HDR и содержимое кадра
ctest --test-dir build --output-on-failure
```

### Управление

| Клавиша | Действие |
| --- | --- |
| W/A/S/D, мышь | Перемещение и осмотр |
| Space / Left Ctrl | Вверх / вниз |
| Shift | Ускорение |
| Tab | Захват / освобождение мыши |
| Колесо | Угол обзора |
| [ / ] | Уменьшить / увеличить экспозицию |
| B | Включить / отключить bloom |
| Esc | Выход |

## Ограничения

Фотореализм не гарантируется: **нет глобального освещения (GI), ray tracing, динамической экспозиции, TAA, SSR, SSAO, физики или редактора**. IBL — приближение split-sum для статического HDR-окружения, не real-time отражения объектов. В glTF пока работают непрозрачные TRIANGLES и UV0 без анимаций, скининга, sparse accessors, alpha blend/mask, KHR расширений. Тени от одного направленного источника, ограниченная область shadow map; bloom — простой Gaussian, не многомасштабный.

HDR-файлы должны быть Radiance equirectangular `.hdr` (не OpenEXR); внешние файлы могут требовать настройки камеры и экспозиции. При каждом запуске окружение предобрабатывается на GPU; кеш в файлы не реализован.

См. [дорожную карту](docs/ROADMAP.md).

```text
include/engine2/   Camera, Mesh, Material, GltfLoader, Environment, PostProcess, Renderer
src/               C++20 GPU implementation and demo application
shaders/           GLSL 330 for GGX / IBL / skybox / shadows / bloom / composite
tests/             Embedded glTF + Radiance .hdr and frame-content validation
docs/ROADMAP.md    Development milestones
.github/workflows/ CI build and ctest
```
