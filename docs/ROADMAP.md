# engine2 — план развития

Наша цель — расширяемый собственный C++ 3D-движок. Каждая версия добавляет конкретные тестируемые возможности; PBR/HDR/SSAO не делают картинку фотореалистичной автоматически.

## v0.1 — база (готово)
- [x] C++20, CMake, OpenGL 3.3 Core, камера/окно/ввод
- [x] Процедурная геометрия, GGX direct light, directional PCF shadow map

## v0.2 — загрузка объектов (готово)
- [x] glTF 2.0 meshes/nodes/UV0/normals/tangents, embedded/external images
- [x] Base color, metallic/roughness, normal, AO, emissive
- [x] Загрузка моделей .gltf / .glb и smoke tests

## v0.3 — HDR и IBL (готово)
- [x] HDR radiance panorama, cubemap, diffuse irradiance, GGX prefilter, BRDF LUT
- [x] RGBA16F, bloom, exposure, ACES tonemapping
- [x] CLI screenshot и проверка непустого изображения

## v0.4 — динамическое освещение и отсечение (готово)
- [x] 0..8 точечных источников с обратноквадратичным затуханием (без локальных теней)
- [x] Движущиеся цветные источники для демонстрационной сцены
- [x] SSAO half resolution, 16 sample kernel, edge-aware blur, toggle
- [x] Local AABB + world-space frustum culling с учётом scale/rotation
- [x] Отдельная проверка shadow-caster bounds по light frustum
- [x] Статистика отправленных, видимых и отброшенных примитивов
- [x] Frustum unit tests + pixel comparisons on/off lighting

## v0.5 — прозрачные материалы и тесты (готово)
- [x] Alpha MASK/BLEND glTF: cutout-тени и SSAO, сортировка blended мешей, тесты кадров
- [ ] UV1, KHR glTF extensions
- [ ] Temporal anti-aliasing и motion vectors
- [ ] Auto exposure, более качественный multi-scale bloom
- [ ] GPU profiling, asset caching, instanced rendering, culling hierarchy
- [ ] Scene/ECS и игровой цикл с физическими объектами

## v0.6 — тени точечных источников (готово)
- [x] Shadow cubemaps 256² × 6 для первых двух точечных источников
- [x] Радиальная линейная depth-запись + 12-tap PCF сравнение в GLSL 330
- [x] Консервативное light-radius culling теневых примитивов
- [x] Alpha MASK вырезы в кубических тенях; BLEND исключены
- [x] CLI `--point-shadows 0..2`, `--light-time`, draw-call diagnostics
- [x] Интеграционные screenshots on/off + pixel difference в CTest

## v0.7 — изображения и сцена (план)
- [ ] Cascaded shadow maps для направленного света
- [ ] Temporal AA и motion vectors
- [ ] Автоматическая HDR-экспозиция, более качественный bloom
- [ ] GPU profiling, cached shadow maps, instanced rendering
- [ ] UV1 и KHR glTF extensions
- [ ] Scene/ECS, редактор сцен и физические объекты

## v0.6+ — более крупные системы (план)
- [ ] Скелетная анимация, физика/коллизии, звук, скрипты
- [ ] Vulkan/D3D12 backend, render graph
- [ ] Глобальное освещение (пробы/ray tracing) и real-time отражения
- [ ] Editor/asset browser, LOD, streaming больших сцен
- [ ] Golden image тесты, замеры FPS и GPU

Список будет уточняться по результатам тестирования, профилирования и обратной связи.
