# engine2 — дорожная карта

Цель — собственный расширяемый 3D-движок, который со временем сможет давать убедительную фотореалистичную картинку. Версия 0.1 — программный фундамент, а не заявление о готовом фотореализме.

## 0.1 — минимальный рендерер (текущий этап)

- [x] Кроссплатформенный CMake-проект C++20
- [x] OpenGL 3.3 Core, окно, цикл, ввод, fly camera
- [x] Процедурная геометрия, материал metallic/roughness
- [x] Cook–Torrance GGX + directional shadows PCF
- [x] Filmic tonemapping и smoke test

## 0.2 — ресурсы и настоящий material workflow

- [ ] Загрузчик glTF 2.0 (позиции, нормали, тангенты, UV, индексы)
- [ ] PBR texture sets (base color, ORM, normal, emissive), sRGB/linear
- [ ] Material/Texture/Model кеш, mipmaps, anisotropic filtering
- [ ] HDR-окружение (equirectangular → cubemap), irradiance + prefilter + BRDF LUT
- [ ] Нормальная модель сцены: Entity, Transform, MeshRenderer, Camera, Light

## 0.3 — качество изображения и масштабирование

- [ ] Cascaded shadow maps, несколько источников света
- [ ] HDR render targets, bloom, экспозиция
- [ ] SSAO, screen-space reflections, temporal anti-aliasing
- [ ] Frustum culling, instancing, profiling, ресурсы GPU

## 0.4+ — продвинутый рендеринг и игровые системы

- [ ] Vulkan/Direct3D 12 backend и render graph
- [ ] Ray-traced shadows / reflections / GI с fallback
- [ ] Система потоковой загрузки больших миров
- [ ] Физика, анимация, аудио, скрипты, редактор
- [ ] Регрессионные image tests, показатели производительности

Порядок работ может изменяться по результатам профилирования и поддержки оборудования.
