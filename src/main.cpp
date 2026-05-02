#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <knownfolders.h>
#include <shlobj.h>

#include "resource.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <functional>
#include <random>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;

namespace {

constexpr UINT_PTR kAutomationTimer = 1;
constexpr int kTimerMs = 5;
constexpr int kBurstCycles = 8;
constexpr int kClientWidth = 1260;
constexpr int kClientHeight = 720;
constexpr int kTimedKeySlots = 4;
constexpr wchar_t kWindowClass[] = L"OGXAutoClickerWindow";

class App;
App* g_app = nullptr;
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;

enum class Page {
    Mouse,
    Keyboard,
    Settings
};

enum class Language {
    English = 0,
    Turkish,
    Russian,
    German,
    French,
    Spanish
};

enum class MouseButton {
    Left,
    Right
};

enum class TriggerMode {
    Toggle,
    Hold,
    Burst
};

enum class InputBackend {
    SendInputBackend,
    GameCompatibleBackend,
    LegacyMouseEvent
};

enum class BindTarget {
    None,
    MouseLeftHotkey,
    MouseRightHotkey,
    KeyboardTargetKey,
    KeyboardHotkey,
    KeyboardTask0Key,
    KeyboardTask1Key,
    KeyboardTask2Key,
    KeyboardTask3Key
};

enum class NumberTarget {
    None,
    MouseLeftCps,
    MouseRightCps,
    MouseLeftRandomMin,
    MouseLeftRandomMax,
    MouseRightRandomMin,
    MouseRightRandomMax,
    KeyboardRate,
    KeyboardTask0Seconds,
    KeyboardTask1Seconds,
    KeyboardTask2Seconds,
    KeyboardTask3Seconds
};

struct MouseClickConfig {
    UINT hotkey = VK_F6;
    bool doubleClick = false;
    int cps = 10;
    bool randomCps = false;
    int randomMinCps = 20;
    int randomMaxCps = 30;
    TriggerMode mode = TriggerMode::Toggle;
    InputBackend backend = InputBackend::GameCompatibleBackend;
    bool running = false;
    int burstRemaining = 0;
    double lastFireMs = 0.0;
    double nextIntervalMs = 0.0;
};

struct TimedKeyTask {
    bool enabled = false;
    UINT key = L'X';
    int intervalSec = 40;
    double lastFireMs = 0.0;
};

struct KeyboardConfig {
    UINT targetKey = VK_SPACE;
    UINT hotkey = VK_F8;
    bool mainKeyEnabled = true;
    bool doubleTap = false;
    int rate = 8;
    TriggerMode mode = TriggerMode::Toggle;
    InputBackend backend = InputBackend::GameCompatibleBackend;
    std::array<TimedKeyTask, kTimedKeySlots> tasks = {{
        {false, L'X', 40, 0.0},
        {false, L'1', 50, 0.0},
        {false, L'E', 60, 0.0},
        {false, L'F', 90, 0.0},
    }};
    bool running = false;
    int burstRemaining = 0;
    double lastFireMs = 0.0;
};

struct HitArea {
    RectF rect;
    std::function<void()> action;
};

struct Translation {
    const wchar_t* key;
    const wchar_t* values[6];
};

constexpr Translation kTranslations[] = {
    {L"app_title", {L"OGX Auto Clicker", L"OGX Auto Clicker", L"OGX Auto Clicker", L"OGX Auto Clicker", L"OGX Auto Clicker", L"OGX Auto Clicker"}},
    {L"mouse", {L"Mouse", L"Mouse", L"Мышь", L"Maus", L"Souris", L"Mouse"}},
    {L"keyboard", {L"Keyboard", L"Klavye", L"Клавиатура", L"Tastatur", L"Clavier", L"Teclado"}},
    {L"settings", {L"Settings", L"Ayarlar", L"Настройки", L"Einstellungen", L"Paramètres", L"Ajustes"}},
    {L"mouse_title", {L"Mouse automation", L"Mouse otomasyonu", L"Автоматизация мыши", L"Maus-Automatisierung", L"Automatisation souris", L"Automatización del mouse"}},
    {L"keyboard_title", {L"Keyboard automation", L"Klavye otomasyonu", L"Автоматизация клавиатуры", L"Tastatur-Automatisierung", L"Automatisation clavier", L"Automatización del teclado"}},
    {L"settings_title", {L"Settings", L"Ayarlar", L"Настройки", L"Einstellungen", L"Paramètres", L"Ajustes"}},
    {L"left_click", {L"Left click", L"Sol tık", L"Левый клик", L"Linksklick", L"Clic gauche", L"Clic izquierdo"}},
    {L"right_click", {L"Right click", L"Sağ tık", L"Правый клик", L"Rechtsklick", L"Clic droit", L"Clic derecho"}},
    {L"selected", {L"Selected", L"Seçili", L"Выбрано", L"Ausgewählt", L"Sélectionné", L"Seleccionado"}},
    {L"keybind", {L"Keybind", L"Tuş atama", L"Клавиша", L"Tastenbindung", L"Raccourci", L"Atajo"}},
    {L"waiting_key", {L"Press a key...", L"Bir tuşa bas...", L"Нажмите клавишу...", L"Taste drücken...", L"Appuyez sur une touche...", L"Pulsa una tecla..."}},
    {L"single_click", {L"Single click", L"Tek tık", L"Один клик", L"Einzelklick", L"Clic simple", L"Clic simple"}},
    {L"double_click", {L"Double click", L"Çift tık", L"Двойной клик", L"Doppelklick", L"Double clic", L"Doble clic"}},
    {L"single_tap", {L"Single tap", L"Tek basış", L"Одно нажатие", L"Einzeldruck", L"Appui simple", L"Pulsación simple"}},
    {L"double_tap", {L"Double tap", L"Çift basış", L"Двойное нажатие", L"Doppeldruck", L"Double appui", L"Doble pulsación"}},
    {L"cps", {L"Clicks / second", L"Saniyedeki tık", L"Кликов / сек", L"Klicks / Sek.", L"Clics / seconde", L"Clics / segundo"}},
    {L"cps_label", {L"CPS", L"CPS", L"CPS", L"CPS", L"CPS", L"CPS"}},
    {L"fixed_cps", {L"Fixed CPS", L"Sabit CPS", L"Фикс. CPS", L"Festes CPS", L"CPS fixe", L"CPS fijo"}},
    {L"random_cps", {L"Random CPS", L"Random CPS", L"Случ. CPS", L"Zufalls-CPS", L"CPS aléatoire", L"CPS aleatorio"}},
    {L"min_cps", {L"Min CPS", L"Min CPS", L"Мин. CPS", L"Min. CPS", L"CPS min", L"CPS mín"}},
    {L"max_cps", {L"Max CPS", L"Max CPS", L"Макс. CPS", L"Max. CPS", L"CPS max", L"CPS máx"}},
    {L"rate", {L"Presses / second", L"Saniyedeki basış", L"Нажатий / сек", L"Drücke / Sek.", L"Appuis / seconde", L"Pulsaciones / segundo"}},
    {L"mode", {L"Trigger mode", L"Tetik modu", L"Режим запуска", L"Auslösemodus", L"Mode déclencheur", L"Modo de activación"}},
    {L"toggle", {L"Toggle", L"Aç/Kapat", L"Переключатель", L"Umschalten", L"Basculer", L"Alternar"}},
    {L"hold", {L"Hold", L"Basılı tut", L"Удержание", L"Halten", L"Maintenir", L"Mantener"}},
    {L"burst", {L"Burst", L"Seri", L"Серия", L"Serie", L"Rafale", L"Ráfaga"}},
    {L"backend", {L"Click method", L"Tıklama metodu", L"Метод клика", L"Klickmethode", L"Méthode de clic", L"Método de clic"}},
    {L"sendinput", {L"SendInput", L"SendInput", L"SendInput", L"SendInput", L"SendInput", L"SendInput"}},
    {L"standard", {L"Standard", L"Standart", L"Стандарт", L"Standard", L"Standard", L"Estándar"}},
    {L"game_mode", {L"Game", L"Oyun", L"Игра", L"Spiel", L"Jeu", L"Juego"}},
    {L"key_method", {L"Key method", L"Tuş metodu", L"Метод клавиш", L"Tastenmethode", L"Méthode touche", L"Método de tecla"}},
    {L"legacy", {L"Legacy", L"Eski", L"Legacy", L"Legacy", L"Ancien", L"Clásico"}},
    {L"timed_settings", {L"Timed settings", L"Zamanlı ayarlar", L"Настройки таймера", L"Zeitsteuerung", L"Réglages minutés", L"Ajustes temporizados"}},
    {L"task", {L"Task", L"Görev", L"Задача", L"Aufgabe", L"Tâche", L"Tarea"}},
    {L"interval", {L"Interval", L"Aralık", L"Интервал", L"Intervall", L"Intervalle", L"Intervalo"}},
    {L"seconds_short", {L"sec", L"sn", L"сек", L"s", L"s", L"s"}},
    {L"hours_short", {L"h", L"sa", L"ч", L"h", L"h", L"h"}},
    {L"minutes_short", {L"m", L"dk", L"м", L"m", L"m", L"m"}},
    {L"on", {L"On", L"Açık", L"Вкл", L"Ein", L"Oui", L"Sí"}},
    {L"off", {L"Off", L"Kapalı", L"Выкл", L"Aus", L"Non", L"No"}},
    {L"type_number", {L"Type number...", L"Sayı yaz...", L"Введите число...", L"Zahl eingeben...", L"Saisir un nombre...", L"Escribe número..."}},
    {L"configs", {L"Configs", L"Configler", L"Конфиги", L"Configs", L"Configs", L"Configs"}},
    {L"current_config", {L"Current config", L"Aktif config", L"Текущий конфиг", L"Aktive Config", L"Config active", L"Config actual"}},
    {L"create_config", {L"Create config", L"Config oluştur", L"Создать конфиг", L"Config erstellen", L"Créer config", L"Crear config"}},
    {L"update_config", {L"Update config", L"Config güncelle", L"Обновить конфиг", L"Config aktualisieren", L"Mettre à jour", L"Actualizar config"}},
    {L"delete_config", {L"Delete config", L"Config sil", L"Удалить конфиг", L"Config löschen", L"Supprimer config", L"Eliminar config"}},
    {L"auto_save", {L"Auto-save changes", L"Değişiklikleri otomatik kaydet", L"Автосохранение", L"Automatisch speichern", L"Sauvegarde auto", L"Guardado automático"}},
    {L"config_created", {L"Config created", L"Config oluşturuldu", L"Конфиг создан", L"Config erstellt", L"Config créée", L"Config creada"}},
    {L"config_updated", {L"Config updated", L"Config güncellendi", L"Конфиг обновлён", L"Config aktualisiert", L"Config mise à jour", L"Config actualizada"}},
    {L"config_deleted", {L"Config deleted", L"Config silindi", L"Конфиг удалён", L"Config gelöscht", L"Config supprimée", L"Config eliminada"}},
    {L"running", {L"Running", L"Çalışıyor", L"Работает", L"Läuft", L"Actif", L"Activo"}},
    {L"stopped", {L"Stopped", L"Durdu", L"Остановлено", L"Gestoppt", L"Arrêté", L"Detenido"}},
    {L"start", {L"Start", L"Başlat", L"Старт", L"Start", L"Démarrer", L"Iniciar"}},
    {L"stop", {L"Stop", L"Durdur", L"Стоп", L"Stopp", L"Arrêter", L"Detener"}},
    {L"target_key", {L"Target key", L"Hedef tuş", L"Целевая клавиша", L"Zieltaste", L"Touche cible", L"Tecla objetivo"}},
    {L"main_key_mode", {L"Run mode", L"Çalışma modu", L"Режим работы", L"Arbeitsmodus", L"Mode d'exécution", L"Modo de ejecución"}},
    {L"main_key", {L"Main key", L"Ana tuş", L"Основная клавиша", L"Haupttaste", L"Touche principale", L"Tecla principal"}},
    {L"timed_only", {L"Timed only", L"Sadece zamanlı", L"Только таймер", L"Nur zeitgesteuert", L"Minuté seulement", L"Solo temporizado"}},
    {L"hotkey", {L"Hotkey", L"Kısayol", L"Горячая клавиша", L"Hotkey", L"Raccourci", L"Tecla rápida"}},
    {L"language", {L"Language", L"Dil", L"Язык", L"Sprache", L"Langue", L"Idioma"}},
    {L"english", {L"English", L"İngilizce", L"Английский", L"Englisch", L"Anglais", L"Inglés"}},
    {L"turkish", {L"Turkish", L"Türkçe", L"Турецкий", L"Türkisch", L"Turc", L"Turco"}},
    {L"russian", {L"Russian", L"Rusça", L"Русский", L"Russisch", L"Russe", L"Ruso"}},
    {L"german", {L"German", L"Almanca", L"Немецкий", L"Deutsch", L"Allemand", L"Alemán"}},
    {L"french", {L"French", L"Fransızca", L"Французский", L"Französisch", L"Français", L"Francés"}},
    {L"spanish", {L"Spanish", L"İspanyolca", L"Испанский", L"Spanisch", L"Espagnol", L"Español"}},
    {L"always_top", {L"Always on top", L"Her zaman üstte", L"Поверх окон", L"Immer im Vordergrund", L"Toujours au-dessus", L"Siempre encima"}},
    {L"reset", {L"Reset defaults", L"Varsayılanlara dön", L"Сбросить", L"Zurücksetzen", L"Réinitialiser", L"Restablecer"}},
    {L"accent", {L"Purple / black", L"Mor / siyah", L"Фиолетовый / черный", L"Lila / schwarz", L"Violet / noir", L"Morado / negro"}},
    {L"status_ready", {L"Ready", L"Hazır", L"Готово", L"Bereit", L"Prêt", L"Listo"}},
    {L"status_bound", {L"Keybind saved", L"Tuş ataması kaydedildi", L"Клавиша сохранена", L"Taste gespeichert", L"Raccourci enregistré", L"Atajo guardado"}},
    {L"status_cancel", {L"Binding canceled", L"Atama iptal edildi", L"Назначение отменено", L"Bindung abgebrochen", L"Attribution annulée", L"Asignación cancelada"}},
    {L"hover_mouse", {L"Hover a mouse button", L"Mouse tuşunun üstüne gel", L"Наведите на кнопку мыши", L"Maustaste berühren", L"Survolez un bouton", L"Pasa sobre un botón"}},
    {L"choose_language", {L"Choose app language", L"Uygulama dilini seç", L"Выберите язык", L"App-Sprache wählen", L"Choisir la langue", L"Elige idioma"}},
    {L"theme", {L"Theme", L"Tema", L"Тема", L"Design", L"Thème", L"Tema"}},
};

Color Rgb(BYTE r, BYTE g, BYTE b, BYTE a = 255) {
    return Color(a, r, g, b);
}

bool Contains(const RectF& rect, float x, float y) {
    return x >= rect.X && x <= rect.X + rect.Width && y >= rect.Y && y <= rect.Y + rect.Height;
}

void BuildRoundRect(GraphicsPath& path, const RectF& rect, float radius) {
    path.Reset();
    const float d = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRound(Graphics& g, const RectF& rect, float radius, const Color& color) {
    GraphicsPath path;
    BuildRoundRect(path, rect, radius);
    SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

void FillRound(Graphics& g, const RectF& rect, float radius, Brush& brush) {
    GraphicsPath path;
    BuildRoundRect(path, rect, radius);
    g.FillPath(&brush, &path);
}

void StrokeRound(Graphics& g, const RectF& rect, float radius, const Color& color, float width = 1.0f) {
    GraphicsPath path;
    BuildRoundRect(path, rect, radius);
    Pen pen(color, width);
    g.DrawPath(&pen, &path);
}

std::unique_ptr<Image> LoadPngFromResource(HINSTANCE instance, int resourceId) {
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) {
        return nullptr;
    }

    HGLOBAL loaded = LoadResource(instance, resource);
    if (!loaded) {
        return nullptr;
    }

    const DWORD size = SizeofResource(instance, resource);
    const void* data = LockResource(loaded);
    if (!data || size == 0) {
        return nullptr;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) {
        return nullptr;
    }

    void* buffer = GlobalLock(memory);
    if (!buffer) {
        GlobalFree(memory);
        return nullptr;
    }
    CopyMemory(buffer, data, size);
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return nullptr;
    }

    std::unique_ptr<Image> image(Image::FromStream(stream));
    stream->Release();
    if (!image || image->GetLastStatus() != Ok) {
        return nullptr;
    }
    return image;
}

void DrawText(Graphics& g,
              const std::wstring& text,
              const RectF& rect,
              float size,
              const Color& color,
              INT style = FontStyleRegular,
              StringAlignment align = StringAlignmentNear,
              StringAlignment line = StringAlignmentCenter) {
    Font font(L"Segoe UI", size, style, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(line);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsLineLimit);
    g.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

double NowMs() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    const auto elapsed = clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

int ClampRate(int value) {
    return std::max(1, std::min(300, value));
}

int ClampSeconds(int value) {
    return std::max(1, std::min(86400, value));
}

int ClampCps(int value) {
    return std::max(1, std::min(300, value));
}

int ClampProfileInt(UINT value, int low, int high) {
    return std::max(low, std::min(high, static_cast<int>(value)));
}

std::wstring IntToString(int value) {
    return std::to_wstring(value);
}

std::wstring KeyName(UINT vk) {
    if (vk >= L'A' && vk <= L'Z') {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= L'0' && vk <= L'9') {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return L"F" + std::to_wstring(vk - VK_F1 + 1);
    }

    switch (vk) {
    case VK_LBUTTON: return L"Mouse Left";
    case VK_RBUTTON: return L"Mouse Right";
    case VK_MBUTTON: return L"Mouse Middle";
    case VK_XBUTTON1: return L"Mouse X1";
    case VK_XBUTTON2: return L"Mouse X2";
    case VK_SPACE: return L"Space";
    case VK_TAB: return L"Tab";
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"Esc";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"Page Up";
    case VK_NEXT: return L"Page Down";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_SHIFT: return L"Shift";
    case VK_CONTROL: return L"Ctrl";
    case VK_MENU: return L"Alt";
    case VK_CAPITAL: return L"Caps";
    case VK_OEM_1: return L";";
    case VK_OEM_PLUS: return L"+";
    case VK_OEM_COMMA: return L",";
    case VK_OEM_MINUS: return L"-";
    case VK_OEM_PERIOD: return L".";
    case VK_OEM_2: return L"/";
    case VK_OEM_3: return L"`";
    case VK_OEM_4: return L"[";
    case VK_OEM_5: return L"\\";
    case VK_OEM_6: return L"]";
    case VK_OEM_7: return L"'";
    default:
        break;
    }

    wchar_t name[64] = {};
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lparam = static_cast<LONG>(scan << 16);
    if (GetKeyNameTextW(lparam, name, 64) > 0) {
        return name;
    }
    return L"VK " + std::to_wstring(vk);
}

std::wstring AppDataFolder() {
    PWSTR localAppData = nullptr;
    std::wstring result = L".";

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        result = std::wstring(localAppData) + L"\\OGX Auto Clicker";
        CreateDirectoryW(result.c_str(), nullptr);
        CoTaskMemFree(localAppData);
    }

    return result;
}

std::wstring ConfigPath() {
    return AppDataFolder() + L"\\settings.ini";
}

std::wstring ConfigFolderPath() {
    const std::wstring folder = AppDataFolder() + L"\\configs";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

std::wstring SanitizeConfigName(const std::wstring& name) {
    std::wstring result;
    for (wchar_t ch : name) {
        const bool valid = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L' ' || ch == L'_' || ch == L'-';
        result.push_back(valid ? ch : L'_');
    }
    if (result.empty()) {
        result = L"Default";
    }
    return result.substr(0, 48);
}

std::wstring ProfilePath(const std::wstring& name) {
    return ConfigFolderPath() + L"\\" + SanitizeConfigName(name) + L".ini";
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

class App {
public:
    int Run(HINSTANCE instance, int) {
        instance_ = instance;
        LoadConfig();

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_OGX_APP));
        wc.hIconSm = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_OGX_APP));
        wc.lpszClassName = kWindowClass;
        wc.hbrBackground = nullptr;

        if (!RegisterClassExW(&wc)) {
            return 1;
        }

        const DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT windowRect = {0, 0, kClientWidth, kClientHeight};
        AdjustWindowRectEx(&windowRect, windowStyle, FALSE, 0);
        const int windowWidth = windowRect.right - windowRect.left;
        const int windowHeight = windowRect.bottom - windowRect.top;

        RECT workArea = {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        const int workWidth = static_cast<int>(workArea.right - workArea.left);
        const int workHeight = static_cast<int>(workArea.bottom - workArea.top);
        const int x = static_cast<int>(workArea.left) + std::max(0, (workWidth - windowWidth) / 2);
        const int y = static_cast<int>(workArea.top) + std::max(0, (workHeight - windowHeight) / 2);

        hwnd_ = CreateWindowExW(
            0,
            kWindowClass,
            T(L"app_title").c_str(),
            windowStyle,
            x,
            y,
            windowWidth,
            windowHeight,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_) {
            return 1;
        }

        logoImage_ = LoadPngFromResource(instance_, IDR_OGX_LOGO_PNG);
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(wc.hIcon));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(wc.hIconSm));
        ApplyWindowOptions();
        SetTimer(hwnd_, kAutomationTimer, kTimerMs, nullptr);

        g_app = this;
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);

        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);

        MSG msg = {};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (g_keyboardHook) {
            UnhookWindowsHookEx(g_keyboardHook);
            g_keyboardHook = nullptr;
        }
        if (g_mouseHook) {
            UnhookWindowsHookEx(g_mouseHook);
            g_mouseHook = nullptr;
        }
        SaveConfig();
        return static_cast<int>(msg.wParam);
    }

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_NCCREATE:
            hwnd_ = hwnd;
            return TRUE;
        case WM_CREATE:
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            RECT windowRect = {0, 0, kClientWidth, kClientHeight};
            AdjustWindowRectEx(&windowRect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
            const LONG width = windowRect.right - windowRect.left;
            const LONG height = windowRect.bottom - windowRect.top;
            info->ptMinTrackSize.x = width;
            info->ptMinTrackSize.y = height;
            info->ptMaxTrackSize.x = width;
            info->ptMaxTrackSize.y = height;
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSELEAVE:
            mouseInside_ = false;
            hoverX_ = -1;
            hoverY_ = -1;
            hoverHitIndex_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (numberTarget_ != NumberTarget::None) {
                HandleNumberKey(static_cast<UINT>(wParam));
                return 0;
            }
            if (captureTarget_ != BindTarget::None) {
                AssignCapturedKey(static_cast<UINT>(wParam));
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == kAutomationTimer) {
                TickAutomation();
                return 0;
            }
            break;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kAutomationTimer);
            StopAllAutomation();
            SaveConfig();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool HandleGlobalKey(UINT vk, bool isDown, bool isUp) {
        if (vk >= keyDown_.size()) {
            return false;
        }

        if (isDown) {
            if (numberTarget_ != NumberTarget::None) {
                HandleNumberKey(vk);
                return true;
            }

            if (captureTarget_ != BindTarget::None) {
                AssignCapturedKey(vk);
                keyDown_[vk] = false;
                return true;
            }

            const bool repeated = keyDown_[vk];
            keyDown_[vk] = true;

            if (!repeated) {
                OnHotkeyDown(vk);
            }
            return false;
        }

        if (isUp) {
            if (numberTarget_ != NumberTarget::None) {
                return true;
            }
            keyDown_[vk] = false;
            OnHotkeyUp(vk);
        }
        return false;
    }

    bool HandleGlobalMouseButton(UINT vk, bool isDown, bool isUp, POINT screenPoint) {
        if (vk >= keyDown_.size()) {
            return false;
        }

        const bool insideApp = IsPointInsideApp(screenPoint);
        if (insideApp && captureTarget_ == BindTarget::None) {
            if (isUp && keyDown_[vk]) {
                return HandleGlobalKey(vk, isDown, isUp);
            }
            return false;
        }

        return HandleGlobalKey(vk, isDown, isUp);
    }

private:
    friend LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    std::wstring T(const wchar_t* key) const {
        const int index = static_cast<int>(language_);
        for (const auto& translation : kTranslations) {
            if (std::wcscmp(translation.key, key) == 0) {
                return translation.values[index];
            }
        }
        return key;
    }

    bool IsPointInsideApp(POINT screenPoint) const {
        if (!hwnd_) {
            return false;
        }

        RECT windowRect = {};
        if (!GetWindowRect(hwnd_, &windowRect)) {
            return false;
        }
        return PtInRect(&windowRect, screenPoint) != FALSE;
    }

    void LoadConfig() {
        const std::wstring appPath = ConfigPath();
        wchar_t activeName[96] = {};
        GetPrivateProfileStringW(L"App", L"CurrentConfig", L"Default", activeName, 96, appPath.c_str());
        currentConfig_ = SanitizeConfigName(activeName);

        language_ = static_cast<Language>(ClampProfileInt(GetPrivateProfileIntW(L"Settings", L"Language", 0, appPath.c_str()), 0, 5));
        alwaysOnTop_ = GetPrivateProfileIntW(L"Settings", L"AlwaysOnTop", 0, appPath.c_str()) != 0;
        autoSave_ = GetPrivateProfileIntW(L"App", L"AutoSave", 1, appPath.c_str()) != 0;

        std::wstring path = ProfilePath(currentConfig_);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            path = appPath;
        }

        leftClick_.hotkey = GetPrivateProfileIntW(L"MouseLeft", L"Hotkey", VK_F6, path.c_str());
        leftClick_.doubleClick = GetPrivateProfileIntW(L"MouseLeft", L"DoubleClick", 0, path.c_str()) != 0;
        leftClick_.cps = ClampCps(GetPrivateProfileIntW(L"MouseLeft", L"Cps", 10, path.c_str()));
        leftClick_.randomCps = GetPrivateProfileIntW(L"MouseLeft", L"RandomCps", 0, path.c_str()) != 0;
        leftClick_.randomMinCps = ClampCps(GetPrivateProfileIntW(L"MouseLeft", L"RandomMinCps", 20, path.c_str()));
        leftClick_.randomMaxCps = ClampCps(GetPrivateProfileIntW(L"MouseLeft", L"RandomMaxCps", 30, path.c_str()));
        NormalizeRandomCps(leftClick_);
        leftClick_.mode = static_cast<TriggerMode>(ClampProfileInt(GetPrivateProfileIntW(L"MouseLeft", L"Mode", 0, path.c_str()), 0, 2));
        leftClick_.backend = static_cast<InputBackend>(ClampProfileInt(GetPrivateProfileIntW(L"MouseLeft", L"Backend", 1, path.c_str()), 0, 2));

        rightClick_.hotkey = GetPrivateProfileIntW(L"MouseRight", L"Hotkey", VK_F7, path.c_str());
        rightClick_.doubleClick = GetPrivateProfileIntW(L"MouseRight", L"DoubleClick", 0, path.c_str()) != 0;
        rightClick_.cps = ClampCps(GetPrivateProfileIntW(L"MouseRight", L"Cps", 10, path.c_str()));
        rightClick_.randomCps = GetPrivateProfileIntW(L"MouseRight", L"RandomCps", 0, path.c_str()) != 0;
        rightClick_.randomMinCps = ClampCps(GetPrivateProfileIntW(L"MouseRight", L"RandomMinCps", 20, path.c_str()));
        rightClick_.randomMaxCps = ClampCps(GetPrivateProfileIntW(L"MouseRight", L"RandomMaxCps", 30, path.c_str()));
        NormalizeRandomCps(rightClick_);
        rightClick_.mode = static_cast<TriggerMode>(ClampProfileInt(GetPrivateProfileIntW(L"MouseRight", L"Mode", 0, path.c_str()), 0, 2));
        rightClick_.backend = static_cast<InputBackend>(ClampProfileInt(GetPrivateProfileIntW(L"MouseRight", L"Backend", 1, path.c_str()), 0, 2));

        keyboard_.targetKey = GetPrivateProfileIntW(L"Keyboard", L"TargetKey", VK_SPACE, path.c_str());
        keyboard_.hotkey = GetPrivateProfileIntW(L"Keyboard", L"Hotkey", VK_F8, path.c_str());
        keyboard_.mainKeyEnabled = GetPrivateProfileIntW(L"Keyboard", L"MainKeyEnabled", 1, path.c_str()) != 0;
        keyboard_.doubleTap = GetPrivateProfileIntW(L"Keyboard", L"DoubleTap", 0, path.c_str()) != 0;
        keyboard_.rate = ClampRate(GetPrivateProfileIntW(L"Keyboard", L"Rate", 8, path.c_str()));
        keyboard_.mode = static_cast<TriggerMode>(ClampProfileInt(GetPrivateProfileIntW(L"Keyboard", L"Mode", 0, path.c_str()), 0, 2));
        keyboard_.backend = static_cast<InputBackend>(ClampProfileInt(GetPrivateProfileIntW(L"Keyboard", L"Backend", 1, path.c_str()), 0, 2));

        const UINT defaultKeys[kTimedKeySlots] = {L'X', L'1', L'E', L'F'};
        const int defaultIntervals[kTimedKeySlots] = {40, 50, 60, 90};
        for (int i = 0; i < kTimedKeySlots; ++i) {
            const std::wstring section = L"KeyboardTask" + std::to_wstring(i);
            keyboard_.tasks[i].enabled = GetPrivateProfileIntW(section.c_str(), L"Enabled", 0, path.c_str()) != 0;
            keyboard_.tasks[i].key = GetPrivateProfileIntW(section.c_str(), L"Key", defaultKeys[i], path.c_str());
            keyboard_.tasks[i].intervalSec = ClampSeconds(GetPrivateProfileIntW(section.c_str(), L"IntervalSec", defaultIntervals[i], path.c_str()));
            keyboard_.tasks[i].lastFireMs = NowMs();
        }
    }

    void SaveConfig(bool forceProfile = false) const {
        const std::wstring appPath = ConfigPath();
        auto writeIntTo = [](const std::wstring& path, const wchar_t* section, const wchar_t* name, int value) {
            const std::wstring text = std::to_wstring(value);
            WritePrivateProfileStringW(section, name, text.c_str(), path.c_str());
        };
        auto writeStringTo = [](const std::wstring& path, const wchar_t* section, const wchar_t* name, const std::wstring& value) {
            WritePrivateProfileStringW(section, name, value.c_str(), path.c_str());
        };

        writeIntTo(appPath, L"Settings", L"Language", static_cast<int>(language_));
        writeIntTo(appPath, L"Settings", L"AlwaysOnTop", alwaysOnTop_ ? 1 : 0);
        writeIntTo(appPath, L"App", L"AutoSave", autoSave_ ? 1 : 0);
        writeStringTo(appPath, L"App", L"CurrentConfig", currentConfig_);

        if (!forceProfile && !autoSave_) {
            return;
        }

        const std::wstring path = ProfilePath(currentConfig_);
        auto writeInt = [&writeIntTo, &path](const wchar_t* section, const wchar_t* name, int value) {
            writeIntTo(path, section, name, value);
        };

        writeInt(L"MouseLeft", L"Hotkey", static_cast<int>(leftClick_.hotkey));
        writeInt(L"MouseLeft", L"DoubleClick", leftClick_.doubleClick ? 1 : 0);
        writeInt(L"MouseLeft", L"Cps", leftClick_.cps);
        writeInt(L"MouseLeft", L"RandomCps", leftClick_.randomCps ? 1 : 0);
        writeInt(L"MouseLeft", L"RandomMinCps", leftClick_.randomMinCps);
        writeInt(L"MouseLeft", L"RandomMaxCps", leftClick_.randomMaxCps);
        writeInt(L"MouseLeft", L"Mode", static_cast<int>(leftClick_.mode));
        writeInt(L"MouseLeft", L"Backend", static_cast<int>(leftClick_.backend));

        writeInt(L"MouseRight", L"Hotkey", static_cast<int>(rightClick_.hotkey));
        writeInt(L"MouseRight", L"DoubleClick", rightClick_.doubleClick ? 1 : 0);
        writeInt(L"MouseRight", L"Cps", rightClick_.cps);
        writeInt(L"MouseRight", L"RandomCps", rightClick_.randomCps ? 1 : 0);
        writeInt(L"MouseRight", L"RandomMinCps", rightClick_.randomMinCps);
        writeInt(L"MouseRight", L"RandomMaxCps", rightClick_.randomMaxCps);
        writeInt(L"MouseRight", L"Mode", static_cast<int>(rightClick_.mode));
        writeInt(L"MouseRight", L"Backend", static_cast<int>(rightClick_.backend));

        writeInt(L"Keyboard", L"TargetKey", static_cast<int>(keyboard_.targetKey));
        writeInt(L"Keyboard", L"Hotkey", static_cast<int>(keyboard_.hotkey));
        writeInt(L"Keyboard", L"MainKeyEnabled", keyboard_.mainKeyEnabled ? 1 : 0);
        writeInt(L"Keyboard", L"DoubleTap", keyboard_.doubleTap ? 1 : 0);
        writeInt(L"Keyboard", L"Rate", keyboard_.rate);
        writeInt(L"Keyboard", L"Mode", static_cast<int>(keyboard_.mode));
        writeInt(L"Keyboard", L"Backend", static_cast<int>(keyboard_.backend));

        for (int i = 0; i < kTimedKeySlots; ++i) {
            const std::wstring section = L"KeyboardTask" + std::to_wstring(i);
            writeInt(section.c_str(), L"Enabled", keyboard_.tasks[i].enabled ? 1 : 0);
            writeInt(section.c_str(), L"Key", static_cast<int>(keyboard_.tasks[i].key));
            writeInt(section.c_str(), L"IntervalSec", keyboard_.tasks[i].intervalSec);
        }
    }

    void WriteAppState() const {
        const std::wstring appPath = ConfigPath();
        auto writeInt = [&appPath](const wchar_t* section, const wchar_t* name, int value) {
            const std::wstring text = std::to_wstring(value);
            WritePrivateProfileStringW(section, name, text.c_str(), appPath.c_str());
        };
        writeInt(L"Settings", L"Language", static_cast<int>(language_));
        writeInt(L"Settings", L"AlwaysOnTop", alwaysOnTop_ ? 1 : 0);
        writeInt(L"App", L"AutoSave", autoSave_ ? 1 : 0);
        WritePrivateProfileStringW(L"App", L"CurrentConfig", currentConfig_.c_str(), appPath.c_str());
    }

    std::vector<std::wstring> ConfigNames() const {
        std::vector<std::wstring> names;
        names.push_back(L"Default");

        const std::wstring search = ConfigFolderPath() + L"\\*.ini";
        WIN32_FIND_DATAW data = {};
        HANDLE handle = FindFirstFileW(search.c_str(), &data);
        if (handle != INVALID_HANDLE_VALUE) {
            do {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    std::wstring name = data.cFileName;
                    const size_t dot = name.rfind(L".ini");
                    if (dot != std::wstring::npos) {
                        name = name.substr(0, dot);
                    }
                    name = SanitizeConfigName(name);
                    if (std::find(names.begin(), names.end(), name) == names.end()) {
                        names.push_back(name);
                    }
                }
            } while (FindNextFileW(handle, &data));
            FindClose(handle);
        }
        return names;
    }

    void SelectConfig(const std::wstring& name) {
        currentConfig_ = SanitizeConfigName(name);
        WriteAppState();
        LoadConfig();
        ApplyWindowOptions();
        SetStatus(currentConfig_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void CycleConfig(int direction) {
        const std::vector<std::wstring> names = ConfigNames();
        if (names.empty()) {
            return;
        }

        auto it = std::find(names.begin(), names.end(), currentConfig_);
        int index = it == names.end() ? 0 : static_cast<int>(std::distance(names.begin(), it));
        index = (index + direction + static_cast<int>(names.size())) % static_cast<int>(names.size());
        SelectConfig(names[index]);
    }

    void CreateConfig() {
        const std::vector<std::wstring> names = ConfigNames();
        int next = 1;
        std::wstring name;
        do {
            name = L"Config " + std::to_wstring(next++);
        } while (std::find(names.begin(), names.end(), name) != names.end());

        currentConfig_ = name;
        SaveConfig(true);
        SetStatus(T(L"config_created"));
    }

    void UpdateConfig() {
        SaveConfig(true);
        SetStatus(T(L"config_updated"));
    }

    void DeleteConfig() {
        if (currentConfig_ != L"Default") {
            DeleteFileW(ProfilePath(currentConfig_).c_str());
            currentConfig_ = L"Default";
            WriteAppState();
            LoadConfig();
            SetStatus(T(L"config_deleted"));
        } else {
            ResetDefaults();
            SaveConfig(true);
            SetStatus(T(L"config_updated"));
        }
    }

    void ApplyWindowOptions() {
        if (!hwnd_) {
            return;
        }
        SetWindowTextW(hwnd_, T(L"app_title").c_str());
        SetWindowPos(
            hwnd_,
            alwaysOnTop_ ? HWND_TOPMOST : HWND_NOTOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void StopAllAutomation() {
        leftClick_.running = false;
        rightClick_.running = false;
        keyboard_.running = false;
        leftClick_.burstRemaining = 0;
        rightClick_.burstRemaining = 0;
        keyboard_.burstRemaining = 0;
    }

    void OnMouseMove(int x, int y) {
        hoverX_ = static_cast<float>(x);
        hoverY_ = static_cast<float>(y);

        if (!mouseInside_) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd_;
            TrackMouseEvent(&tme);
            mouseInside_ = true;
        }

        const MouseButton old = activeMouseButton_;
        const bool leftHover = PtInRect(&leftButtonZone_, POINT{x, y}) != FALSE;
        const bool rightHover = PtInRect(&rightButtonZone_, POINT{x, y}) != FALSE;
        if (leftHover) {
            activeMouseButton_ = MouseButton::Left;
        } else if (rightHover) {
            activeMouseButton_ = MouseButton::Right;
        }

        bool hot = leftHover || rightHover;
        int hoverIndex = -1;
        if (!hot) {
            for (size_t i = 0; i < hitAreas_.size(); ++i) {
                const auto& area = hitAreas_[i];
                if (Contains(area.rect, hoverX_, hoverY_)) {
                    hot = true;
                    hoverIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        SetCursor(LoadCursor(nullptr, hot ? IDC_HAND : IDC_ARROW));

        const bool hoverChanged = old != activeMouseButton_ ||
                                  leftHover != leftMouseHover_ ||
                                  rightHover != rightMouseHover_ ||
                                  hoverIndex != hoverHitIndex_;
        leftMouseHover_ = leftHover;
        rightMouseHover_ = rightHover;
        hoverHitIndex_ = hoverIndex;
        if (hoverChanged) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void OnClick(int x, int y) {
        if (numberTarget_ != NumberTarget::None) {
            CommitNumberEdit();
        }

        for (auto it = hitAreas_.rbegin(); it != hitAreas_.rend(); ++it) {
            if (Contains(it->rect, static_cast<float>(x), static_cast<float>(y))) {
                it->action();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
        if (PtInRect(&leftButtonZone_, POINT{x, y})) {
            activeMouseButton_ = MouseButton::Left;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (PtInRect(&rightButtonZone_, POINT{x, y})) {
            activeMouseButton_ = MouseButton::Right;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void OnHotkeyDown(UINT vk) {
        bool changed = false;
        if (leftClick_.hotkey == vk) {
            TriggerMouseDown(leftClick_);
            changed = true;
        }
        if (rightClick_.hotkey == vk) {
            TriggerMouseDown(rightClick_);
            changed = true;
        }
        if (keyboard_.hotkey == vk) {
            TriggerKeyboardDown();
            changed = true;
        }
        if (changed) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void OnHotkeyUp(UINT vk) {
        bool changed = false;
        if (leftClick_.hotkey == vk && leftClick_.mode == TriggerMode::Hold) {
            leftClick_.running = false;
            changed = true;
        }
        if (rightClick_.hotkey == vk && rightClick_.mode == TriggerMode::Hold) {
            rightClick_.running = false;
            changed = true;
        }
        if (keyboard_.hotkey == vk && keyboard_.mode == TriggerMode::Hold) {
            keyboard_.running = false;
            changed = true;
        }
        if (changed) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void TriggerMouseDown(MouseClickConfig& config) {
        switch (config.mode) {
        case TriggerMode::Toggle:
            config.running = !config.running;
            config.burstRemaining = 0;
            config.lastFireMs = 0.0;
            config.nextIntervalMs = 0.0;
            break;
        case TriggerMode::Hold:
            config.running = true;
            config.burstRemaining = 0;
            config.lastFireMs = 0.0;
            config.nextIntervalMs = 0.0;
            break;
        case TriggerMode::Burst:
            config.running = true;
            config.burstRemaining = kBurstCycles;
            config.lastFireMs = 0.0;
            config.nextIntervalMs = 0.0;
            break;
        }
    }

    void TriggerKeyboardDown() {
        switch (keyboard_.mode) {
        case TriggerMode::Toggle:
            if (keyboard_.running) {
                StopKeyboardAutomation();
            } else {
                StartKeyboardAutomation(false);
            }
            break;
        case TriggerMode::Hold:
            StartKeyboardAutomation(false);
            break;
        case TriggerMode::Burst:
            StartKeyboardAutomation(true);
            break;
        }
    }

    void StartKeyboardAutomation(bool burst) {
        keyboard_.running = true;
        keyboard_.burstRemaining = (burst && keyboard_.mainKeyEnabled) ? kBurstCycles : 0;
        keyboard_.lastFireMs = 0.0;
        const double now = NowMs();
        for (auto& task : keyboard_.tasks) {
            task.lastFireMs = now;
        }
    }

    void StopKeyboardAutomation() {
        keyboard_.running = false;
        keyboard_.burstRemaining = 0;
        keyboard_.lastFireMs = 0.0;
    }

    void AssignCapturedKey(UINT vk) {
        if (vk == VK_ESCAPE) {
            captureTarget_ = BindTarget::None;
            SetStatus(T(L"status_cancel"));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        switch (captureTarget_) {
        case BindTarget::MouseLeftHotkey:
            leftClick_.hotkey = vk;
            break;
        case BindTarget::MouseRightHotkey:
            rightClick_.hotkey = vk;
            break;
        case BindTarget::KeyboardTargetKey:
            keyboard_.targetKey = vk;
            break;
        case BindTarget::KeyboardHotkey:
            keyboard_.hotkey = vk;
            break;
        case BindTarget::KeyboardTask0Key:
        case BindTarget::KeyboardTask1Key:
        case BindTarget::KeyboardTask2Key:
        case BindTarget::KeyboardTask3Key: {
            const int index = TaskIndexFromBindTarget(captureTarget_);
            if (index >= 0) {
                keyboard_.tasks[index].key = vk;
                keyboard_.tasks[index].lastFireMs = NowMs();
            }
            break;
        }
        case BindTarget::None:
            break;
        }

        captureTarget_ = BindTarget::None;
        SetStatus(T(L"status_bound"));
        SaveConfig();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void NormalizeRandomCps(MouseClickConfig& config) {
        config.cps = ClampCps(config.cps);
        config.randomMinCps = ClampCps(config.randomMinCps);
        config.randomMaxCps = ClampCps(config.randomMaxCps);
        if (config.randomMinCps > config.randomMaxCps) {
            std::swap(config.randomMinCps, config.randomMaxCps);
        }
    }

    bool IsNumberTarget(NumberTarget target) const {
        return numberTarget_ == target;
    }

    NumberTarget NumberTargetForTask(int index) const {
        return static_cast<NumberTarget>(static_cast<int>(NumberTarget::KeyboardTask0Seconds) + index);
    }

    int TaskIndexFromNumberTarget(NumberTarget target) const {
        const int index = static_cast<int>(target) - static_cast<int>(NumberTarget::KeyboardTask0Seconds);
        return index >= 0 && index < kTimedKeySlots ? index : -1;
    }

    void StartNumberEdit(NumberTarget target, int currentValue) {
        numberTarget_ = target;
        numberBuffer_ = std::to_wstring(currentValue);
        numberSelectAll_ = true;
        SetStatus(T(L"type_number"));
    }

    std::wstring NumberText(NumberTarget target, int value, const std::wstring& suffix = std::wstring()) const {
        if (numberTarget_ == target) {
            return numberBuffer_.empty() ? T(L"type_number") : numberBuffer_ + suffix;
        }
        return std::to_wstring(value) + suffix;
    }

    void HandleNumberKey(UINT vk) {
        if (numberTarget_ == NumberTarget::None) {
            return;
        }

        int digit = -1;
        if (vk >= L'0' && vk <= L'9') {
            digit = static_cast<int>(vk - L'0');
        } else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
            digit = static_cast<int>(vk - VK_NUMPAD0);
        }

        if (digit >= 0) {
            if (numberSelectAll_) {
                numberBuffer_.clear();
                numberSelectAll_ = false;
            }
            if (numberBuffer_.size() < 6) {
                numberBuffer_.push_back(static_cast<wchar_t>(L'0' + digit));
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        if (vk == VK_BACK) {
            if (numberSelectAll_) {
                numberBuffer_.clear();
                numberSelectAll_ = false;
            } else if (!numberBuffer_.empty()) {
                numberBuffer_.pop_back();
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        if (vk == VK_RETURN || vk == VK_TAB) {
            CommitNumberEdit();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        if (vk == VK_ESCAPE) {
            CancelNumberEdit();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void CommitNumberEdit() {
        if (numberTarget_ == NumberTarget::None) {
            return;
        }

        if (!numberBuffer_.empty()) {
            int value = 0;
            try {
                value = std::stoi(numberBuffer_);
            } catch (...) {
                value = 0;
            }
            ApplyNumberValue(numberTarget_, value);
        }

        numberTarget_ = NumberTarget::None;
        numberBuffer_.clear();
        numberSelectAll_ = false;
        SetStatus(T(L"status_ready"));
    }

    void CancelNumberEdit() {
        numberTarget_ = NumberTarget::None;
        numberBuffer_.clear();
        numberSelectAll_ = false;
        SetStatus(T(L"status_cancel"));
    }

    void ApplyNumberValue(NumberTarget target, int value) {
        switch (target) {
        case NumberTarget::MouseLeftCps:
            leftClick_.cps = ClampCps(value);
            leftClick_.nextIntervalMs = 0.0;
            break;
        case NumberTarget::MouseRightCps:
            rightClick_.cps = ClampCps(value);
            rightClick_.nextIntervalMs = 0.0;
            break;
        case NumberTarget::MouseLeftRandomMin:
            leftClick_.randomMinCps = ClampCps(value);
            NormalizeRandomCps(leftClick_);
            leftClick_.nextIntervalMs = 0.0;
            break;
        case NumberTarget::MouseLeftRandomMax:
            leftClick_.randomMaxCps = ClampCps(value);
            NormalizeRandomCps(leftClick_);
            leftClick_.nextIntervalMs = 0.0;
            break;
        case NumberTarget::MouseRightRandomMin:
            rightClick_.randomMinCps = ClampCps(value);
            NormalizeRandomCps(rightClick_);
            rightClick_.nextIntervalMs = 0.0;
            break;
        case NumberTarget::MouseRightRandomMax:
            rightClick_.randomMaxCps = ClampCps(value);
            NormalizeRandomCps(rightClick_);
            rightClick_.nextIntervalMs = 0.0;
            break;
        case NumberTarget::KeyboardRate:
            keyboard_.rate = ClampRate(value);
            break;
        case NumberTarget::KeyboardTask0Seconds:
        case NumberTarget::KeyboardTask1Seconds:
        case NumberTarget::KeyboardTask2Seconds:
        case NumberTarget::KeyboardTask3Seconds: {
            const int index = TaskIndexFromNumberTarget(target);
            if (index >= 0) {
                keyboard_.tasks[index].intervalSec = ClampSeconds(value);
                keyboard_.tasks[index].lastFireMs = NowMs();
            }
            break;
        }
        case NumberTarget::None:
            break;
        }
        SaveConfig();
    }

    void SetStatus(const std::wstring& status) {
        statusText_ = status;
        statusUntilMs_ = NowMs() + 1800.0;
    }

    void TickAutomation() {
        const double now = NowMs();
        bool repaint = false;

        repaint = TickMouseAction(leftClick_, MouseButton::Left, now) || repaint;
        repaint = TickMouseAction(rightClick_, MouseButton::Right, now) || repaint;
        repaint = TickKeyboardAction(now) || repaint;
        repaint = TickTimedKeyTasks(now) || repaint;

        if (!statusText_.empty() && now > statusUntilMs_) {
            statusText_.clear();
            repaint = true;
        }

        if (repaint) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    bool TickMouseAction(MouseClickConfig& config, MouseButton button, double now) {
        if (!config.running) {
            return false;
        }

        if (config.nextIntervalMs <= 0.0) {
            config.nextIntervalMs = NextMouseIntervalMs(config);
        }

        if (config.lastFireMs == 0.0 || now - config.lastFireMs >= config.nextIntervalMs) {
            FireMouseClick(button, config);
            config.lastFireMs = now;
            config.nextIntervalMs = NextMouseIntervalMs(config);
            bool stateChanged = false;
            if (config.mode == TriggerMode::Burst && config.burstRemaining > 0) {
                --config.burstRemaining;
                if (config.burstRemaining <= 0) {
                    config.running = false;
                    stateChanged = true;
                }
            }
            return stateChanged;
        }
        return false;
    }

    double NextMouseIntervalMs(MouseClickConfig& config) {
        NormalizeRandomCps(config);
        int cps = config.cps;
        if (config.randomCps) {
            std::uniform_int_distribution<int> dist(config.randomMinCps, config.randomMaxCps);
            cps = dist(rng_);
        }
        return 1000.0 / static_cast<double>(std::max(1, cps));
    }

    bool TickKeyboardAction(double now) {
        if (!keyboard_.running || !keyboard_.mainKeyEnabled) {
            return false;
        }

        const double interval = 1000.0 / static_cast<double>(std::max(1, keyboard_.rate));
        if (keyboard_.lastFireMs == 0.0 || now - keyboard_.lastFireMs >= interval) {
            FireKeyTap(keyboard_.targetKey, keyboard_.doubleTap);
            keyboard_.lastFireMs = now;
            bool stateChanged = false;
            if (keyboard_.mode == TriggerMode::Burst && keyboard_.burstRemaining > 0) {
                --keyboard_.burstRemaining;
                if (keyboard_.burstRemaining <= 0) {
                    keyboard_.running = false;
                    stateChanged = true;
                }
            }
            return stateChanged;
        }
        return false;
    }

    bool TickTimedKeyTasks(double now) {
        if (!keyboard_.running) {
            return false;
        }

        for (auto& task : keyboard_.tasks) {
            if (!task.enabled) {
                continue;
            }

            const double interval = static_cast<double>(ClampSeconds(task.intervalSec)) * 1000.0;
            if (task.lastFireMs == 0.0) {
                task.lastFireMs = now;
                continue;
            }

            if (now - task.lastFireMs >= interval) {
                FireKeyTap(task.key, false);
                task.lastFireMs = now;
            }
        }
        return false;
    }

    void SendMouseButton(DWORD flag) {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = flag;
        SendInput(1, &input, sizeof(INPUT));
    }

    bool IsExtendedKey(UINT vk) const {
        switch (vk) {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_CANCEL:
        case VK_SNAPSHOT:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
            return true;
        default:
            return false;
        }
    }

    void PushVkKey(std::vector<INPUT>& inputs, UINT vk, bool keyUp) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(vk);
        input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
        inputs.push_back(input);
    }

    void PushScanKey(std::vector<INPUT>& inputs, UINT vk, bool keyUp) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (keyUp ? KEYEVENTF_KEYUP : 0);
        if (IsExtendedKey(vk)) {
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        inputs.push_back(input);
    }

    void FireMouseClick(MouseButton button, const MouseClickConfig& config) {
        const DWORD downFlag = button == MouseButton::Left ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_RIGHTDOWN;
        const DWORD upFlag = button == MouseButton::Left ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_RIGHTUP;
        const int repeats = config.doubleClick ? 2 : 1;

        if (config.backend == InputBackend::LegacyMouseEvent) {
            for (int i = 0; i < repeats; ++i) {
                mouse_event(downFlag, 0, 0, 0, 0);
                Sleep(2);
                mouse_event(upFlag, 0, 0, 0, 0);
                Sleep(1);
            }
        } else if (config.backend == InputBackend::GameCompatibleBackend) {
            for (int i = 0; i < repeats; ++i) {
                SendMouseButton(downFlag);
                Sleep(1);
                SendMouseButton(upFlag);
            }
        } else {
            std::vector<INPUT> inputs;
            inputs.reserve(static_cast<size_t>(repeats) * 2);
            for (int i = 0; i < repeats; ++i) {
                INPUT down = {};
                down.type = INPUT_MOUSE;
                down.mi.dwFlags = downFlag;
                INPUT up = {};
                up.type = INPUT_MOUSE;
                up.mi.dwFlags = upFlag;
                inputs.push_back(down);
                inputs.push_back(up);
            }
            SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
        }
    }

    void FireKeyTap(UINT vk, bool doubleTap) {
        const int repeats = doubleTap ? 2 : 1;

        if (keyboard_.backend == InputBackend::LegacyMouseEvent) {
            const BYTE scan = static_cast<BYTE>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
            const DWORD extended = IsExtendedKey(vk) ? KEYEVENTF_EXTENDEDKEY : 0;
            for (int i = 0; i < repeats; ++i) {
                keybd_event(static_cast<BYTE>(vk), scan, extended, 0);
                Sleep(2);
                keybd_event(static_cast<BYTE>(vk), scan, extended | KEYEVENTF_KEYUP, 0);
                Sleep(1);
            }
            return;
        }

        for (int i = 0; i < repeats; ++i) {
            std::vector<INPUT> down;
            std::vector<INPUT> up;
            down.reserve(1);
            up.reserve(1);

            if (keyboard_.backend == InputBackend::GameCompatibleBackend) {
                PushScanKey(down, vk, false);
                PushScanKey(up, vk, true);
                SendInput(static_cast<UINT>(down.size()), down.data(), sizeof(INPUT));
                Sleep(3);
                SendInput(static_cast<UINT>(up.size()), up.data(), sizeof(INPUT));
                Sleep(2);
            } else {
                PushVkKey(down, vk, false);
                PushVkKey(up, vk, true);
                down.insert(down.end(), up.begin(), up.end());
                SendInput(static_cast<UINT>(down.size()), down.data(), sizeof(INPUT));
            }
        }
    }

    MouseClickConfig& ActiveMouseConfig() {
        return activeMouseButton_ == MouseButton::Left ? leftClick_ : rightClick_;
    }

    const MouseClickConfig& ActiveMouseConfig() const {
        return activeMouseButton_ == MouseButton::Left ? leftClick_ : rightClick_;
    }

    void Paint() {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd_, &ps);
        RECT client = {};
        GetClientRect(hwnd_, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;

        HDC memDc = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
        HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

        Graphics g(memDc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        Draw(g, width, height);

        BitBlt(hdc, 0, 0, width, height, memDc, 0, 0, SRCCOPY);

        SelectObject(memDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        EndPaint(hwnd_, &ps);
    }

    void Draw(Graphics& g, int width, int height) {
        hitAreas_.clear();

        LinearGradientBrush bg(Point(0, 0), Point(width, height), Rgb(15, 11, 24), Rgb(5, 4, 9));
        g.FillRectangle(&bg, 0, 0, width, height);

        DrawSidebar(g, height);

        RectF content(236.0f, 24.0f, static_cast<float>(width - 260), static_cast<float>(height - 48));
        switch (page_) {
        case Page::Mouse:
            DrawMousePage(g, content);
            break;
        case Page::Keyboard:
            DrawKeyboardPage(g, content);
            break;
        case Page::Settings:
            DrawSettingsPage(g, content);
            break;
        }

        DrawStatus(g, width, height);
    }

    void DrawSidebar(Graphics& g, int height) {
        RectF sidebar(0.0f, 0.0f, 216.0f, static_cast<float>(height));
        LinearGradientBrush sideBrush(Point(0, 0), Point(216, height), Rgb(10, 8, 16), Rgb(18, 10, 28));
        g.FillRectangle(&sideBrush, sidebar);

        Pen border(Rgb(87, 47, 128, 110), 1.0f);
        g.DrawLine(&border, 216.0f, 0.0f, 216.0f, static_cast<float>(height));

        DrawBrandLogo(g, RectF(24, 24, 56, 56));
        DrawText(g, L"OGX", RectF(90, 29, 104, 24), 22.0f, Rgb(244, 238, 255), FontStyleBold);
        DrawText(g, L"Auto Clicker", RectF(90, 56, 104, 22), 13.0f, Rgb(166, 147, 193));

        DrawNavButton(g, RectF(18, 128, 180, 52), T(L"mouse"), Page::Mouse);
        DrawNavButton(g, RectF(18, 190, 180, 52), T(L"keyboard"), Page::Keyboard);
        DrawNavButton(g, RectF(18, 252, 180, 52), T(L"settings"), Page::Settings);

        DrawText(g, T(L"accent"), RectF(26, static_cast<float>(height - 74), 164, 24), 13.0f, Rgb(154, 133, 180));
        RectF swatch(28, static_cast<float>(height - 42), 154, 12);
        LinearGradientBrush accentBrush(PointF(swatch.X, swatch.Y), PointF(swatch.X + swatch.Width, swatch.Y), Rgb(104, 55, 255), Rgb(225, 92, 255));
        FillRound(g, swatch, 6.0f, accentBrush);
    }

    void DrawBrandLogo(Graphics& g, const RectF& rect) {
        if (logoImage_) {
            g.DrawImage(logoImage_.get(), rect);
        } else {
            LinearGradientBrush bg(PointF(rect.X, rect.Y), PointF(rect.X + rect.Width, rect.Y + rect.Height), Rgb(132, 62, 255), Rgb(217, 79, 255));
            FillRound(g, rect, 13.0f, bg);
            DrawText(g, L"OGX", rect, 11.0f, Rgb(255, 255, 255), FontStyleBold, StringAlignmentCenter);
        }
    }

    void DrawNavButton(Graphics& g, const RectF& rect, const std::wstring& label, Page target) {
        const bool active = page_ == target;
        const bool hover = Contains(rect, hoverX_, hoverY_);
        FillRound(g, rect, 14.0f, active ? Rgb(103, 61, 190, 230) : (hover ? Rgb(42, 26, 66, 220) : Rgb(0, 0, 0, 0)));
        if (active) {
            StrokeRound(g, rect, 14.0f, Rgb(180, 112, 255, 170), 1.0f);
        }

        RectF icon(rect.X + 15, rect.Y + 15, 22, 22);
        DrawNavIcon(g, icon, target, active ? Rgb(255, 255, 255) : Rgb(179, 153, 210));
        DrawText(g, label, RectF(rect.X + 50, rect.Y, rect.Width - 60, rect.Height), 16.0f, active ? Rgb(255, 255, 255) : Rgb(196, 179, 216), active ? FontStyleBold : FontStyleRegular);

        hitAreas_.push_back({rect, [this, target]() {
            page_ = target;
            SaveConfig();
        }});
    }

    void DrawNavIcon(Graphics& g, const RectF& rect, Page target, const Color& color) {
        Pen pen(color, 2.0f);
        SolidBrush brush(color);
        if (target == Page::Mouse) {
            g.DrawEllipse(&pen, rect.X + 4, rect.Y + 1, rect.Width - 8, rect.Height - 2);
            g.DrawLine(&pen, rect.X + rect.Width / 2, rect.Y + 3, rect.X + rect.Width / 2, rect.Y + 11);
            g.FillEllipse(&brush, rect.X + rect.Width / 2 - 1.5f, rect.Y + 6, 3.0f, 5.0f);
        } else if (target == Page::Keyboard) {
            StrokeRound(g, rect, 5.0f, color, 2.0f);
            for (int row = 0; row < 2; ++row) {
                for (int col = 0; col < 3; ++col) {
                    g.FillRectangle(&brush, rect.X + 5 + col * 5.5f, rect.Y + 6 + row * 6.0f, 3.0f, 3.0f);
                }
            }
        } else {
            g.DrawEllipse(&pen, rect.X + 4, rect.Y + 4, rect.Width - 8, rect.Height - 8);
            g.DrawEllipse(&pen, rect.X + 9, rect.Y + 9, rect.Width - 18, rect.Height - 18);
            g.DrawLine(&pen, rect.X + rect.Width / 2, rect.Y + 1, rect.X + rect.Width / 2, rect.Y + 6);
            g.DrawLine(&pen, rect.X + rect.Width / 2, rect.Y + rect.Height - 6, rect.X + rect.Width / 2, rect.Y + rect.Height - 1);
            g.DrawLine(&pen, rect.X + 1, rect.Y + rect.Height / 2, rect.X + 6, rect.Y + rect.Height / 2);
            g.DrawLine(&pen, rect.X + rect.Width - 6, rect.Y + rect.Height / 2, rect.X + rect.Width - 1, rect.Y + rect.Height / 2);
        }
    }

    void DrawMousePage(Graphics& g, const RectF& content) {
        DrawPageTitle(g, content, T(L"mouse_title"));

        RectF mouseRect(content.X + 34, content.Y + 110, 330, 398);
        DrawMouseGraphic(g, mouseRect);

        RectF panel(content.X + content.Width - 360, content.Y + 92, 330, 556);
        DrawMouseSettingsPanel(g, panel);
    }

    void DrawPageTitle(Graphics& g, const RectF& content, const std::wstring& title) {
        DrawText(g, title, RectF(content.X, content.Y, content.Width, 46), 30.0f, Rgb(250, 247, 255), FontStyleBold);
        RectF underline(content.X, content.Y + 56, 96, 4);
        LinearGradientBrush accent(PointF(underline.X, underline.Y), PointF(underline.X + underline.Width, underline.Y), Rgb(129, 74, 255), Rgb(236, 92, 255));
        FillRound(g, underline, 2.0f, accent);
    }

    void DrawMouseGraphic(Graphics& g, const RectF& rect) {
        Pen cable(Rgb(118, 74, 182, 130), 3.0f);
        g.DrawBezier(&cable,
                     rect.X + rect.Width / 2,
                     rect.Y + 8,
                     rect.X + rect.Width / 2 - 18,
                     rect.Y - 58,
                     rect.X + rect.Width / 2 + 42,
                     rect.Y - 76,
                     rect.X + rect.Width / 2 + 26,
                     rect.Y - 126);

        RectF shadow(rect.X + 12, rect.Y + 28, rect.Width - 24, rect.Height - 10);
        FillRound(g, shadow, 104.0f, Rgb(0, 0, 0, 125));

        GraphicsPath body;
        BuildRoundRect(body, rect, 110.0f);
        LinearGradientBrush bodyBrush(PointF(rect.X, rect.Y), PointF(rect.X + rect.Width, rect.Y + rect.Height), Rgb(42, 31, 61), Rgb(8, 7, 14));
        g.FillPath(&bodyBrush, &body);
        Pen bodyPen(Rgb(163, 93, 244, 190), 2.0f);
        g.DrawPath(&bodyPen, &body);

        RectF left(rect.X + 34, rect.Y + 36, rect.Width / 2 - 38, 138);
        RectF right(rect.X + rect.Width / 2 + 4, rect.Y + 36, rect.Width / 2 - 38, 138);

        leftButtonZone_ = RectFromRectF(left);
        rightButtonZone_ = RectFromRectF(right);

        DrawMouseButtonZone(g, left, MouseButton::Left, leftMouseHover_ || activeMouseButton_ == MouseButton::Left);
        DrawMouseButtonZone(g, right, MouseButton::Right, rightMouseHover_ || activeMouseButton_ == MouseButton::Right);

        Pen divider(Rgb(118, 77, 159, 150), 2.0f);
        g.DrawLine(&divider, rect.X + rect.Width / 2, rect.Y + 38, rect.X + rect.Width / 2, rect.Y + 170);

        RectF wheel(rect.X + rect.Width / 2 - 14, rect.Y + 82, 28, 72);
        LinearGradientBrush wheelBrush(PointF(wheel.X, wheel.Y), PointF(wheel.X, wheel.Y + wheel.Height), Rgb(174, 111, 255), Rgb(87, 48, 151));
        FillRound(g, wheel, 14.0f, wheelBrush);
        Pen wheelLine(Rgb(239, 224, 255, 120), 1.0f);
        g.DrawLine(&wheelLine, wheel.X + 14, wheel.Y + 14, wheel.X + 14, wheel.Y + 55);
    }

    RECT RectFromRectF(const RectF& rect) const {
        RECT r = {};
        r.left = static_cast<LONG>(std::floor(rect.X));
        r.top = static_cast<LONG>(std::floor(rect.Y));
        r.right = static_cast<LONG>(std::ceil(rect.X + rect.Width));
        r.bottom = static_cast<LONG>(std::ceil(rect.Y + rect.Height));
        return r;
    }

    void DrawMouseButtonZone(Graphics& g, const RectF& rect, MouseButton button, bool hot) {
        const bool running = button == MouseButton::Left ? leftClick_.running : rightClick_.running;
        LinearGradientBrush brush(
            PointF(rect.X, rect.Y),
            PointF(rect.X, rect.Y + rect.Height),
            hot ? Rgb(126, 70, 239, 235) : Rgb(42, 29, 63, 210),
            running ? Rgb(235, 88, 255, 220) : Rgb(24, 18, 36, 220));
        FillRound(g, rect, 38.0f, brush);
        StrokeRound(g, rect, 38.0f, hot ? Rgb(229, 180, 255, 210) : Rgb(98, 68, 132, 150), hot ? 2.0f : 1.0f);

        const std::wstring label = button == MouseButton::Left ? T(L"left_click") : T(L"right_click");
        DrawText(g, label, RectF(rect.X + 10, rect.Y + 34, rect.Width - 20, 28), 16.0f, Rgb(255, 252, 255), FontStyleBold, StringAlignmentCenter);
        DrawText(g, KeyName(button == MouseButton::Left ? leftClick_.hotkey : rightClick_.hotkey), RectF(rect.X + 10, rect.Y + 70, rect.Width - 20, 24), 14.0f, Rgb(223, 201, 255), FontStyleRegular, StringAlignmentCenter);
    }

    void DrawMouseSettingsPanel(Graphics& g, const RectF& rect) {
        DrawPanel(g, rect);

        MouseClickConfig& config = ActiveMouseConfig();
        const bool isLeft = activeMouseButton_ == MouseButton::Left;
        const std::wstring title = isLeft ? T(L"left_click") : T(L"right_click");

        DrawText(g, title, RectF(rect.X + 24, rect.Y + 20, rect.Width - 48, 30), 22.0f, Rgb(251, 247, 255), FontStyleBold);
        DrawStatusPill(g, RectF(rect.X + rect.Width - 122, rect.Y + 24, 90, 24), config.running);

        const NumberTarget cpsTarget = isLeft ? NumberTarget::MouseLeftCps : NumberTarget::MouseRightCps;
        const NumberTarget minTarget = isLeft ? NumberTarget::MouseLeftRandomMin : NumberTarget::MouseRightRandomMin;
        const NumberTarget maxTarget = isLeft ? NumberTarget::MouseLeftRandomMax : NumberTarget::MouseRightRandomMax;
        const bool gameMouseMode = config.backend == InputBackend::GameCompatibleBackend;
        const std::wstring clickRateLabel = gameMouseMode ? T(L"cps_label") : T(L"cps");
        const std::wstring clickRateSuffix = gameMouseMode ? L" CPS" : std::wstring();

        float y = rect.Y + 54;
        DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), T(L"keybind"));
        DrawButton(g, RectF(rect.X + 24, y + 24, rect.Width - 48, 34),
                   IsCapturing(isLeft ? BindTarget::MouseLeftHotkey : BindTarget::MouseRightHotkey) ? T(L"waiting_key") : KeyName(config.hotkey),
                   false,
                   [this, isLeft]() {
                       captureTarget_ = isLeft ? BindTarget::MouseLeftHotkey : BindTarget::MouseRightHotkey;
                       SetStatus(T(L"waiting_key"));
                   });

        y += 64;
        DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), T(L"single_click") + L" / " + T(L"double_click"));
        DrawSegmentedTwo(g,
                         RectF(rect.X + 24, y + 24, rect.Width - 48, 34),
                         T(L"single_click"),
                         T(L"double_click"),
                         !config.doubleClick,
                         [this, isLeft]() {
                             MouseClickConfig& cfg = isLeft ? leftClick_ : rightClick_;
                             cfg.doubleClick = false;
                             SaveConfig();
                         },
                         [this, isLeft]() {
                             MouseClickConfig& cfg = isLeft ? leftClick_ : rightClick_;
                             cfg.doubleClick = true;
                             SaveConfig();
                         });

        y += 62;
        DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), clickRateLabel);
        DrawSegmentedTwo(g,
                         RectF(rect.X + 24, y + 24, rect.Width - 48, 34),
                         T(L"fixed_cps"),
                         T(L"random_cps"),
                         !config.randomCps,
                         [this, isLeft]() {
                             MouseClickConfig& cfg = isLeft ? leftClick_ : rightClick_;
                             cfg.randomCps = false;
                             cfg.nextIntervalMs = 0.0;
                             SaveConfig();
                         },
                         [this, isLeft]() {
                             MouseClickConfig& cfg = isLeft ? leftClick_ : rightClick_;
                             cfg.randomCps = true;
                             NormalizeRandomCps(cfg);
                             cfg.nextIntervalMs = 0.0;
                             SaveConfig();
                         });

        y += 58;
        if (config.randomCps) {
            DrawStepper(g, RectF(rect.X + 24, y, rect.Width - 48, 58), T(L"min_cps"), config.randomMinCps, [this, isLeft](int delta) {
                MouseClickConfig& cfg = isLeft ? leftClick_ : rightClick_;
                cfg.randomMinCps = ClampCps(cfg.randomMinCps + delta);
                NormalizeRandomCps(cfg);
                cfg.nextIntervalMs = 0.0;
                SaveConfig();
            }, 1, clickRateSuffix, minTarget);

            y += 58;
            DrawStepper(g, RectF(rect.X + 24, y, rect.Width - 48, 58), T(L"max_cps"), config.randomMaxCps, [this, isLeft](int delta) {
                MouseClickConfig& cfg = isLeft ? leftClick_ : rightClick_;
                cfg.randomMaxCps = ClampCps(cfg.randomMaxCps + delta);
                NormalizeRandomCps(cfg);
                cfg.nextIntervalMs = 0.0;
                SaveConfig();
            }, 1, clickRateSuffix, maxTarget);
        } else {
            DrawStepper(g, RectF(rect.X + 24, y, rect.Width - 48, 58), clickRateLabel, config.cps, [this, isLeft](int delta) {
                MouseClickConfig& cfg = isLeft ? leftClick_ : rightClick_;
                cfg.cps = ClampCps(cfg.cps + delta);
                cfg.nextIntervalMs = 0.0;
                SaveConfig();
            }, 1, clickRateSuffix, cpsTarget);
        }

        y += 60;
        DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), T(L"mode"));
        DrawModeButtons(g, RectF(rect.X + 24, y + 24, rect.Width - 48, 34), config.mode, [this, isLeft](TriggerMode mode) {
            MouseClickConfig& cfg = isLeft ? leftClick_ : rightClick_;
            cfg.mode = mode;
            cfg.running = false;
            cfg.burstRemaining = 0;
            cfg.nextIntervalMs = 0.0;
            SaveConfig();
        });

        y += 58;
        DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), T(L"backend"));
        DrawBackendButtons(g, RectF(rect.X + 24, y + 24, rect.Width - 48, 34), config.backend, [this, isLeft](InputBackend backend) {
            MouseClickConfig& cfg = isLeft ? leftClick_ : rightClick_;
            cfg.backend = backend;
            SaveConfig();
        });

        DrawButton(g,
                   RectF(rect.X + 24, rect.Y + rect.Height - 58, rect.Width - 48, 40),
                   config.running ? T(L"stop") : T(L"start"),
                   config.running,
                   [this, isLeft]() {
                       MouseClickConfig& cfg = isLeft ? leftClick_ : rightClick_;
                       if (cfg.mode == TriggerMode::Burst && !cfg.running) {
                           cfg.running = true;
                           cfg.burstRemaining = kBurstCycles;
                           cfg.lastFireMs = 0.0;
                           cfg.nextIntervalMs = 0.0;
                       } else {
                           cfg.running = !cfg.running;
                           cfg.burstRemaining = 0;
                           cfg.lastFireMs = 0.0;
                           cfg.nextIntervalMs = 0.0;
                       }
                   });
    }

    void DrawKeyboardPage(Graphics& g, const RectF& content) {
        DrawPageTitle(g, content, T(L"keyboard_title"));

        RectF keyboardRect(content.X + 18, content.Y + 124, content.Width - 390, 282);
        DrawKeyboardVisual(g, keyboardRect);

        RectF timedRect(content.X + 18, content.Y + 430, content.Width - 390, 218);
        DrawTimedSettingsPanel(g, timedRect);

        RectF panel(content.X + content.Width - 360, content.Y + 92, 330, 556);
        DrawKeyboardPanel(g, panel);
    }

    void DrawKeyboardVisual(Graphics& g, const RectF& rect) {
        DrawPanel(g, rect);

        const float keyH = 42.0f;
        const float gap = 8.0f;
        float y = rect.Y + 34;
        DrawKeyboardRow(g, rect.X + 34, y, keyH, gap, {L"Q", L"W", L"E", L"R", L"T", L"Y", L"U", L"I", L"O", L"P"});
        y += keyH + gap;
        DrawKeyboardRow(g, rect.X + 54, y, keyH, gap, {L"A", L"S", L"D", L"F", L"G", L"H", L"J", L"K", L"L"});
        y += keyH + gap;
        DrawKeyboardRow(g, rect.X + 78, y, keyH, gap, {L"Z", L"X", L"C", L"V", L"B", L"N", L"M"});
        y += keyH + gap + 2;

        DrawKey(g, RectF(rect.X + 96, y, 74, keyH), L"Ctrl", VK_CONTROL);
        DrawKey(g, RectF(rect.X + 178, y, 74, keyH), L"Alt", VK_MENU);
        DrawKey(g, RectF(rect.X + 260, y, 230, keyH), L"Space", VK_SPACE);
        DrawKey(g, RectF(rect.X + 498, y, 74, keyH), L"Tab", VK_TAB);

        const std::wstring footer = keyboard_.mainKeyEnabled ? (T(L"target_key") + L": " + KeyName(keyboard_.targetKey)) : T(L"timed_only");
        DrawText(g, footer,
                 RectF(rect.X + 34, rect.Y + rect.Height - 44, rect.Width - 68, 28),
                 17.0f,
                 Rgb(224, 208, 248),
                 FontStyleBold,
                 StringAlignmentCenter);
    }

    void DrawKeyboardRow(Graphics& g, float x, float y, float keyH, float gap, std::initializer_list<const wchar_t*> keys) {
        float currentX = x;
        for (const wchar_t* key : keys) {
            const UINT vk = static_cast<UINT>(key[0]);
            DrawKey(g, RectF(currentX, y, 48, keyH), key, vk);
            currentX += 48 + gap;
        }
    }

    void DrawKey(Graphics& g, const RectF& rect, const std::wstring& label, UINT vk) {
        const bool active = keyboard_.targetKey == vk;
        const bool hover = Contains(rect, hoverX_, hoverY_);
        LinearGradientBrush brush(PointF(rect.X, rect.Y), PointF(rect.X, rect.Y + rect.Height),
                                  active ? Rgb(143, 76, 255) : (hover ? Rgb(55, 37, 79) : Rgb(26, 21, 38)),
                                  active ? Rgb(223, 84, 255) : Rgb(14, 12, 22));
        FillRound(g, rect, 8.0f, brush);
        StrokeRound(g, rect, 8.0f, active ? Rgb(243, 210, 255, 220) : Rgb(94, 68, 124, 145), active ? 2.0f : 1.0f);
        DrawText(g, label, rect, 15.0f, Rgb(246, 240, 255), active ? FontStyleBold : FontStyleRegular, StringAlignmentCenter);

        hitAreas_.push_back({rect, [this, vk]() {
            keyboard_.targetKey = vk;
            SaveConfig();
        }});
    }

    void DrawTimedSettingsPanel(Graphics& g, const RectF& rect) {
        DrawPanel(g, rect);
        DrawText(g, T(L"timed_settings"), RectF(rect.X + 22, rect.Y + 16, rect.Width - 44, 28), 20.0f, Rgb(251, 247, 255), FontStyleBold);

        for (int i = 0; i < kTimedKeySlots; ++i) {
            DrawTimedTaskRow(g, RectF(rect.X + 20, rect.Y + 56 + i * 38.0f, rect.Width - 40, 32), i);
        }
    }

    std::wstring FormatDuration(int totalSeconds) const {
        totalSeconds = ClampSeconds(totalSeconds);
        const int hours = totalSeconds / 3600;
        const int minutes = (totalSeconds % 3600) / 60;
        const int seconds = totalSeconds % 60;

        std::wstring text;
        if (hours > 0) {
            text += std::to_wstring(hours) + L" " + T(L"hours_short") + L" ";
        }
        if (minutes > 0 || hours > 0) {
            text += std::to_wstring(minutes) + L" " + T(L"minutes_short") + L" ";
        }
        text += std::to_wstring(seconds) + L" " + T(L"seconds_short");
        return text;
    }

    void DrawTimedTaskRow(Graphics& g, const RectF& rect, int index) {
        TimedKeyTask& task = keyboard_.tasks[index];
        const NumberTarget secondsTarget = NumberTargetForTask(index);
        const bool hover = Contains(rect, hoverX_, hoverY_);
        FillRound(g, rect, 10.0f, hover ? Rgb(31, 23, 47, 220) : Rgb(17, 13, 26, 190));
        StrokeRound(g, rect, 10.0f, Rgb(82, 57, 112, 110), 1.0f);

        RectF toggle(rect.X + 8, rect.Y + 5, 56, 22);
        FillRound(g, toggle, 11.0f, task.enabled ? Rgb(126, 70, 232) : Rgb(47, 38, 62));
        DrawText(g, task.enabled ? T(L"on") : T(L"off"), toggle, 11.0f, Rgb(255, 255, 255), FontStyleBold, StringAlignmentCenter);
        hitAreas_.push_back({toggle, [this, index]() {
            TimedKeyTask& cfg = keyboard_.tasks[index];
            cfg.enabled = !cfg.enabled;
            cfg.lastFireMs = NowMs();
            SaveConfig();
        }});

        DrawText(g, T(L"task") + L" " + std::to_wstring(index + 1), RectF(rect.X + 76, rect.Y, 78, rect.Height), 12.0f, Rgb(190, 171, 213), FontStyleBold);

        RectF keyButton(rect.X + 154, rect.Y + 4, 78, 24);
        DrawButton(g,
                   keyButton,
                   IsCapturing(BindTargetForTask(index)) ? T(L"waiting_key") : KeyName(task.key),
                   false,
                   [this, index]() {
                       captureTarget_ = BindTargetForTask(index);
                       SetStatus(T(L"waiting_key"));
                   });

        RectF minus(rect.X + 248, rect.Y + 4, 30, 24);
        RectF plus(rect.X + rect.Width - 38, rect.Y + 4, 30, 24);
        DrawTinyButton(g, minus, L"-", [this, index]() {
            TimedKeyTask& cfg = keyboard_.tasks[index];
            cfg.intervalSec = ClampSeconds(cfg.intervalSec - 5);
            cfg.lastFireMs = NowMs();
            SaveConfig();
        });
        DrawTinyButton(g, plus, L"+", [this, index]() {
            TimedKeyTask& cfg = keyboard_.tasks[index];
            cfg.intervalSec = ClampSeconds(cfg.intervalSec + 5);
            cfg.lastFireMs = NowMs();
            SaveConfig();
        });

        RectF intervalRect(minus.X + 36, rect.Y + 3, plus.X - minus.X - 42, rect.Height - 6);
        if (IsNumberTarget(secondsTarget)) {
            FillRound(g, intervalRect, 8.0f, Rgb(76, 45, 118, 210));
        }
        const std::wstring interval = T(L"interval") + L": " + NumberText(secondsTarget, task.intervalSec, L" " + T(L"seconds_short")) + L" | " + FormatDuration(task.intervalSec);
        DrawText(g, interval, intervalRect, 12.0f, Rgb(235, 224, 250), FontStyleBold, StringAlignmentCenter);
        hitAreas_.push_back({intervalRect, [this, secondsTarget, task]() {
            StartNumberEdit(secondsTarget, task.intervalSec);
        }});
    }

    void DrawKeyboardPanel(Graphics& g, const RectF& rect) {
        DrawPanel(g, rect);
        DrawText(g, T(L"keyboard"), RectF(rect.X + 24, rect.Y + 20, rect.Width - 48, 30), 22.0f, Rgb(251, 247, 255), FontStyleBold);
        DrawStatusPill(g, RectF(rect.X + rect.Width - 122, rect.Y + 24, 90, 24), keyboard_.running);

        float y = rect.Y + 54;
        DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), T(L"main_key_mode"));
        DrawSegmentedTwo(g,
                         RectF(rect.X + 24, y + 24, rect.Width - 48, 34),
                         T(L"main_key"),
                         T(L"timed_only"),
                         keyboard_.mainKeyEnabled,
                         [this]() {
                             keyboard_.mainKeyEnabled = true;
                             keyboard_.lastFireMs = 0.0;
                             SaveConfig();
                         },
                         [this]() {
                             keyboard_.mainKeyEnabled = false;
                             keyboard_.lastFireMs = 0.0;
                             keyboard_.burstRemaining = 0;
                             SaveConfig();
                         });

        y += 62;
        DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), T(L"hotkey"));
        DrawButton(g,
                   RectF(rect.X + 24, y + 24, rect.Width - 48, 34),
                   IsCapturing(BindTarget::KeyboardHotkey) ? T(L"waiting_key") : KeyName(keyboard_.hotkey),
                   false,
                   [this]() {
                       captureTarget_ = BindTarget::KeyboardHotkey;
                       SetStatus(T(L"waiting_key"));
                   });

        y += 62;
        if (keyboard_.mainKeyEnabled) {
            DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), T(L"target_key"));
            DrawButton(g,
                       RectF(rect.X + 24, y + 24, rect.Width - 48, 34),
                       IsCapturing(BindTarget::KeyboardTargetKey) ? T(L"waiting_key") : KeyName(keyboard_.targetKey),
                       false,
                       [this]() {
                           captureTarget_ = BindTarget::KeyboardTargetKey;
                           SetStatus(T(L"waiting_key"));
                       });
            y += 62;
        }

        DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), T(L"key_method"));
        DrawBackendButtons(g, RectF(rect.X + 24, y + 24, rect.Width - 48, 34), keyboard_.backend, [this](InputBackend backend) {
            keyboard_.backend = backend;
            SaveConfig();
        });

        y += 62;
        if (keyboard_.mainKeyEnabled) {
            DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), T(L"single_tap") + L" / " + T(L"double_tap"));
            DrawSegmentedTwo(g,
                             RectF(rect.X + 24, y + 24, rect.Width - 48, 34),
                             T(L"single_tap"),
                             T(L"double_tap"),
                             !keyboard_.doubleTap,
                             [this]() {
                                 keyboard_.doubleTap = false;
                                 SaveConfig();
                             },
                             [this]() {
                                 keyboard_.doubleTap = true;
                                 SaveConfig();
                             });

            y += 62;
            DrawStepper(g, RectF(rect.X + 24, y, rect.Width - 48, 58), T(L"rate"), keyboard_.rate, [this](int delta) {
                keyboard_.rate = ClampRate(keyboard_.rate + delta);
                SaveConfig();
            }, 1, std::wstring(), NumberTarget::KeyboardRate);

            y += 68;
        }

        DrawLabel(g, RectF(rect.X + 24, y, rect.Width - 48, 20), T(L"mode"));
        DrawModeButtons(g, RectF(rect.X + 24, y + 24, rect.Width - 48, 34), keyboard_.mode, [this](TriggerMode mode) {
            keyboard_.mode = mode;
            keyboard_.running = false;
            keyboard_.burstRemaining = 0;
            SaveConfig();
        });

        DrawButton(g,
                   RectF(rect.X + 24, rect.Y + rect.Height - 52, rect.Width - 48, 36),
                   keyboard_.running ? T(L"stop") : T(L"start"),
                   keyboard_.running,
                   [this]() {
                       const bool starting = !keyboard_.running;
                       if (keyboard_.mode == TriggerMode::Burst && starting && keyboard_.mainKeyEnabled) {
                           StartKeyboardAutomation(true);
                       } else {
                           if (starting) {
                               StartKeyboardAutomation(false);
                           } else {
                               StopKeyboardAutomation();
                           }
                       }
                   });
    }

    void DrawSettingsPage(Graphics& g, const RectF& content) {
        DrawPageTitle(g, content, T(L"settings_title"));

        RectF languagePanel(content.X + 18, content.Y + 110, content.Width - 390, 318);
        DrawPanel(g, languagePanel);
        DrawText(g, T(L"choose_language"), RectF(languagePanel.X + 24, languagePanel.Y + 22, languagePanel.Width - 48, 28), 21.0f, Rgb(250, 247, 255), FontStyleBold);

        const std::array<std::pair<Language, const wchar_t*>, 6> langs = {
            std::make_pair(Language::English, L"english"),
            std::make_pair(Language::Turkish, L"turkish"),
            std::make_pair(Language::Russian, L"russian"),
            std::make_pair(Language::German, L"german"),
            std::make_pair(Language::French, L"french"),
            std::make_pair(Language::Spanish, L"spanish"),
        };

        const float cardW = (languagePanel.Width - 72) / 2.0f;
        const float cardH = 64.0f;
        for (size_t i = 0; i < langs.size(); ++i) {
            const int col = static_cast<int>(i % 2);
            const int row = static_cast<int>(i / 2);
            RectF card(languagePanel.X + 24 + col * (cardW + 24), languagePanel.Y + 74 + row * (cardH + 18), cardW, cardH);
            DrawLanguageCard(g, card, langs[i].first, T(langs[i].second));
        }

        RectF configPanel(content.X + 18, content.Y + 450, content.Width - 390, 198);
        DrawConfigPanel(g, configPanel);

        RectF optionsPanel(content.X + content.Width - 340, content.Y + 110, 310, 336);
        DrawPanel(g, optionsPanel);
        DrawText(g, T(L"settings"), RectF(optionsPanel.X + 24, optionsPanel.Y + 22, optionsPanel.Width - 48, 28), 21.0f, Rgb(250, 247, 255), FontStyleBold);

        DrawLabel(g, RectF(optionsPanel.X + 24, optionsPanel.Y + 80, optionsPanel.Width - 48, 22), T(L"theme"));
        RectF themeBox(optionsPanel.X + 24, optionsPanel.Y + 112, optionsPanel.Width - 48, 58);
        LinearGradientBrush themeBrush(PointF(themeBox.X, themeBox.Y), PointF(themeBox.X + themeBox.Width, themeBox.Y + themeBox.Height), Rgb(35, 24, 50), Rgb(83, 42, 135));
        FillRound(g, themeBox, 12.0f, themeBrush);
        StrokeRound(g, themeBox, 12.0f, Rgb(156, 96, 226, 140), 1.0f);
        DrawText(g, T(L"accent"), RectF(themeBox.X + 18, themeBox.Y, themeBox.Width - 36, themeBox.Height), 17.0f, Rgb(252, 246, 255), FontStyleBold);

        DrawToggleRow(g, RectF(optionsPanel.X + 24, optionsPanel.Y + 190, optionsPanel.Width - 48, 44), T(L"always_top"), alwaysOnTop_, [this]() {
            alwaysOnTop_ = !alwaysOnTop_;
            ApplyWindowOptions();
            SaveConfig();
        });

        DrawToggleRow(g, RectF(optionsPanel.X + 24, optionsPanel.Y + 240, optionsPanel.Width - 48, 44), T(L"auto_save"), autoSave_, [this]() {
            autoSave_ = !autoSave_;
            SaveConfig(autoSave_);
        });

        DrawButton(g, RectF(optionsPanel.X + 24, optionsPanel.Y + 292, optionsPanel.Width - 48, 36), T(L"reset"), false, [this]() {
            ResetDefaults();
            SaveConfig();
            ApplyWindowOptions();
        });
    }

    void DrawConfigPanel(Graphics& g, const RectF& rect) {
        DrawPanel(g, rect);
        DrawText(g, T(L"configs"), RectF(rect.X + 24, rect.Y + 18, rect.Width - 48, 28), 21.0f, Rgb(250, 247, 255), FontStyleBold);

        DrawLabel(g, RectF(rect.X + 24, rect.Y + 62, rect.Width - 48, 20), T(L"current_config"));
        RectF selector(rect.X + 24, rect.Y + 88, rect.Width - 48, 38);
        FillRound(g, selector, 11.0f, Rgb(18, 14, 28));
        StrokeRound(g, selector, 11.0f, Rgb(91, 63, 121, 140));

        DrawTinyButton(g, RectF(selector.X + 4, selector.Y + 4, 42, selector.Height - 8), L"<", [this]() {
            CycleConfig(-1);
        });
        DrawTinyButton(g, RectF(selector.X + selector.Width - 46, selector.Y + 4, 42, selector.Height - 8), L">", [this]() {
            CycleConfig(1);
        });
        DrawText(g, currentConfig_, RectF(selector.X + 56, selector.Y, selector.Width - 112, selector.Height), 16.0f, Rgb(255, 252, 255), FontStyleBold, StringAlignmentCenter);

        const float buttonW = (rect.Width - 72) / 3.0f;
        RectF create(rect.X + 24, rect.Y + 144, buttonW, 36);
        RectF update(rect.X + 36 + buttonW, rect.Y + 144, buttonW, 36);
        RectF del(rect.X + 48 + buttonW * 2, rect.Y + 144, buttonW, 36);
        DrawButton(g, create, T(L"create_config"), false, [this]() { CreateConfig(); });
        DrawButton(g, update, T(L"update_config"), false, [this]() { UpdateConfig(); });
        DrawButton(g, del, T(L"delete_config"), false, [this]() { DeleteConfig(); });
    }

    void DrawPanel(Graphics& g, const RectF& rect) {
        FillRound(g, RectF(rect.X + 8, rect.Y + 12, rect.Width, rect.Height), 20.0f, Rgb(0, 0, 0, 90));
        LinearGradientBrush panelBrush(PointF(rect.X, rect.Y), PointF(rect.X, rect.Y + rect.Height), Rgb(25, 19, 38, 238), Rgb(11, 9, 18, 245));
        FillRound(g, rect, 20.0f, panelBrush);
        StrokeRound(g, rect, 20.0f, Rgb(118, 75, 161, 135), 1.0f);
    }

    void DrawLabel(Graphics& g, const RectF& rect, const std::wstring& label) {
        DrawText(g, label, rect, 13.0f, Rgb(169, 148, 195), FontStyleBold);
    }

    bool IsCapturing(BindTarget target) const {
        return captureTarget_ == target;
    }

    BindTarget BindTargetForTask(int index) const {
        return static_cast<BindTarget>(static_cast<int>(BindTarget::KeyboardTask0Key) + index);
    }

    int TaskIndexFromBindTarget(BindTarget target) const {
        const int index = static_cast<int>(target) - static_cast<int>(BindTarget::KeyboardTask0Key);
        return index >= 0 && index < kTimedKeySlots ? index : -1;
    }

    void DrawButton(Graphics& g, const RectF& rect, const std::wstring& label, bool active, std::function<void()> action) {
        const bool hover = Contains(rect, hoverX_, hoverY_);
        LinearGradientBrush brush(PointF(rect.X, rect.Y), PointF(rect.X, rect.Y + rect.Height),
                                  active ? Rgb(135, 71, 255) : (hover ? Rgb(59, 39, 87) : Rgb(31, 24, 45)),
                                  active ? Rgb(218, 78, 255) : Rgb(18, 15, 28));
        FillRound(g, rect, 11.0f, brush);
        StrokeRound(g, rect, 11.0f, active ? Rgb(240, 202, 255, 210) : Rgb(102, 72, 132, 150), active ? 2.0f : 1.0f);
        DrawText(g, label, RectF(rect.X + 12, rect.Y, rect.Width - 24, rect.Height), 15.0f, Rgb(247, 242, 255), active ? FontStyleBold : FontStyleRegular, StringAlignmentCenter);
        hitAreas_.push_back({rect, std::move(action)});
    }

    void DrawSegmentedTwo(Graphics& g,
                          const RectF& rect,
                          const std::wstring& left,
                          const std::wstring& right,
                          bool leftActive,
                          std::function<void()> leftAction,
                          std::function<void()> rightAction) {
        FillRound(g, rect, 11.0f, Rgb(18, 14, 28));
        StrokeRound(g, rect, 11.0f, Rgb(91, 63, 121, 140));

        RectF leftRect(rect.X + 3, rect.Y + 3, rect.Width / 2.0f - 4, rect.Height - 6);
        RectF rightRect(rect.X + rect.Width / 2.0f + 1, rect.Y + 3, rect.Width / 2.0f - 4, rect.Height - 6);
        DrawSegment(g, leftRect, left, leftActive, std::move(leftAction));
        DrawSegment(g, rightRect, right, !leftActive, std::move(rightAction));
    }

    void DrawSegment(Graphics& g, const RectF& rect, const std::wstring& text, bool active, std::function<void()> action) {
        const bool hover = Contains(rect, hoverX_, hoverY_);
        if (active || hover) {
            FillRound(g, rect, 9.0f, active ? Rgb(124, 70, 232) : Rgb(43, 31, 63));
        }
        DrawText(g, text, RectF(rect.X + 6, rect.Y, rect.Width - 12, rect.Height), 13.0f, active ? Rgb(255, 255, 255) : Rgb(198, 180, 220), active ? FontStyleBold : FontStyleRegular, StringAlignmentCenter);
        hitAreas_.push_back({rect, std::move(action)});
    }

    void DrawModeButtons(Graphics& g, const RectF& rect, TriggerMode active, std::function<void(TriggerMode)> action) {
        FillRound(g, rect, 11.0f, Rgb(18, 14, 28));
        StrokeRound(g, rect, 11.0f, Rgb(91, 63, 121, 140));
        const float w = (rect.Width - 8) / 3.0f;
        DrawModeSegment(g, RectF(rect.X + 3, rect.Y + 3, w, rect.Height - 6), T(L"toggle"), active == TriggerMode::Toggle, [action]() { action(TriggerMode::Toggle); });
        DrawModeSegment(g, RectF(rect.X + 4 + w, rect.Y + 3, w, rect.Height - 6), T(L"hold"), active == TriggerMode::Hold, [action]() { action(TriggerMode::Hold); });
        DrawModeSegment(g, RectF(rect.X + 5 + w * 2, rect.Y + 3, w, rect.Height - 6), T(L"burst"), active == TriggerMode::Burst, [action]() { action(TriggerMode::Burst); });
    }

    void DrawModeSegment(Graphics& g, const RectF& rect, const std::wstring& text, bool active, std::function<void()> action) {
        const bool hover = Contains(rect, hoverX_, hoverY_);
        if (active || hover) {
            FillRound(g, rect, 9.0f, active ? Rgb(124, 70, 232) : Rgb(43, 31, 63));
        }
        DrawText(g, text, RectF(rect.X + 4, rect.Y, rect.Width - 8, rect.Height), 12.5f, active ? Rgb(255, 255, 255) : Rgb(198, 180, 220), active ? FontStyleBold : FontStyleRegular, StringAlignmentCenter);
        hitAreas_.push_back({rect, std::move(action)});
    }

    void DrawBackendButtons(Graphics& g, const RectF& rect, InputBackend active, std::function<void(InputBackend)> action) {
        FillRound(g, rect, 11.0f, Rgb(18, 14, 28));
        StrokeRound(g, rect, 11.0f, Rgb(91, 63, 121, 140));
        const float w = (rect.Width - 8) / 3.0f;
        DrawModeSegment(g, RectF(rect.X + 3, rect.Y + 3, w, rect.Height - 6), T(L"standard"), active == InputBackend::SendInputBackend, [action]() { action(InputBackend::SendInputBackend); });
        DrawModeSegment(g, RectF(rect.X + 4 + w, rect.Y + 3, w, rect.Height - 6), T(L"game_mode"), active == InputBackend::GameCompatibleBackend, [action]() { action(InputBackend::GameCompatibleBackend); });
        DrawModeSegment(g, RectF(rect.X + 5 + w * 2, rect.Y + 3, w, rect.Height - 6), T(L"legacy"), active == InputBackend::LegacyMouseEvent, [action]() { action(InputBackend::LegacyMouseEvent); });
    }

    void DrawStepper(Graphics& g,
                     const RectF& rect,
                     const std::wstring& label,
                     int value,
                     std::function<void(int)> update,
                     int step = 1,
                     const std::wstring& suffix = std::wstring(),
                     NumberTarget target = NumberTarget::None) {
        DrawLabel(g, RectF(rect.X, rect.Y, rect.Width, 20), label);
        RectF box(rect.X, rect.Y + 24, rect.Width, 34);
        FillRound(g, box, 11.0f, Rgb(18, 14, 28));
        StrokeRound(g, box, 11.0f, Rgb(91, 63, 121, 140));

        RectF minus(box.X + 4, box.Y + 4, 44, box.Height - 8);
        RectF plus(box.X + box.Width - 48, box.Y + 4, 44, box.Height - 8);
        DrawTinyButton(g, minus, L"-", [update, step]() { update(-step); });
        DrawTinyButton(g, plus, L"+", [update, step]() { update(step); });
        RectF valueRect(box.X + 58, box.Y, box.Width - 116, box.Height);
        if (target != NumberTarget::None && IsNumberTarget(target)) {
            FillRound(g, RectF(valueRect.X + 4, valueRect.Y + 4, valueRect.Width - 8, valueRect.Height - 8), 8.0f, Rgb(76, 45, 118, 210));
        }
        DrawText(g, NumberText(target, value, suffix), valueRect, 17.0f, Rgb(255, 252, 255), FontStyleBold, StringAlignmentCenter);
        if (target != NumberTarget::None) {
            hitAreas_.push_back({valueRect, [this, target, value]() {
                StartNumberEdit(target, value);
            }});
        }
    }

    void DrawTinyButton(Graphics& g, const RectF& rect, const std::wstring& label, std::function<void()> action) {
        const bool hover = Contains(rect, hoverX_, hoverY_);
        FillRound(g, rect, 8.0f, hover ? Rgb(73, 47, 112) : Rgb(36, 28, 52));
        DrawText(g, label, rect, 20.0f, Rgb(242, 231, 255), FontStyleBold, StringAlignmentCenter);
        hitAreas_.push_back({rect, std::move(action)});
    }

    void DrawStatusPill(Graphics& g, const RectF& rect, bool running) {
        FillRound(g, rect, 12.0f, running ? Rgb(44, 155, 117, 210) : Rgb(63, 49, 79, 210));
        DrawText(g, running ? T(L"running") : T(L"stopped"), RectF(rect.X + 8, rect.Y, rect.Width - 16, rect.Height), 12.0f, Rgb(255, 255, 255), FontStyleBold, StringAlignmentCenter);
    }

    void DrawLanguageCard(Graphics& g, const RectF& rect, Language lang, const std::wstring& label) {
        const bool active = language_ == lang;
        const bool hover = Contains(rect, hoverX_, hoverY_);
        LinearGradientBrush brush(PointF(rect.X, rect.Y), PointF(rect.X + rect.Width, rect.Y + rect.Height),
                                  active ? Rgb(128, 70, 241) : (hover ? Rgb(47, 33, 68) : Rgb(24, 19, 35)),
                                  active ? Rgb(215, 81, 255) : Rgb(15, 13, 24));
        FillRound(g, rect, 14.0f, brush);
        StrokeRound(g, rect, 14.0f, active ? Rgb(243, 211, 255, 220) : Rgb(91, 63, 121, 140), active ? 2.0f : 1.0f);
        DrawText(g, label, RectF(rect.X + 20, rect.Y, rect.Width - 40, rect.Height), 17.0f, Rgb(250, 246, 255), active ? FontStyleBold : FontStyleRegular, StringAlignmentCenter);

        hitAreas_.push_back({rect, [this, lang]() {
            language_ = lang;
            ApplyWindowOptions();
            SaveConfig();
        }});
    }

    void DrawToggleRow(Graphics& g, const RectF& rect, const std::wstring& label, bool active, std::function<void()> action) {
        DrawText(g, label, RectF(rect.X, rect.Y, rect.Width - 78, rect.Height), 16.0f, Rgb(236, 225, 252), FontStyleBold);
        RectF toggle(rect.X + rect.Width - 62, rect.Y + 9, 58, 30);
        FillRound(g, toggle, 15.0f, active ? Rgb(125, 70, 232) : Rgb(44, 35, 59));
        const float knobX = active ? toggle.X + toggle.Width - 27 : toggle.X + 3;
        FillRound(g, RectF(knobX, toggle.Y + 3, 24, 24), 12.0f, Rgb(255, 250, 255));
        hitAreas_.push_back({rect, std::move(action)});
    }

    void DrawStatus(Graphics& g, int width, int height) {
        std::wstring text = statusText_.empty() ? T(L"status_ready") : statusText_;
        RectF status(static_cast<float>(width - 250), static_cast<float>(height - 42), 220, 26);
        FillRound(g, status, 13.0f, Rgb(22, 16, 33, 225));
        StrokeRound(g, status, 13.0f, Rgb(98, 63, 139, 140));
        DrawText(g, text, RectF(status.X + 14, status.Y, status.Width - 28, status.Height), 12.0f, Rgb(190, 171, 213), FontStyleBold, StringAlignmentCenter);
    }

    void ResetDefaults() {
        const Language currentLanguage = language_;
        const bool currentAutoSave = autoSave_;
        const std::wstring activeConfig = currentConfig_;
        leftClick_ = MouseClickConfig{};
        rightClick_ = MouseClickConfig{};
        rightClick_.hotkey = VK_F7;
        keyboard_ = KeyboardConfig{};
        language_ = currentLanguage;
        autoSave_ = currentAutoSave;
        currentConfig_ = activeConfig;
        alwaysOnTop_ = false;
        captureTarget_ = BindTarget::None;
        numberTarget_ = NumberTarget::None;
        numberBuffer_.clear();
        numberSelectAll_ = false;
        StopAllAutomation();
        SetStatus(T(L"status_ready"));
    }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    Page page_ = Page::Mouse;
    Language language_ = Language::English;
    MouseClickConfig leftClick_;
    MouseClickConfig rightClick_;
    KeyboardConfig keyboard_;
    MouseButton activeMouseButton_ = MouseButton::Left;
    BindTarget captureTarget_ = BindTarget::None;
    NumberTarget numberTarget_ = NumberTarget::None;
    std::wstring numberBuffer_;
    bool numberSelectAll_ = false;
    bool alwaysOnTop_ = false;
    bool autoSave_ = true;
    std::wstring currentConfig_ = L"Default";
    std::mt19937 rng_{std::random_device{}()};
    std::unique_ptr<Image> logoImage_;

    std::array<bool, 256> keyDown_ = {};
    std::vector<HitArea> hitAreas_;
    RECT leftButtonZone_ = {};
    RECT rightButtonZone_ = {};
    bool leftMouseHover_ = false;
    bool rightMouseHover_ = false;
    bool mouseInside_ = false;
    int hoverHitIndex_ = -1;
    float hoverX_ = -1.0f;
    float hoverY_ = -1.0f;
    std::wstring statusText_;
    double statusUntilMs_ = 0.0;
};

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_app) {
        const auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const bool injected = (info->flags & LLKHF_INJECTED) != 0;
        if (!injected) {
            const bool isDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            const bool isUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
            if ((isDown || isUp) && g_app->HandleGlobalKey(info->vkCode, isDown, isUp)) {
                return 1;
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_app) {
        const auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        const bool injected = (info->flags & LLMHF_INJECTED) != 0;
        if (!injected) {
            UINT vk = 0;
            bool isDown = false;
            bool isUp = false;

            switch (wParam) {
            case WM_LBUTTONDOWN:
                vk = VK_LBUTTON;
                isDown = true;
                break;
            case WM_LBUTTONUP:
                vk = VK_LBUTTON;
                isUp = true;
                break;
            case WM_RBUTTONDOWN:
                vk = VK_RBUTTON;
                isDown = true;
                break;
            case WM_RBUTTONUP:
                vk = VK_RBUTTON;
                isUp = true;
                break;
            case WM_MBUTTONDOWN:
                vk = VK_MBUTTON;
                isDown = true;
                break;
            case WM_MBUTTONUP:
                vk = VK_MBUTTON;
                isUp = true;
                break;
            case WM_XBUTTONDOWN:
                vk = HIWORD(info->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
                isDown = true;
                break;
            case WM_XBUTTONUP:
                vk = HIWORD(info->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
                isUp = true;
                break;
            default:
                break;
            }

            if (vk != 0 && g_app->HandleGlobalMouseButton(vk, isDown, isUp, info->pt)) {
                return 1;
            }
        }
    }
    return CallNextHookEx(g_mouseHook, code, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (app) {
        return app->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Ok) {
        return 1;
    }

    App app;
    const int result = app.Run(instance, showCommand);

    GdiplusShutdown(gdiplusToken);
    return result;
}
