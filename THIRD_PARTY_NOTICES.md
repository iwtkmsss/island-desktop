# Сторонній код

## Dynamic Island for Windows

Copyright (c) 2026 devcode90. MIT License.

Джерело: https://github.com/devcode90/Dynamic-Island-for-Windows

Використано надану користувачем копію мода 1.4.0 у `references/original-mod/`. Адаптовано пружину `SpringValue`, вибір активної медіасесії й алгоритм оцінки позиції треку. Змінено інтеграцію з Qt, керування потоками, скасування, кешування, поведінку інтерфейсу та відокремлено вибір розділу користувача.

Додатково адаптовано такі частини наданого файлу `dynamic-island-for-windows.wh.cpp`:

- **Аналіз звуку:** `SampleAudioAmplitude`, `SpectrumAnalyzer` і `PushWaveformSample` (рядки 5800–6050) у `src/core/borrowed/audioanalysis.h`. Збережено вікно Hann, radix-2 FFT, логарифмічні частотні смуги, спектральну корекцію, автоматичне регулювання підсилення та згладжування. Розширено декодування PCM/float, обробку тиші, скидання потоку та згасання залежно від часу.
- **Захоплення системного звуку:** схема WASAPI loopback з `AudioThreadProc` (рядки 7794–7935) у `src/platform/windows/audiocontroller.cpp`. Захоплення спільного потоку пристрою виведення, COM-ресурси, перепідключення й завершення адаптовано до окремого керованого потоку Qt. Замість черги кадрів інтерфейс читає останній результат аналізу.
- **Системна гучність:** `VolumeNotifyClient` і `BindVolumeEndpoint` (рядки 8108–8272) у `src/platform/windows/volumecontroller.cpp`. Підписки на зміни endpoint і гучності, життєвий цикл COM та команди керування адаптовано до потоку Qt; сповіщення передають лише ознаки змін.
- **Bluetooth:** `ClassifyBluetoothDevice` (рядки 5354–5384), `TryReadClassicBluetoothBatteryPercent` з властивістю `PKEY_Bluetooth_Battery` (рядки 5465–5539) і схема класичних та BLE-пристроїв з `BluetoothThreadProc` (рядки 5712–5793) у `src/platform/windows/bluetoothcontroller.cpp`. Збережено визначення типу за назвою, читання батареї з вузлів BTHENUM через SetupAPI та окреме перерахування класичних і BLE-пристроїв. Замість відокремленого потоку й подій DeviceWatcher використано керований потік Qt із повторним скануванням, скасуванням і нумерацією результатів; вузли батареї перераховуються один раз за скан; тип за Class of Device винесено до `src/core/devicetracker.h`. Читання батареї через GATT не використовується.
- **Повноекранні програми:** `IsForegroundFullscreen` (рядки 2108–2147) у `src/platform/windows/shellcontroller.cpp`. Перевірку обмежено монітором острівця; виключено власні, системні та згорнуті вікна.

Номери рядків стосуються саме незмінної наданої копії. Нові адаптації також поширюються з наведеним повідомленням про авторське право та ліцензією MIT.

Ліцензійний текст upstream, отриманий 11.11.2026: `third_party/dynamic-island/LICENSE`. Точна відповідність наданого великого файлу окремому upstream-коміту не встановлена.

## Qt

Використовуються динамічні бібліотеки Qt 6.8.3. Їхні ліцензії й відомості про сторонні компоненти: https://doc.qt.io/qt-6.8/licenses-used-in-qt.html

## KDE Frameworks / Plasma (Linux desktop panel)

The Linux panel links dynamically to KDE's LibTaskManager (TasksModel; Eike Hein and KDE contributors), KService, KIO, KIconThemes and KWindowSystem, and uses the installed Kirigami QML icon component. Their upstream sources and per-file licensing are available at https://invent.kde.org/plasma/plasma-workspace/-/tree/master/libtaskmanager and https://invent.kde.org/frameworks. TasksModel headers specify LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL. No Plasma applet source has been copied into this project. The tray adapter interoperates with StatusNotifierItem and Canonical DBusMenu over D-Bus.
