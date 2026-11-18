# engine2

**engine2 v0.4** — экспериментальный 3D-рендерер на **C++20 / OpenGL 3.3 Core**. Есть glTF 2.0, текстурные PBR-материалы, HDR Image-Based Lighting, несколько точечных источников света, SSAO и отсечение невидимых объектов. Это пока **не полноценный игровой движок и не готовый фотореализм**.

## Возможности

- GLFW окно, цикл приложения, FPS-камера, управление мышью и клавиатурой.
- Индексированные меши, процедурная геометрия, трансформации, локальные AABB.
- Импорт **glTF 2.0 (.gltf/.glb)**: меши, UV0, иерархия узлов, инстансы, генерация нормалей и тангентов.
- Текстурные PBR-материалы: base color (sRGB), metallic/roughness, normal, AO, emissive.
- **Cook–Torrance GGX**: направленный свет с PCF shadow map 2048×2048, **до 8 движущихся точечных источников света** с затуханием по квадрату расстояния и плавным ограничением радиуса.
- **HDR IBL**: панорама Radiance `.hdr` или процедурный небосвод, diffuse irradiance, GGX specular prefilter, split-sum BRDF LUT.
- **SSAO**: полурезолюционный геометрический проход (позиция/нормаль в координатах камеры), 16 hemisphere samples и edge-aware blur; влияет на непрямое освещение, а не на прямой свет.
- **Frustum culling**: шесть плоскостей камеры проверяются против трансформированных AABB; shadow casters проверяются отдельно во фрустуме солнца, чтобы не терять тени от объектов вне кадра.
- HDR framebuffer RGBA16F, bloom, ACES tonemapping, экспозиция.
- PPM-screenshot, диагностические счётчики draw calls, CTest с базовыми проверками пикселей и математики.

Внешние зависимости: GLFW, glad, GLM, tinygltf/stb. Код GPU-проходов, фильтров, PBR-шейдеров, отбраковки и приложения находится в `engine2`.

## Сборка

Требуются: CMake 3.24+, C++20, Python 3, Git, OpenGL 3.3 Core, интернет для первой настройки CMake. На Ubuntu потребуются `libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev xvfb`.

```bash
git clone https://github.com/verge88/engine2.git
cd engine2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/engine2
```

На Windows: `cmake -S . -B build`, `cmake --build build --config Release`, запуск `build/Release/engine2.exe`; на macOS требуется OpenGL 3.3 Core. Каталог `shaders` копируется рядом с бинарником.

## Параметры

```bash
# Включённый по умолчанию SSAO и 3 движущихся локальных источника
./build/engine2

# Внешние модель и окружение, 6 цветных огней
./build/engine2 --model assets/scene.glb --environment assets/studio.hdr --lights 6

# Отключить затенение окружения или все точечные источники
./build/engine2 --ssao 0 --lights 0

# Отключить frustum culling для отладки геометрии
./build/engine2 --culling 0

# Экспозиция, bloom, статистика и сохранение PPM
./build/engine2 --exposure 0.75 --bloom 0.25 --frames 2 --stats --screenshot frame.ppm

# Автоматические тесты
ctest --test-dir build --output-on-failure
```

Допустимые диапазоны: `--lights` 0..8 (целое), `--ssao` и `--culling` 0|1, `--exposure` 0.05..16, `--bloom` 0..2.

| Клавиша | Действие |
| --- | --- |
| W/A/S/D, мышь | Перемещение и осмотр |
| Space / Left Ctrl, Shift | Высота, ускорение |
| Колесо, Tab, Esc | FOV, освободить/захватить курсор, выход |
| [ / ] | Экспозиция −/+ |
| B | Вкл/выкл bloom |
| O | Вкл/выкл SSAO |

## Ограничения версии 0.4

- **Точечные источники пока не отбрасывают тени**: directional shadow map есть только для солнца. Точечные источники освещают поверхности по GGX без shadow cubemap.
- SSAO не учитывает normal maps и является экранным приближением, не глобальным освещением; возможно появление артефактов на границах.
- Расчёт IBL основан на статическом HDR-окружении. Нет трассировки лучей, TAA, SSR, настоящего GI, cascaded shadows, авторегулирования экспозиции.
- Импорт glTF ограничен непрозрачными TRIANGLES и UV0 без анимаций, скининга, alpha modes, sparse accessors и extensions.
- Frustum culling CPU не использует иерархические деревья/батчинг: одна проверка на экземпляр. Статистика считает только переданные в API `RenderItem` и draw calls из shadow/PBR-проходов.
- Производительность различается по драйверам. Тесты запускаются в Linux через программный OpenGL, визуальное сравнение пока не эталонное.

Следующие шаги: [дорожная карта](docs/ROADMAP.md).

```text
include/engine2/    API камеры, мешей, AABB/Frustum, материалов и рендер-проходов
src/                C++20: импорт, PBR, SSAO, IBL, постобработка
shaders/            GLSL 330: GGX, HDR, SSAO, skybox, shadows, bloom
tests/              glTF/HDR fixtures, frustum unit tests, проверки изображений
docs/ROADMAP.md     План развития
.github/workflows/  CI build + ctest
```
