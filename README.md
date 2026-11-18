# engine2

**engine2** — учебно-практическое ядро собственного 3D-движка на **C++20** с OpenGL 3.3 Core. Это рабочий старт для фотореалистичного рендеринга, **не готовый аналог Unreal Engine**.

## Что уже реализовано

- Собственный цикл приложения, управление камерой от первого лица и обработка ввода.
- Генерация 3D-сетки сферы и плоскости, VAO/VBO/EBO, трансформации объектов.
- Прямое освещение по **Cook–Torrance / GGX PBR** (metallic–roughness).
- **Directional shadow map** 2048×2048 с 3×3 PCF-фильтрацией.
- Hemisphere ambient fill, аппроксимация **ACES filmic tone mapping** и вывод в sRGB.
- Демо: 25 сфер с разной металличностью/шероховатостью и пол с процедурной текстурой.
- CMake, автоматическая подгрузка зависимостей и Linux CI со smoke test.

Используются только базовые библиотеки: **GLFW** (окно и ввод), **glad** (загрузка OpenGL) и **GLM** (линейная алгебра). Система материалов, проход теней, шейдеры, геометрия и рендерер написаны в проекте.

## Сборка

Требования: CMake 3.24+, C++20 compiler, Python 3 (для glad), Git, драйвер с **OpenGL 3.3 Core** и интернет при первой конфигурации CMake. На Linux нужны заголовки X11 и OpenGL (например, `libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev` в Ubuntu).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/engine2
```

Windows (Visual Studio): `cmake -S . -B build`, затем `cmake --build build --config Release`; приложение будет в `build/Release/engine2.exe`. macOS: требуется поддержка OpenGL 3.3 Core; в GLFW включён forward-compatible контекст.

```bash
./build/engine2 --frames 3   # запустить 3 кадра и завершиться (для CI)
```

После сборки шейдеры копируются рядом с исполняемым файлом в каталог `shaders`; при перемещении бинарника копируйте также каталог шейдеров.

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

## Ограничения версии 0.1

PBR-освещение — **лишь часть фотореализма**. В этой версии нет glTF/asset pipeline, PBR-текстур, image-based lighting, SSAO/SSR, TAA, глобального освещения, трассировки лучей, системы сцены/ECS, физики и редактора. Свет окружения пока приближённый, тени — от одного направленного источника. См. [план развития](docs/ROADMAP.md).

### Структура

```text
include/engine2/   Публичные C++ интерфейсы
src/               Реализация и демо-приложение
shaders/           GLSL 330: PBR + shadow pass
docs/ROADMAP.md    Технические этапы развития
.github/workflows/ Сборка и smoke test
```
