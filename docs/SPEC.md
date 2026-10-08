# ZYRON — спецификация (сжатая редакция)

Источник: ТЗ владельца от 2026-10-07 (89 разделов; в оригинале продукт назывался «Mini AI StemDeck», с 2026-10-07 — ZYRON). Здесь сохранены **все требования**; убраны ASCII-схемы
и повторы. Нумерация `§N` совпадает с оригиналом — на неё ссылаются ROADMAP, ADR, агенты и ревью.
Пробелы и противоречия оригинала вынесены в конец («Пробелы и уточнения»), решения по ним — в `DECISIONS.md`.

---

## Цель, язык, платформы

**§1 Цель.** Собственное кроссплатформенное DJ-приложение: 2 деки, 4 деки, полноценный realtime audio engine,
stem separation, локальный AI, CUDA, MIDI-контроллеры, локальная библиотека, автоанализ музыки, AI-рекомендации,
в будущем — полностью автономный AI-DJ. Полностью локально, без облака для основной работы.
Принцип: **DJ software first, AI second** — без AI приложение остаётся полноценным DJ-инструментом.

**§2 Язык.** Основное приложение — **C++** (C++20/23). Не использовать как основу: TypeScript/React/Electron,
C#, Python. Стек: C++20/23, JUCE, CMake, CUDA, SQLite, FFmpeg, ONNX Runtime/LibTorch. Python — только
вспомогательная среда для AI-моделей, если модель технически не работает через C++ runtime.
Application/core/audio layer остаются C++.

**§3 Почему C++.** Realtime audio, низкая задержка, DSP, многоканальный mixer, 2–4 одновременных деки, stems,
GPU/CUDA, MIDI, кроссплатформенность, нативная производительность, интеграция с pro-audio библиотеками;
рассчитано на долгосрочное развитие.

**§4 Платформы (равноправные).** Windows 10/11; macOS (Intel + Apple Silicon); Linux (Ubuntu/Debian,
PipeWire/ALSA). Нельзя делать Windows-only и потом портировать.

**§5 JUCE** — основной framework: GUI, окна, контролы, audio devices и callbacks, MIDI, клавиатура, мышь,
drag & drop, таймеры, filesystem abstraction, кроссплатформенность. Native C++, не Electron.

**§6 CMake.** Сборка независимо от IDE. Windows: Visual Studio, Ninja. macOS: Xcode, Ninja. Linux: GCC,
Clang, Ninja. Файлы: `cmake/`, `CMakeLists.txt`, `CMakePresets.json`. Пресеты: `windows-debug`,
`windows-release`, `macos-debug`, `macos-release`, `linux-debug`, `linux-release`.

## Архитектура

**§7 Подсистемы:** Application, UI, Core, AudioEngine, DSP, Deck, Mixer, Library, Analysis, Stems, AI, MIDI,
Recording, Platform, Tests.

**§8 Потоки управления.** UI → Application State → Command System → Core → Audio Engine. AI → Command API →
Core → Audio Engine. MIDI → Mapping → Command API → Core. UI, AI и MIDI используют **один** Command API.
UI не управляет realtime-обработкой напрямую.

**§9 Audio Engine:** realtime playback; 2–4 деки; sample-accurate synchronization; mixing; volume; gain; EQ;
filters; effects; tempo; pitch; keylock; loops; cue; master output; headphone output; recording.

**§10 Realtime thread.** Никогда не выполняет тяжёлого. Нельзя: загружать файлы, stem separation, SQLite,
network, AI inference, крупные аллокации, блокирующие mutex. Только: audio processing, mixing, DSP,
playback, routing.

**§11 Audio graph:** Deck A–D → Channel → EQ → Filter → FX → Mixer → Master → Audio Output.

## Деки, сведение, DSP

**§12 Режим 2 деки (A, B).** Каждая дека: загрузка трека, playback, pause, cue, waveform, BPM, beatgrid,
key, pitch, tempo, keylock, sync, loops, hot cues, gain, volume, EQ, filter, stems.

**§13 Режим 4 деки (A B / C D).** Все четыре работают одновременно; комбинации A+B, A+C, A+B+C, A+B+C+D.
На поддерживаемом железе — без audio dropouts.

**§14 DeckState** (структура ориентировочная, состояние каждой деки независимо): track, playing, paused,
position, bpm, pitch, syncEnabled, keyLockEnabled, gain, volume, EQ, filter, loop, cues, stems.

**§15 BPM analysis.** После импорта: BPM, beat positions, bars, downbeats, beatgrid.

**§16 Beatgrid editor.** Изменить BPM, поставить первый beat, сдвинуть grid, пересчитать grid, изменить
phase. Изменения сохраняются.

**§17 Beat sync.** Master Deck (напр. A); остальные деки SYNC → master. Обеспечить: BPM matching, phase
alignment, beat alignment.

**§18 Pitch/Tempo диапазоны:** ±6 %, ±10 %, ±16 %, ±25 %, ±50 %, ±100 %; настраивается на каждую деку.

**§19 Keylock.** ON — при смене BPM тональность сохраняется. OFF — pitch и tempo связаны, как у винила /
обычного digital pitch.

**§20 Mixer.** Канал: GAIN, LOW, MID, HIGH, FILTER, VOLUME, CUE. Master: MASTER, MASTER LIMITER.
Crossfader A↔B. Для 4 дек — assign (по умолчанию A→Left, B→Right, C→Left, D→Right; настраиваемый).

**§21 EQ.** Минимум LOW/MID/HIGH. У каждой полосы: gain, соответствующая частотная характеристика, плавная
интерполяция параметров; никаких резких скачков при движении knob/fader.

**§22 Filter.** HPF и LPF; параметры frequency, resonance.

**§23 FX.** Filter, Delay, Echo, Reverb, Flanger, Phaser. Архитектура позволяет добавлять новые FX без
переписывания mixer.

**§24 Waveform.** Full waveform (весь трек) и Detail waveform (текущая область). Отображать: playhead,
beatgrid, bars, hot cues, loops, текущую позицию.

**§25 Hot cues.** 8 штук (CUE 1–8). Поля: position, color, name, type. Типы: Cue, Loop, Intro, Drop, Break,
Outro, Memory.

**§26 Loops.** Длины: 1/2, 1, 2, 4, 8, 16, 32 beats. Функции: Loop In, Loop Out, Reloop, Exit, Loop Move,
Loop Length.

## Библиотека и анализ

**§27 Форматы:** MP3, WAV, FLAC, OGG, AAC, M4A. Библиотека локальная.

**§28 SQLite** хранит: tracks, playlists, metadata, BPM, key, energy, analysis, cue points, loops, stem
status, waveform metadata. **Большие waveform/stem-файлы не хранить в SQLite.**

**§29 Library Scanner.** Пользователь выбирает папку (напр. `D:\Music\DnB`) → Scan → Find files → Read
metadata → Database → Analysis Queue. Работает как background worker.

**§30 Поиск** по: artist, title, album, genre, BPM, key, energy, path. Позже: semantic/AI search
(«Найди тяжёлый neurofunk около 174 BPM»).

**§31 Analysis pipeline:** Import → Metadata → Duration → BPM → Beatgrid → Key → Energy → Waveform → Ready.
Каждая операция — отдельная task.

## Stems и AI-runtime

**§32 Stem separation** — ключевая AI-функция. Модель: Demucs (Vocals, Drums, Bass, Other). В будущем
BS-Roformer и другие.

**§33 Интерфейс:** `class StemSeparator { virtual Result separate(const AudioFile& input) = 0; }`.
Реализации: `DemucsStemSeparator`, `BSRoformerStemSeparator`, `FutureStemSeparator`.

**§34 AI Runtime абстрагирован:** ONNX Runtime, LibTorch, Future Runtime. Модель не диктует архитектуру.

**§35 CUDA — обязательный backend.** Тестовая конфигурация: 2 × RTX 3090, 24 GB VRAM. Определять: GPU, VRAM,
CUDA version, compute capability.

**§36 Multi-GPU.** AI-очередь поддерживает GPU 0 / GPU 1 (Track 01 → GPU 0, Track 02 → GPU 1, Track 03 → GPU 0 …).
Учитывать VRAM.

**§37** Core не зависит от CUDA напрямую. AI Backend: CUDA, Metal, CPU.
**§38 macOS:** Apple Silicon → Metal/MPS (CUDA не нужен); Intel → CPU или доступный GPU backend.
**§39 Linux:** NVIDIA → CUDA; без GPU → CPU; позже ROCm для AMD.
**§40 CPU fallback.** Нет GPU → AI на CPU; приложение продолжает работать, производительность может быть ниже.

**§41 GPU worker.** AI-обработка в отдельных worker threads/processes (Application → AI Queue → GPU 0 / GPU 1);
не блокирует UI и realtime audio.

**§42 Stem cache.** После обработки Track → stems (vocals, drums, bass, other); при повторной загрузке
не пересчитывать.

**§43 Stem Mixer.** Каждый stem (VOCALS, DRUMS, BASS, OTHER): volume, mute, solo, cue. Позже: EQ, filter, FX.

**§44 Stems разных треков** микшируются в общем mixer (A: drums+bass, B: vocals, C: other).

## Роутинг, запись, управление

**§45 Routing:** MASTER, HEADPHONES, CUE. Windows → WASAPI/ASIO; macOS → CoreAudio; Linux → PipeWire/ALSA.
Основная абстракция audio-device — JUCE.

**§46 Recording.** REC master. Форматы: WAV, FLAC, MP3. Частоты: 44.1 / 48 / 96 kHz.

**§47 MIDI.** Input, output, MIDI Learn, mapping, CC, Note, Pitch Bend. (MIDI knob → Mapping → параметр.)
**§48 MIDI Learn.** Right click по параметру → MIDI Learn → пошевелить физический knob → mapping создаётся
автоматически.
**§49 HID (будущее):** DJ-контроллеры, геймпады, custom HID, RadioMaster Boxer, другие контроллеры.

**§50 Command System.** Все действия приложения — через единый Command API. Примеры: LOAD_TRACK,
UNLOAD_TRACK, PLAY, PAUSE, CUE, SYNC, SET_BPM, SET_PITCH, SET_KEYLOCK, SET_VOLUME, SET_GAIN, SET_EQ,
SET_FILTER, LOOP_IN, LOOP_OUT, LOOP_EXIT, SET_STEM_VOLUME, MUTE_STEM, SOLO_STEM.

## AI-функции (будущее)

**§51 AI Command API.** AI использует те же команды (LOAD_TRACK(deckB, track123) → SYNC(deckB) →
SET_EQ(deckB, LOW, −6) → PLAY(deckB)). Автономный AI-DJ без изменений Audio Engine.
**§52 Рекомендации.** Анализ: Current Track, BPM, Key, Energy, Genre, Structure → поиск следующего (пример:
170–178 BPM, 7A/8A/9A, Energy 7–8.5, Neurofunk).
**§53 Energy** 1–10. Позже: loudness, spectral density, drum density, bass intensity, vocal density,
arrangement, drop intensity.
**§54 Структура трека:** INTRO, BUILD, DROP, BREAK, DROP 2, OUTRO — используется AI-DJ.
**§55 Set Builder.** «Собери 60-минутный DnB сет» → Track 01..N. Учитывает BPM, key, Camelot, energy, genre,
intro, outro, drops, breakdowns.
**§56 Energy curve.** График энергии по времени; AI строит сет под заданную кривую.
**§57 Transition Planner.** Выбирает transition start, duration, EQ automation, bass swap, stem mute, filter,
FX, volume automation (пример: Track A → bass gradually reduced → drums remain → Track B intro → bass B
enters → drop B).
**§58 Autonomous AI DJ.** Library → Analysis → AI Set Planner → Select Track → Load Deck → Sync → Stem
Processing → Transition → Mix → Select Next → Repeat. Пользователь задаёт: Genre, Duration, Starting Energy,
Target Energy, BPM Range, Style.
**§59 Локальная LLM (будущее):** понимание команд, playlist/set planning, выбор трека, объяснение
рекомендаций, управление Command API. **Не** занимается realtime DSP.
**§60 Пример:** «Сделай переход на что-нибудь тяжелее» → Current Energy 6.4 → поиск Energy 7.5–8.5,
BPM 172–176, совместимый key, подходящий intro → LOAD, SYNC, SET EQ, SET STEMS, PLAY, TRANSITION.

## UI

**§61 Основной интерфейс (2 деки):** меню (File, Library, 2 Decks, 4 Decks, AI, Settings); GLOBAL WAVEFORM;
Deck A | Deck B (Track, BPM/KEY, WAVEFORM, VOC/DRUM/BASS/OTHER, CUE PLAY SYNC LOOP); MIXER; LIBRARY.
**§62 4 деки:** A B / C D / MIXER / LIBRARY.
**§63 Тема:** Dark, Light; основной интерфейс оптимизирован под dark.

## Нефункциональные требования

**§64 Hardware detection при запуске:** OS, CPU, RAM, GPU (+VRAM, CUDA), Audio devices, MIDI devices.
**§65 Performance.** UI не зависает при stem separation, library scan, AI inference, waveform generation.
Audio — без clicks, pops, dropouts, buffer underruns. AI — в background.
**§66 Потоки (минимум):** Main/UI, Realtime Audio, Library Worker, Analysis Worker, AI Worker 0, AI Worker 1,
MIDI Thread. Реализация может меняться.
**§67 Память.** Realtime-safe allocation, buffer reuse, preallocation, lock-free queues где нужно, нет утечек,
RAII. Современный C++: `unique_ptr`, `shared_ptr`, `optional`, `variant`, `span` — где оправдано.
**§68 Код.** C++20/23; RAII; smart pointers; const correctness; namespaces; модульность; unit tests.
Избегать: глобального состояния, raw pointers без необходимости, giant classes, god objects, UI-логики в
Audio Engine.
**§69 Зависимости.** Предпочтительно: C++, JUCE, CMake, CUDA, SQLite, FFmpeg, ONNX Runtime, LibTorch. Без
необходимости не добавлять; у каждой — понятная лицензия, cross-platform support, активное развитие,
обоснование.
**§70 Тесты.** Unit: BPM, beatgrid, mixer, EQ, filters, loops, command system, library, metadata.
Audio: clipping, sample alignment, sync, tempo, routing. Integration: Load track → Analyze → Load deck →
Play → Sync → Mix → Record.
**§71 CI/CD.** Сборка минимум для Windows (MSVC), macOS (Clang), Linux (GCC/Clang). Каждый PR: Build, Unit
Tests, Static Analysis.
**§72 Packaging.** Windows: .exe, MSI. macOS: .app, .dmg. Linux: AppImage, .deb. Позже Flatpak.
**§73 Installer** ставит приложение, нужный runtime, default configuration. AI-модели в installer не
обязательны; первый запуск: Download/Import → Verify → Ready. Для offline — возможность указать локальную
папку моделей.
**§74 AI Model Manager** (отдельный раздел): Demucs, BS-Roformer, BPM model, Key model и т. д. со статусом
Installed/Not installed; показывать model, version, size, backend, GPU compatibility.
**§75 Offline-first.** Playback, mixing, library, stems, analysis, recording, AI — без интернета. Интернет
только для обновлений, скачивания моделей, проверки версий.
**§76 Security.** Не запускать пользовательские AI-модели с произвольным native code без предупреждения.
Валидировать пути к файлам. Не выполнять shell-команды из AI. AI работает только через Command API.

## Структура и абстракции

**§77 Структура проекта:**
```
ZYRON/  CMakeLists.txt  CMakePresets.json
  src/ Application/ UI/{Deck,Mixer,Library,Waveform,Settings,AI}/ Core/{Commands,State,Events}/
       Audio/{Engine,Deck,Mixer,DSP,Effects,Routing}/ Library/{Database,Scanner,Metadata}/
       Analysis/{BPM,Beatgrid,Key,Energy}/ Stems/{Demucs,BSROFormer,Cache}/
       AI/{Runtime,Recommendation,SetBuilder,Transition}/ MIDI/ Recording/ Platform/{Windows,MacOS,Linux}/
  tests/  resources/  models/
```
**§78 Platform abstraction.** OS-specific код — только в `Platform/{Windows,MacOS,Linux}`. Core без
Windows-specific кода. Плохо: `#ifdef _WIN32` по всему проекту. Допустимо: `Platform::AudioDevice`,
`Platform::FileSystem`, `Platform::GPU`, `Platform::Window` с отдельными реализациями.
**§79 GPU abstraction.** `class GPUBackend { bool isAvailable() const; std::vector<GPUInfo> getDevices();
AIResult run(const AIRequest&); }`. Реализации: CudaBackend, MetalBackend, CpuBackend; позже RocmBackend,
DirectMLBackend.
**§80 Audio backend.** Только JUCE abstraction; не писать отдельные Audio Engine под ОС. Core видит
`AudioDevice`, `AudioBuffer`, `AudioStream`; JUCE скрывает OS backend.

## План и приоритеты

**§81 Roadmap (фазы):** 1 Foundation · 2 Audio Engine · 3 DJ Features · 4 4-Deck · 5 Stems · 6 AI Analysis ·
7 AI Assistant · 8 AI DJ · 9 Controllers. Детализация — `ROADMAP.md`.
**§82 MVP 1:** Select Music Folder → Scan Library → Analyze Tracks → Load A → Load B → Play → SYNC → EQ →
FILTER → STEMS → Mix → REC; всё локально.
**§83 MVP 2:** 4 Decks, Loops, Hot Cues, Keylock, Key Detection, Recording, MIDI.
**§84 MVP 3:** AI Recommendation, AI Set Builder, Energy Curve, Track Compatibility.
**§85 Финальный продукт:** native DJ application: Deck A/B/C/D → Mixer → Stems → AI Engine → CUDA | Metal | CPU.
**§86 Ключевые требования (обязательно):** C++20/23, JUCE, CMake, Windows, macOS, Linux, 2 decks, 4 decks,
realtime audio, waveform, BPM, beatgrid, sync, EQ, filter, loops, hot cues, recording, MIDI-ready, local
library, SQLite, stem separation, Demucs, CUDA, CPU fallback, GPU abstraction, AI worker, cache, modular
architecture. В будущем: BS-Roformer, Metal/MPS, ROCm, AI recommendations/set builder/transitions,
autonomous AI DJ, MIDI controllers, HID, local LLM.
**§87 Главный принцип.** Единый C++ Core, поверх — Windows/macOS/Linux; CUDA (Win/Linux), Metal (macOS),
общий AI Engine. Основной код C++, UI JUCE, build CMake, audio JUCE + native C++ DSP, AI ONNX Runtime/LibTorch,
NVIDIA CUDA, DB SQLite, кодеки FFmpeg.
**§88 Критерий качества.** Должно ощущаться как профессиональный DJ application, не как эксперимент.
Приоритеты: 1 Audio stability · 2 Low latency · 3 Cross-platform · 4 Performance · 5 Modular architecture ·
6 GPU acceleration · 7 AI functionality · 8 UI/UX. AI не ломает базовый DJ workflow.
**§89 Итог.** Локальный ZYRON: Обычный DJ → Stem DJ → AI-assisted DJ → AI DJ → автономный
локальный AI DJ; единое кроссплатформенное C++ приложение с модульным Audio Core.

---

## Пробелы и уточнения (внесены при конвертации ТЗ; нужны решения)

1. **Лицензия JUCE vs распространение — РЕШЕНО (2026-10-07):** владелец — «чисто опен сурс без коммерции пока
   что» → проект под AGPLv3, JUCE по AGPL, зависимости должны быть совместимы с AGPLv3 → ADR-0002.
2. **Linux PipeWire (§45).** Нативные backend'ы JUCE — ALSA и JACK; PipeWire работает через их совместимость.
   Проверить на целевых дистрибутивах до обещаний «PipeWire support».
3. **ASIO (§45).** Требует Steinberg ASIO SDK — проверить условия лицензии на момент интеграции.
4. **Наушники/CUE (§45).** Нужен ≥ 4 выходных каналов на одном устройстве (или split-cue на стерео-карте).
   Режим «split cue» (L = cue, R = master) нужен как fallback для обычных звуковых карт; JUCE не объединяет
   разные устройства в одно.
5. **Roadmap §81 не содержит Library/Scanner/SQLite/Analysis**, хотя MVP 1 (§82) их требует → добавлены
   в Phase 3 `ROADMAP.md`.
6. **Recording thread** отсутствует в §66; запись на диск не может идти из RT-потока → отдельный writer thread.
7. **Алгоритмы анализа** (beat tracking, key, energy, structure) не заданы; многие известные MIR-библиотеки
   под GPL/AGPL → ADR-0010.
8. **«Поддерживаемое железо» (§13)** не определено — нужен минимальный профиль (CPU, RAM, размер буфера) для
   теста «4 деки без dropouts».
9. **Stems «на лету».** Разделение трека занимает время; для мгновенной загрузки стемы должны быть
   предрассчитаны (кэш §42). Поведение при загрузке трека без кэша (прогресс, фоновая обработка, play без
   stems) надо описать в UX.
10. **Time-stretch/keylock движок (§19)** не выбран → ADR-0005; влияет на CPU-бюджет 4 дек × 4 stems.
11. **Master limiter (§20)** — параметры (lookahead, ceiling) не заданы.
12. **Формат mapping-файлов MIDI/HID (§47–49)** не задан.
13. **Имя продукта — РЕШЕНО (2026-10-07):** переименовано из «Mini AI StemDeck» в **ZYRON** (продукт и репозиторий);
    namespace `zyron`, CMake-таргеты `zyron_<module>`, макросы `ZYRON_*` (ADR-0012).
