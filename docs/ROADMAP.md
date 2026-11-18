# engine2 — дорожная карта

**Цель:** собственный расширяемый 3D-движок с реалистичным освещением и современным игровым pipeline. Поддержка PBR, HDR и IBL ещё не означает фотореализм во всех сценах.

## v0.1 — основание (реализовано)

- [x] C++20, CMake, OpenGL 3.3, окно, ввод, камера
- [x] Процедурные меши, GGX metallic–roughness, PCF shadow mapping

## v0.2 — ассеты (реализовано)

- [x] glTF 2.0 .gltf/.glb, меши, иерархия, трансформации
- [x] Base Color, Metallic/Roughness, Normal, AO, Emissive textures
- [x] Раздельная обработка sRGB/linear, mipmaps, sampler settings
- [x] Smoke tests для сцены с текстурами

## v0.3 — окружение и HDR (реализовано)

- [x] HDR Radiance панорама или процедурное HDR-небо
- [x] Кубическая карта окружения, diffuse irradiance convolution
- [x] GGX prefiltered specular cubemap + BRDF split-sum LUT
- [x] HDR framebuffer (RGBA16F), skybox, exposure + ACES
- [x] Bright-pass / 6-pass separable bloom на половине разрешения
- [x] CLI для загрузки HDR, регулировки exposure/bloom и screenshot PPM
- [x] Проверка содержимого итогового изображения в CI

## v0.4 — качество и ресурсный pipeline (следующий этап)

- [ ] Alpha mask/blend, UV1, распространённые glTF KHR extensions
- [ ] Отрисовка многих источников света, cascaded shadow maps
- [ ] SSAO, TAA, SSR, HDR auto exposure, более качественный bloom
- [ ] Асинхронный asset cache, background streaming
- [ ] Render timings, GPU profiling, frustum culling/instancing

## v0.5+ — игровые подсистемы и новые API

- [ ] Scene/ECS, collision/physics, animation, audio, scripting/editor
- [ ] Vulkan/D3D12 backend + render graph, ray-traced reflections/GI
- [ ] Регрессионные golden image tests на известных GPU
- [ ] Ландшафт, LOD, streaming, оптимизация больших сцен

Ограничения и команды для проверки текущей версии — в README.
