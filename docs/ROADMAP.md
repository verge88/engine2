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

## v0.6 — тени точечных источников (готово)
- [x] Shadow cubemaps 256² × 6 для первых двух точечных источников
- [x] Радиальная линейная depth-запись + 12-tap PCF сравнение в GLSL 330
- [x] Консервативное light-radius culling теневых примитивов
- [x] Alpha MASK вырезы в кубических тенях; BLEND исключены
- [x] CLI `--point-shadows 0..2`, `--light-time`, draw-call diagnostics
- [x] Интеграционные screenshots on/off + pixel difference в CTest

## v0.7 — каскадные солнечные тени (готово)
- [x] 0/1/3 солнечных каскада (depth texture array 1536², OpenGL 3.3)
- [x] Практическая split-схема и стабильные матрицы с texel snapping
- [x] Индивидуальное light-space culling + MASK alpha в каждом каскаде
- [x] 3×3 PCF, настраиваемая дальность, debug tint и управление клавишей C
- [x] Unit tests геометрии каскадов и интеграционные on/off image tests

## v0.8 — временное сглаживание (готово)
- [x] 8-сэмпловый Halton jitter на матрице камеры
- [x] Full-res HDR history ping-pong и сохранение линейной глубины в alpha
- [x] Reprojection через depth + предыдущую view-projection матрицу камеры
- [x] Проверка disocclusion и 3×3 neighborhood color clipping / adaptive history weight
- [x] Сброс истории при resize, переключении и телепорте камеры
- [x] Стабильное CSM: каскады используют недрожащую проекцию
- [x] CLI --taa / --taa-weight, клавиша T, статистика накопления
- [x] Unit-тест Halton jitter и headless визуальные/диагностические проверки

## v0.9 — HDR-экспозиция и многоуровневый bloom (готово)
- [x] GPU 128² log-luminance с редукцией mipmap до 1×1 и CPU чтением
- [x] Geometric-mean auto-exposure, exponential time-based adaptation, ручная компенсация
- [x] Переключение auto/manual, key, speed, экспозиционные счётчики
- [x] Пять HDR уровней bloom с 13-tap downsample и tent upsample
- [x] Мягкий порог bloom и GPU-совместимое отключение bloom при нулевой силе
- [x] Исправлено: финальный композит действительно использует HDR после TAA
- [x] Unit-тесты экспозиции, визуальные проверки HDR-адаптации и bloom

## v1.0 — сущности игрового мира и фиксированное обновление (готово)
- [x] Scene runtime с generation handles, именами и рекурсивным удалением
- [x] Иерархия world/local transform с защитой от циклов и keepWorld reparent
- [x] MeshRenderer и PointLight компоненты с извлечением для текущего рендерера
- [x] RigidBody-компонент: гравитация, приближённая сфера/плоскость, отскок
- [x] 1/60 fixed timestep с накоплением времени и ограничением числа шагов
- [x] Интеграция procedural и glTF геометрии/освещения с Scene
- [x] CLI physics demo, --sim-dt, SceneStats, unit- и visual-тесты

## v1.1 — столкновения физических сфер (готово)
- [x] Sweep-and-prune broadphase по оси X вместо проверки всех пар O(N²)
- [x] Narrowphase sphere-vs-sphere с разрешением проникновения
- [x] Закон импульса с учётом масс и коэффициента восстановления
- [x] Статические сферические коллайдеры и безопасная обработка одинаковых центров
- [x] Статистика потенциальных пар и подтверждённых контактов
- [x] CLI collision demo, unit tests равных/разных масс и OpenGL image regression

## v1.2 — расширение игровых систем (план)
- [ ] 3D broadphase / capsule/box colliders, трение, итерационный решатель, CCD
- [ ] Сохранение/загрузка Scene в JSON и инструмент инспектора сущностей
- [ ] Связать узлы glTF с родительскими сущностями, анимации и skinning
- [ ] Motion vectors объектов и улучшение TAA
- [ ] GPU profiling, cached shadow maps, instanced rendering
- [ ] UV1 и KHR glTF extensions, async exposure readback

## v1.3+ — крупные игровые системы (план)
- [ ] Скелетная анимация, физика/коллизии, звук, скрипты
- [ ] Vulkan/D3D12 backend, render graph
- [ ] Глобальное освещение (пробы/ray tracing) и real-time отражения
- [ ] Editor/asset browser, LOD, streaming больших сцен
- [ ] Golden image тесты, замеры FPS и GPU

Список будет уточняться по результатам тестирования, профилирования и обратной связи.
