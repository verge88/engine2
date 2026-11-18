# engine2 — дорожная карта

Цель — расширяемый собственный 3D-движок с фотореалистичным рендерингом. v0.2 — полноценный шаг к загрузке реальных ассетов, но не завершённый игровой движок.

## 0.1 — минимальный рендерер (завершено)

- [x] C++20, CMake, OpenGL 3.3 Core, окно, ввод, камера
- [x] Процедурная геометрия, metallic–roughness PBR GGX
- [x] Directional shadow mapping + PCF, filmic tone mapping

## 0.2 — ресурсы и glTF (текущий этап)

- [x] glTF 2.0 (.gltf/.glb): меши, иерархия, трансформации, примитивы
- [x] BaseColor, MetallicRoughness, Normal, AO, Emissive textures
- [x] Gamma-correct sRGB / linear formats, sampler state и mipmaps
- [x] Профиль smoke test с внешним glTF со встроенными изображениями
- [ ] UV1, alpha modes, KHR extensions, sparse accessors
- [ ] Кэш ресурсов между различными файлами, асинхронная загрузка

## 0.3 — высокое качество изображения

- [ ] HDR environment: cubemap, diffuse irradiance, specular prefilter, BRDF LUT
- [ ] HDR render targets + bloom, параметры экспозиции
- [ ] Cascaded shadow maps + несколько источников света
- [ ] SSAO, screen-space reflections, TAA
- [ ] Frustum culling, instancing и профилирование GPU

## 0.4+ — современные GPU и игровые системы

- [ ] Vulkan/Direct3D 12 renderer backend и render graph
- [ ] Ray-traced shadows, reflections, GI и совместимый fallback
- [ ] Система Entity/Scene, editor, streaming, asset database
- [ ] Физика, скелетная анимация, аудио, скрипты
- [ ] Регрессионные image tests и оптимизация производительности

Ограничения и тестирование актуальной версии документированы в README.
