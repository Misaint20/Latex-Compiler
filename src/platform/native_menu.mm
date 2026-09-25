#import "platform/native_menu.h"

#import <Cocoa/Cocoa.h>

#include <functional>
#include <string>

// Native application menu rendered by macOS in the system menu bar (not inside
// the window). Menu items forward actions to the provided callback with a
// "menu.*" topic; the platform window translates that into a frontend event so
// the web page decides what to do based on its own view state.
namespace {

// Per-language labels for the native bar. Keys mirror the frontend i18n
// catalog (topbar.menu.*) so both layers change together.
struct MenuLabels {
    NSString* file;
    NSString* go;
    NSString* open_folder;
    NSString* home;
    NSString* chapters;
    NSString* diagrams;
    NSString* styles;
    NSString* assets;
    NSString* history;
    NSString* edit;
};

MenuLabels labels_for(const std::string& language) {
    if (language.rfind("en", 0) == 0) {
        return {@"File", @"Go", @"Open Project Folder…", @"Home", @"Chapters",
                @"Diagrams", @"Styles", @"Assets", @"History", @"Edit"};
    }
    if (language.rfind("pt", 0) == 0) {
        return {@"Arquivo", @"Ir", @"Abrir Pasta do Projeto…", @"Início", @"Capítulos",
                @"Diagramas", @"Estilos", @"Assets", @"Histórico", @"Editar"};
    }
    if (language.rfind("fr", 0) == 0) {
        return {@"Fichier", @"Aller", @"Ouvrir un dossier de projet…", @"Accueil", @"Chapitres",
                @"Diagrammes", @"Styles", @"Assets", @"Historique", @"Édition"};
    }
    if (language.rfind("de", 0) == 0) {
        return {@"Datei", @"Gehe zu", @"Projektordner öffnen…", @"Start", @"Kapitel",
                @"Diagramme", @"Stile", @"Assets", @"Verlauf", @"Bearbeiten"};
    }
    if (language.rfind("it", 0) == 0) {
        return {@"File", @"Vai", @"Apri cartella del progetto…", @"Home", @"Capitoli",
                @"Diagrammi", @"Stili", @"Asset", @"Cronologia", @"Modifica"};
    }
    if (language.rfind("ja", 0) == 0) {
        return {@"ファイル", @"移動", @"プロジェクトフォルダを開く…", @"ホーム", @"章",
                @"ダイアグラム", @"スタイル", @"アセット", @"履歴", @"編集"};
    }
    if (language.rfind("zh", 0) == 0) {
        return {@"文件", @"前往", @"打开项目文件夹…", @"主页", @"章节",
                @"图表", @"样式", @"资源", @"历史", @"编辑"};
    }
    if (language.rfind("ru", 0) == 0) {
        return {@"Файл", @"Переход", @"Открыть папку проекта…", @"Главная", @"Главы",
                @"Диаграммы", @"Стили", @"Ресурсы", @"История", @"Правка"};
    }
    if (language.rfind("uk", 0) == 0) {
        return {@"Файл", @"Перехід", @"Відкрити папку проєкту…", @"Головна", @"Розділи",
                @"Діаграми", @"Стилі", @"Ресурси", @"Історія", @"Правка"};
    }
    if (language.rfind("pl", 0) == 0) {
        return {@"Plik", @"Przejdź do", @"Otwórz folder projektu…", @"Start", @"Rozdziały",
                @"Diagramy", @"Style", @"Zasoby", @"Historia", @"Edycja"};
    }
    if (language.rfind("tr", 0) == 0) {
        return {@"Dosya", @"Git", @"Proje klasörünü aç…", @"Ana Sayfa", @"Bölümler",
                @"Diyagramlar", @"Stiller", @"Varlıklar", @"Geçmiş", @"Düzen"};
    }
    if (language.rfind("nl", 0) == 0) {
        return {@"Bestand", @"Ga naar", @"Projectmap openen…", @"Start", @"Hoofdstukken",
                @"Diagrammen", @"Stijlen", @"Middelen", @"Geschiedenis", @"Bewerk"};
    }
    if (language.rfind("ko", 0) == 0) {
        return {@"파일", @"이동", @"프로젝트 폴더 열기…", @"홈", @"챕터",
                @"다이어그램", @"스타일", @"에셋", @"기록", @"편집"};
    }
    if (language.rfind("vi", 0) == 0) {
        return {@"Tệp", @"Đi tới", @"Mở thư mục dự án…", @"Trang chủ", @"Chương",
                @"Sơ đồ", @"Kiểu", @"Tài nguyên", @"Lịch sử", @"Chỉnh sửa"};
    }
    return {@"Archivo", @"Ir", @"Abrir carpeta de proyecto…", @"Inicio", @"Capítulos",
            @"Diagramas", @"Estilos", @"Assets", @"Historial", @"Editar"};
}

std::function<void(const std::string&, const std::string&)>& command_sink() {
    static std::function<void(const std::string&, const std::string&)> sink;
    return sink;
}

NSMenuItem* project_toggle_item = nil;

void send(NSString* topic, NSString* payload) {
    if (command_sink()) {
        command_sink()(topic.UTF8String, payload.UTF8String);
    }
}

} // namespace

// Target/action shim: macOS menus need an object with a selector; this one
// carries the topic and payload it must forward.
@interface MenuCommandTarget : NSObject
@property(copy) NSString* topic;
@property(copy) NSString* payload;
- (void)fire:(id)sender;
@end

@implementation MenuCommandTarget
- (void)fire:(id)sender {
    (void)sender;
    send(self.topic, self.payload);
}
@end

namespace {

MenuCommandTarget* makeTarget(NSString* topic, NSString* payload) {
    MenuCommandTarget* target = [[MenuCommandTarget alloc] init];
    target.topic = topic;
    target.payload = payload;
    return target;
}

NSMenuItem* item(NSString* title, NSString* key, NSString* topic, NSString* payload) {
    NSMenuItem* entry = [[NSMenuItem alloc]
        initWithTitle:title
               action:@selector(fire:)
        keyEquivalent:key ?: @""];
    if (key) {
        [entry setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    }
    MenuCommandTarget* target = makeTarget(topic, payload);
    [entry setTarget:target];
    [entry setRepresentedObject:target]; // keeps the target alive
    return entry;
}

} // namespace

void platform_install_native_menu(
    const std::function<void(const std::string&, const std::string&)>& on_command) {
    command_sink() = on_command;

    NSMenu* menubar = [[NSMenu alloc] init];

    // App menu (name supplied by the system).
    NSMenuItem* app_item = [menubar addItemWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu* app_menu = [[NSMenu alloc] init];
    [app_item setSubmenu:app_menu];
    [app_menu addItemWithTitle:@"Acerca de Latex Compiler"
                        action:@selector(orderFrontStandardAboutPanel:)
                 keyEquivalent:@""];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItemWithTitle:@"Preferencias…"
                        action:@selector(terminate:)
                 keyEquivalent:@","];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItemWithTitle:@"Salir de Latex Compiler"
                        action:@selector(terminate:)
                 keyEquivalent:@"q"];

    // File menu.
    NSMenuItem* file_item = [menubar addItemWithTitle:@"Archivo" action:nil keyEquivalent:@""];
    NSMenu* file_menu = [[NSMenu alloc] initWithTitle:@"Archivo"];
    [file_item setSubmenu:file_menu];
    [file_menu addItem:item(@"Abrir carpeta de proyecto…", @"o", @"menu.openFolder", @"")];

    // Go menu: project-scoped entries, disabled until a project is open.
    NSMenuItem* go_item = [menubar addItemWithTitle:@"Ir" action:nil keyEquivalent:@""];
    NSMenu* go_menu = [[NSMenu alloc] initWithTitle:@"Ir"];
    [go_item setSubmenu:go_menu];
    [go_menu addItem:item(@"Inicio", @"1", @"menu.goTo", @"welcome")];
    [go_menu addItem:item(@"Capítulos", @"2", @"menu.goTo", @"chapters")];
    [go_menu addItem:item(@"Diagramas", @"3", @"menu.goTo", @"diagrams")];
    [go_menu addItem:item(@"Estilos", @"4", @"menu.goTo", @"styles")];
    [go_menu addItem:item(@"Assets", @"5", @"menu.goTo", @"assets")];
    [go_menu addItem:item(@"Historial", @"6", @"menu.goTo", @"history")];
    project_toggle_item = go_item;

    // Edit menu: the standard selectors give the webview text fields
    // clipboard shortcuts for free.
    NSMenuItem* edit_item = [menubar addItemWithTitle:@"Editar" action:nil keyEquivalent:@""];
    NSMenu* edit_menu = [[NSMenu alloc] initWithTitle:@"Editar"];
    [edit_item setSubmenu:edit_menu];
    [edit_menu addItemWithTitle:@"Deshacer" action:@selector(undo:) keyEquivalent:@"z"];
    [edit_menu addItemWithTitle:@"Rehacer" action:@selector(redo:) keyEquivalent:@"Z"];
    [edit_menu addItem:[NSMenuItem separatorItem]];
    [edit_menu addItemWithTitle:@"Cortar" action:@selector(cut:) keyEquivalent:@"x"];
    [edit_menu addItemWithTitle:@"Copiar" action:@selector(copy:) keyEquivalent:@"c"];
    [edit_menu addItemWithTitle:@"Pegar" action:@selector(paste:) keyEquivalent:@"v"];
    [edit_menu addItemWithTitle:@"Seleccionar todo"
                        action:@selector(selectAll:)
                 keyEquivalent:@"a"];

    menubar.autoenablesItems = NO;
    NSApp.mainMenu = menubar;
    platform_set_native_menu_language("es");
}

namespace {

NSMenuItem* entry_with_topic(NSMenu* menu, NSString* topic) {
    for (NSMenuItem* entry in menu.itemArray) {
        MenuCommandTarget* target =
            [entry.representedObject isKindOfClass:[MenuCommandTarget class]]
                ? static_cast<MenuCommandTarget*>(entry.representedObject)
                : nil;
        if (target != nil && [target.topic isEqualToString:topic]) {
            return entry;
        }
    }
    return nil;
}

} // namespace

void platform_set_native_menu_language(const std::string& language) {
    if (NSApp == nil || NSApp.mainMenu == nil) {
        return;
    }
    const MenuLabels labels = labels_for(language);
    NSMenu* menubar = NSApp.mainMenu;

    // Match by structure, not by current language: File is the first submenu
    // after the app menu; Go the second; Edit the third.
    if (menubar.itemArray.count < 4) {
        return;
    }
    NSMenuItem* file_item = menubar.itemArray[1];
    NSMenuItem* go_item = menubar.itemArray[2];
    NSMenuItem* edit_item = menubar.itemArray[3];

    file_item.title = labels.file;
    file_item.submenu.title = labels.file;
    NSMenuItem* open_item = entry_with_topic(file_item.submenu, @"menu.openFolder");
    if (open_item != nil) {
        open_item.title = labels.open_folder;
    }

    go_item.title = labels.go;
    go_item.submenu.title = labels.go;
    NSDictionary* go_titles = @{
        @"menu.goTo:welcome": labels.home,
        @"menu.goTo:chapters": labels.chapters,
        @"menu.goTo:diagrams": labels.diagrams,
        @"menu.goTo:styles": labels.styles,
        @"menu.goTo:assets": labels.assets,
        @"menu.goTo:history": labels.history,
    };
    for (NSMenuItem* entry in go_item.submenu.itemArray) {
        MenuCommandTarget* target =
            [entry.representedObject isKindOfClass:[MenuCommandTarget class]]
                ? static_cast<MenuCommandTarget*>(entry.representedObject)
                : nil;
        if (target == nil) {
            continue;
        }
        NSString* key = [NSString stringWithFormat:@"%@:%@", target.topic, target.payload];
        NSString* title = go_titles[key];
        if (title != nil) {
            entry.title = title;
        }
    }

    edit_item.title = labels.edit;
    edit_item.submenu.title = labels.edit;
}

void platform_set_project_menu_enabled(bool enabled) {
    project_toggle_item.enabled = enabled ? YES : NO;
}
