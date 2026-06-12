/*
 * SiliconV — Main Window Controller (Implementation)
 */

#import "MainWindowController.h"
#import "VMViewController.h"
#import "ConsoleViewController.h"
#import "VMEngine.h"

/* ── Toolbar Item Identifiers ──────────────────── */

static NSString * const kToolbarStart    = @"Start";
static NSString * const kToolbarStop     = @"Stop";
static NSString * const kToolbarPause    = @"Pause";
static NSString * const kToolbarKernel   = @"Kernel";
static NSString * const kToolbarRootfs   = @"Rootfs";
static NSString * const kToolbarSettings = @"Settings";
static NSString * const kToolbarFlex     = @"Flex";

/* ── Internal Interface ────────────────────────── */

@interface MainWindowController () <NSToolbarDelegate, NSDraggingDestination, NSSplitViewDelegate>
@property (nonatomic, strong) NSWindow               *window;
@property (nonatomic, strong) VMViewController       *vmViewController;
@property (nonatomic, strong) ConsoleViewController  *consoleViewController;
@property (nonatomic, strong) VMEngine               *engine;
@property (nonatomic, strong) NSSplitView            *splitView;
@property (nonatomic, strong) NSView                 *sidebarView;
@property (nonatomic, strong) NSOutlineView          *deviceOutline;
@property (nonatomic, strong) NSTextField            *statusLabel;
@property (nonatomic, strong) NSToolbar              *toolbar;
@property (nonatomic, strong) NSToolbarItem          *startItem;
@property (nonatomic, strong) NSToolbarItem          *stopItem;
@property (nonatomic, strong) NSToolbarItem          *pauseItem;
@property (nonatomic, copy)   NSString               *kernelPath;
@property (nonatomic, copy, nullable) NSString       *rootfsPath;
@property (nonatomic, strong) VMConfig               *config;
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *deviceTree;
@property (nonatomic, strong) NSPanel                *settingsPanel;

/* Observers */
@property (nonatomic, strong) id stateObserver;
@property (nonatomic, strong) id outputObserver;
@end

/* ── Device Tree Model ─────────────────────────── */

static NSString * const kDeviceName = @"name";
static NSString * const kDeviceIcon = @"icon";
static NSString * const kDeviceEnabled = @"enabled";
static NSString * const kDeviceInfo = @"info";
static NSString * const kDeviceChildren = @"children";

@implementation MainWindowController

/* ── Init ──────────────────────────────────────── */

- (instancetype)init {
    self = [super init];
    if (self) {
        _config = [[VMConfig alloc] init];
        [self setupDeviceTree];
        [self setupWindow];
        [self setupObservers];
    }
    return self;
}

- (void)dealloc {
    if (_stateObserver) {
        [[NSNotificationCenter defaultCenter] removeObserver:_stateObserver];
    }
    if (_outputObserver) {
        [[NSNotificationCenter defaultCenter] removeObserver:_outputObserver];
    }
}

/* ── Device Tree Model ─────────────────────────── */

- (void)setupDeviceTree {
    _deviceTree = [NSMutableArray arrayWithArray:@[
        @{kDeviceName: @"UART (PL011)",     kDeviceIcon: @"terminal",       kDeviceEnabled: @YES,  kDeviceInfo: @"MMIO 0x10000000, IRQ 32"},
        @{kDeviceName: @"GICv3",            kDeviceIcon: @"bolt.shield",   kDeviceEnabled: @YES,  kDeviceInfo: @"0x08000000, 8 SPI lines"},
        @{kDeviceName: @"Virtio Devices",   kDeviceIcon: @"square.stack.3d.up", kDeviceEnabled: @YES, kDeviceInfo: @"MMIO Transport",
          kDeviceChildren: @[
              @{kDeviceName: @"virtio-blk",    kDeviceIcon: @"internaldrive", kDeviceEnabled: @NO,  kDeviceInfo: @"IRQ 40, MMIO 0x20000000"},
              @{kDeviceName: @"virtio-net",    kDeviceIcon: @"network",       kDeviceEnabled: @NO,  kDeviceInfo: @"IRQ 41, MMIO 0x20010000"},
              @{kDeviceName: @"virtio-gpu",    kDeviceIcon: @"display",       kDeviceEnabled: @NO,  kDeviceInfo: @"IRQ 43, MMIO 0x20030000"},
              @{kDeviceName: @"virtio-console",kDeviceIcon: @"text.alignleft",kDeviceEnabled: @NO,  kDeviceInfo: @"IRQ 44, MMIO 0x20040000"},
          ]},
        @{kDeviceName: @"PSCI",             kDeviceIcon: @"power",           kDeviceEnabled: @YES,  kDeviceInfo: @"CPU lifecycle"},
        @{kDeviceName: @"DTB Generator",    kDeviceIcon: @"doc.text",        kDeviceEnabled: @YES,  kDeviceInfo: @"Runtime DTB"},
    ]];
}

/* ── Window Setup ──────────────────────────────── */

- (void)setupWindow {
    /* Window */
    NSRect frame = NSMakeRect(0, 0, 1300, 780);
    NSUInteger styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                           NSWindowStyleMaskFullSizeContentView;

    _window = [[NSWindow alloc] initWithContentRect:frame
                                           styleMask:styleMask
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    _window.delegate = self;
    _window.title = @"SiliconV";
    _window.minSize = NSMakeSize(900, 500);
    _window.titlebarAppearsTransparent = YES;
    _window.backgroundColor = [NSColor controlBackgroundColor];
    _window.releasedWhenClosed = NO;

    /* Center */
    [_window center];

    /* ── Toolbar ────────────────────────────────── */
    _toolbar = [[NSToolbar alloc] initWithIdentifier:@"SiliconV.Toolbar"];
    _toolbar.delegate = self;
    _toolbar.displayMode = NSToolbarDisplayModeIconOnly;
    _toolbar.allowsUserCustomization = YES;
    _toolbar.autosavesConfiguration = YES;
    _window.toolbar = _toolbar;

    /* ── Content ────────────────────────────────── */
    NSView *contentView = _window.contentView;

    /* Split view: sidebar | VM display | console */
    _splitView = [[NSSplitView alloc] initWithFrame:contentView.bounds];
    _splitView.delegate = self;
    _splitView.vertical = YES;
    _splitView.dividerStyle = NSSplitViewDividerStyleThin;
    _splitView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [contentView addSubview:_splitView];

    [self setupSidebar];
    [self setupVMDisplay];
    [self setupConsole];

    /* Add subviews to split in order */
    [_splitView addSubview:_sidebarView];
    [_splitView addSubview:_vmViewController.view];
    [_splitView addSubview:_consoleViewController.consoleView];

    /* Set proportional widths */
    [_splitView setPosition:180 ofDividerAtIndex:0];
    [_splitView setPosition:740 ofDividerAtIndex:1];

    /* ── Status Bar ─────────────────────────────── */
    NSView *statusBar = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, 28)];
    statusBar.wantsLayer = YES;
    statusBar.layer.backgroundColor = [[NSColor controlBackgroundColor] CGColor];
    statusBar.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [contentView addSubview:statusBar];

    NSBox *topBorder = [[NSBox alloc] initWithFrame:NSMakeRect(0, 27, frame.size.width, 1)];
    topBorder.boxType = NSBoxSeparator;
    topBorder.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [statusBar addSubview:topBorder];

    _statusLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 4, frame.size.width - 24, 20)];
    _statusLabel.bezeled = NO;
    _statusLabel.drawsBackground = NO;
    _statusLabel.editable = NO;
    _statusLabel.selectable = NO;
    _statusLabel.font = [NSFont systemFontOfSize:11];
    _statusLabel.textColor = [NSColor secondaryLabelColor];
    _statusLabel.stringValue = @"Ready";
    _statusLabel.autoresizingMask = NSViewWidthSizable;
    [statusBar addSubview:_statusLabel];

    /* Adjust content for status bar */
    _splitView.frame = NSMakeRect(0, 28, frame.size.width, frame.size.height - 28);

    /* ── Menu Bar ───────────────────────────────── */
    [self buildMenu];
}

/* ── Sidebar Setup ─────────────────────────────── */

- (void)setupSidebar {
    _sidebarView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 180, 600)];
    _sidebarView.wantsLayer = YES;

    /* Visual effect background (frosted glass on macOS 11+) */
    NSVisualEffectView *effect = [[NSVisualEffectView alloc] initWithFrame:_sidebarView.bounds];
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.material = NSVisualEffectMaterialSidebar;
    effect.state = NSVisualEffectStateFollowsWindowActiveState;
    effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [_sidebarView addSubview:effect];

    /* Header */
    NSTextField *header = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 568, 156, 20)];
    header.bezeled = NO;
    header.drawsBackground = NO;
    header.editable = NO;
    header.selectable = NO;
    header.font = [NSFont systemFontOfSize:10 weight:NSFontWeightSemibold];
    header.textColor = [NSColor secondaryLabelColor];
    header.stringValue = @"DEVICES";
    [_sidebarView addSubview:header];

    /* Outline view for device tree */
    NSScrollView *outlineScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 180, 560)];
    outlineScroll.hasVerticalScroller = YES;
    outlineScroll.borderType = NSNoBorder;
    outlineScroll.drawsBackground = NO;
    outlineScroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    _deviceOutline = [[NSOutlineView alloc] initWithFrame:outlineScroll.contentView.bounds];
    _deviceOutline.headerView = nil;
    _deviceOutline.selectionHighlightStyle = NSTableViewSelectionHighlightStyleNone;

    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"Device"];
    col.width = 160;
    [_deviceOutline addTableColumn:col];

    _deviceOutline.dataSource = (id)self;
    _deviceOutline.delegate = (id)self;
    _deviceOutline.indentationPerLevel = 16;
    _deviceOutline.rowHeight = 24;
    _deviceOutline.backgroundColor = [NSColor clearColor];

    outlineScroll.documentView = _deviceOutline;
    [_sidebarView addSubview:outlineScroll];
}

/* ── VM Display Setup ──────────────────────────── */

- (void)setupVMDisplay {
    _vmViewController = [[VMViewController alloc] init];
    _vmViewController.engine = _engine;
}

/* ── Console Setup ─────────────────────────────── */

- (void)setupConsole {
    _consoleViewController = [[ConsoleViewController alloc] init];
}

/* ── Observers ─────────────────────────────────── */

- (void)setupObservers {
    __weak typeof(self) weakSelf = self;

    /* State changes */
    _stateObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:VMEngineStateDidChangeNotification
        object:nil
        queue:[NSOperationQueue mainQueue]
        usingBlock:^(NSNotification *note) {
            [weakSelf handleStateChange:note];
        }];

    /* Console output */
    _outputObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:VMEngineConsoleOutputNotification
        object:nil
        queue:[NSOperationQueue mainQueue]
        usingBlock:^(NSNotification *note) {
            [weakSelf handleConsoleOutput:note];
        }];
}

- (void)handleStateChange:(NSNotification *)note {
    VMState state = (VMState)[note.userInfo[@"state"] integerValue];

    dispatch_async(dispatch_get_main_queue(), ^{
        switch (state) {
            case VMStateIdle:
                self.statusLabel.stringValue = @"Ready";
                [self enableToolbarItems:YES];
                break;
            case VMStateStarting:
                self.statusLabel.stringValue = @"🔄 Starting VM...";
                [self enableToolbarItems:NO];
                break;
            case VMStateRunning:
                self.statusLabel.stringValue = [NSString stringWithFormat:@"🟢 VM Running | CPUs: %d | RAM: %d MB",
                                                 self.config.numCPUs, self.config.ramMB];
                [self updateToolbarForRunningState];
                break;
            case VMStatePaused:
                self.statusLabel.stringValue = @"⏸ VM Paused";
                [self updateToolbarForPausedState];
                break;
            case VMStateStopping:
                self.statusLabel.stringValue = @"⏹ Stopping VM...";
                [self enableToolbarItems:NO];
                break;
            case VMStateError:
                self.statusLabel.stringValue = @"✗ VM Error — check console";
                [self enableToolbarItems:YES];
                break;
        }
    });
}

- (void)handleConsoleOutput:(NSNotification *)note {
    id output = note.userInfo[@"output"];
    if ([output isKindOfClass:[NSNumber class]]) {
        uint8_t byte = (uint8_t)[output unsignedCharValue];
        [_consoleViewController appendByte:byte];
    } else if ([output isKindOfClass:[NSString class]]) {
        [_consoleViewController appendString:(NSString *)output];
    }
}

/* ── Toolbar Management ────────────────────────── */

- (void)enableToolbarItems:(BOOL)enabled {
    _startItem.enabled = enabled;
}

- (void)updateToolbarForRunningState {
    _startItem.enabled = NO;
    _stopItem.enabled = YES;
    _pauseItem.enabled = YES;
}

- (void)updateToolbarForPausedState {
    _startItem.enabled = YES;
    _stopItem.enabled = YES;
    _pauseItem.enabled = NO;
}

/* ── VM Actions ────────────────────────────────── */

- (void)startVM {
    if (_engine && _engine.state == VMStateRunning) {
        [self showAlert:@"VM is already running."];
        return;
    }
    if (!_kernelPath) {
        [self showAlert:@"Please select a kernel image first."];
        return;
    }

    /* Create engine on first start */
    if (!_engine) {
        _engine = [[VMEngine alloc] init];
        _vmViewController.engine = _engine;
    }

    _config.kernelPath = _kernelPath;
    _config.rootfsPath = _rootfsPath;

    NSError *error = nil;
    if (![_engine startWithConfig:_config error:&error]) {
        [self showAlert:error.localizedDescription ?: @"Failed to start VM"];
    }

    /* Update device tree */
    [self updateDeviceStatus];
}

- (void)stopVM {
    [_engine stop];
}

- (void)pauseVM {
    if (_engine.paused) {
        [_engine resume];
    } else {
        [_engine pause];
    }
}

- (void)updateDeviceStatus {
    if (!_engine) return;

    /* Update virtio-blk status */
    NSMutableDictionary *virtioGroup = _deviceTree[2];
    NSMutableArray *children = [virtioGroup[kDeviceChildren] mutableCopy];

    NSMutableDictionary *blk = [children[0] mutableCopy];
    blk[kDeviceEnabled] = @(_rootfsPath.length > 0);

    NSMutableDictionary *net = [children[1] mutableCopy];
    net[kDeviceEnabled] = @YES;

    NSMutableDictionary *console = [children[3] mutableCopy];
    console[kDeviceEnabled] = @YES;

    children[0] = blk;
    children[1] = net;
    children[3] = console;
    virtioGroup[@"children"] = children;
    _deviceTree[2] = virtioGroup;

    [_deviceOutline reloadData];
}

/* ── File Browsing ─────────────────────────────── */

- (void)browseKernel {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"Select Kernel Image";
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"img"],
                                   [UTType typeWithFilenameExtension:@"bin"],
                                   [UTType typeWithFilenameExtension:@"elf"],
                                   [UTType typeWithFilenameExtension:@""]];
    panel.allowsMultipleSelection = NO;
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK && panel.URL) {
            self.kernelPath = panel.URL.path;
            self.config.kernelPath = self.kernelPath;
            self.statusLabel.stringValue = [NSString stringWithFormat:@"Kernel: %@", self.kernelPath.lastPathComponent];
        }
    }];
}

- (void)browseRootfs {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"Select Root Filesystem";
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"img"],
                                   [UTType typeWithFilenameExtension:@"qcow2"],
                                   [UTType typeWithFilenameExtension:@"raw"],
                                   [UTType typeWithFilenameExtension:@"iso"]];
    panel.allowsMultipleSelection = NO;
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK && panel.URL) {
            self.rootfsPath = panel.URL.path;
            self.config.rootfsPath = self.rootfsPath;
            [self updateDeviceStatus];
            self.statusLabel.stringValue = [NSString stringWithFormat:@"Kernel: %@ | Rootfs: %@",
                                            self.kernelPath.lastPathComponent ?: @"—",
                                            self.rootfsPath.lastPathComponent ?: @"—"];
        }
    }];
}

/* ── Settings Panel ────────────────────────────── */

- (void)showSettings {
    if (!_settingsPanel) {
        NSRect frame = NSMakeRect(0, 0, 400, 340);
        _settingsPanel = [[NSPanel alloc] initWithContentRect:frame
                                                     styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
        _settingsPanel.title = @"VM Configuration";
        _settingsPanel.becomesKeyOnlyIfNeeded = YES;

        [self buildSettingsPanel];
    }

    [_window beginSheet:_settingsPanel completionHandler:nil];
}

- (void)buildSettingsPanel {
    NSView *content = _settingsPanel.contentView;
    CGFloat y = 290;
    CGFloat xPad = 20;

    /* CPU Stepper */
    NSTextField *cpuLabel = [self makeLabel:@"vCPUs:" frame:NSMakeRect(xPad, y, 60, 20)];
    [content addSubview:cpuLabel];

    NSStepper *cpuStepper = [[NSStepper alloc] initWithFrame:NSMakeRect(80, y - 4, 20, 28)];
    cpuStepper.minValue = 1;
    cpuStepper.maxValue = 8;
    cpuStepper.integerValue = _config.numCPUs;
    cpuStepper.tag = 200;
    [content addSubview:cpuStepper];

    NSTextField *cpuValue = [self makeLabel:[NSString stringWithFormat:@"%d", _config.numCPUs]
                                      frame:NSMakeRect(110, y, 30, 20)];
    cpuValue.tag = 210;
    [content addSubview:cpuValue];

    y -= 35;

    /* RAM Slider */
    NSTextField *ramLabel = [self makeLabel:@"RAM (MB):" frame:NSMakeRect(xPad, y, 70, 20)];
    [content addSubview:ramLabel];

    NSSlider *ramSlider = [[NSSlider alloc] initWithFrame:NSMakeRect(90, y, 200, 20)];
    ramSlider.minValue = 512;
    ramSlider.maxValue = 16384;
    ramSlider.integerValue = _config.ramMB;
    ramSlider.tag = 300;
    ramSlider.target = self;
    ramSlider.action = @selector(ramSlid:);
    [content addSubview:ramSlider];

    NSTextField *ramValue = [self makeLabel:[NSString stringWithFormat:@"%d MB", _config.ramMB]
                                      frame:NSMakeRect(300, y, 80, 20)];
    ramValue.tag = 310;
    [content addSubview:ramValue];

    y -= 40;

    /* Cmdline */
    NSTextField *cmdLabel = [self makeLabel:@"Kernel Cmdline:" frame:NSMakeRect(xPad, y, 110, 20)];
    [content addSubview:cmdLabel];

    y -= 22;

    NSTextField *cmdField = [[NSTextField alloc] initWithFrame:NSMakeRect(xPad, y, 360, 70)];
    cmdField.stringValue = _config.cmdline ?: @"";
    cmdField.tag = 400;
    cmdField.font = [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    [content addSubview:cmdField];

    y -= 85;

    /* Dry Run checkbox */
    NSButton *dryRunCB = [[NSButton alloc] initWithFrame:NSMakeRect(xPad, y, 200, 22)];
    [dryRunCB setButtonType:NSButtonTypeSwitch];
    dryRunCB.title = @"Dry Run (validate without running vCPU)";
    dryRunCB.state = _config.dryRun ? NSControlStateValueOn : NSControlStateValueOff;
    dryRunCB.tag = 500;
    [content addSubview:dryRunCB];

    y -= 35;

    /* Apply / Cancel buttons */
    NSButton *applyBtn = [NSButton buttonWithTitle:@"Apply" target:self action:@selector(applySettings)];
    applyBtn.frame = NSMakeRect(290, y, 90, 24);
    applyBtn.bezelStyle = NSBezelStyleRounded;
    applyBtn.keyEquivalent = @"\r";
    [content addSubview:applyBtn];

    NSButton *cancelBtn = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(closeSettingsPanel)];
    cancelBtn.frame = NSMakeRect(200, y, 80, 24);
    cancelBtn.bezelStyle = NSBezelStyleRounded;
    [content addSubview:cancelBtn];
}

- (void)ramSlid:(NSSlider *)slider {
    NSTextField *label = [_settingsPanel.contentView viewWithTag:310];
    label.stringValue = [NSString stringWithFormat:@"%d MB", (int)slider.integerValue];
}

- (void)applySettings {
    NSStepper  *cpu     = [_settingsPanel.contentView viewWithTag:200];
    NSSlider   *ram     = [_settingsPanel.contentView viewWithTag:300];
    NSTextField *cmd    = [_settingsPanel.contentView viewWithTag:400];
    NSButton   *dry    = [_settingsPanel.contentView viewWithTag:500];

    _config.numCPUs  = (int)cpu.integerValue;
    _config.ramMB    = (int)ram.integerValue;
    _config.cmdline  = cmd.stringValue;
    _config.dryRun   = (dry.state == NSControlStateValueOn);

    [self closeSettingsPanel];
}

- (void)closeSettingsPanel {
    [_window endSheet:_settingsPanel];
    [_settingsPanel close];
}

- (NSTextField *)makeLabel:(NSString *)text frame:(NSRect)frame {
    NSTextField *label = [[NSTextField alloc] initWithFrame:frame];
    label.bezeled = NO;
    label.drawsBackground = NO;
    label.editable = NO;
    label.selectable = NO;
    label.font = [NSFont systemFontOfSize:12];
    label.stringValue = text;
    return label;
}

/* ── Menu ──────────────────────────────────────── */

- (void)buildMenu {
    NSMenu *mainMenu = [[NSMenu alloc] init];

    /* App Menu */
    NSMenu *appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"About SiliconV" action:@selector(showAbout) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit SiliconV" action:@selector(terminate:) keyEquivalent:@"q"];

    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    appItem.submenu = appMenu;
    [mainMenu addItem:appItem];

    /* File Menu */
    NSMenu *fileMenu = [[NSMenu alloc] init];
    [fileMenu addItemWithTitle:@"Open Kernel…" action:@selector(browseKernel) keyEquivalent:@"o"];
    [fileMenu addItemWithTitle:@"Open Rootfs…" action:@selector(browseRootfs) keyEquivalent:@"r"];
    [[fileMenu addItemWithTitle:@"Clear Console" action:@selector(clearConsole) keyEquivalent:@"k"] setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagShift];
    [mainMenu addItemWithTitle:@"File" action:NULL keyEquivalent:@""].submenu = fileMenu;

    /* VM Menu */
    NSMenu *vmMenu = [[NSMenu alloc] init];
    [vmMenu addItemWithTitle:@"Start VM" action:@selector(startVM) keyEquivalent:@"s"];
    [[vmMenu addItemWithTitle:@"Stop VM" action:@selector(stopVM) keyEquivalent:@"."] setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [vmMenu addItemWithTitle:@"Pause / Resume" action:@selector(pauseVM) keyEquivalent:@"p"];
    [mainMenu addItemWithTitle:@"VM" action:NULL keyEquivalent:@""].submenu = vmMenu;

    /* Window Menu */
    NSMenu *winMenu = [[NSMenu alloc] init];
    [winMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [mainMenu addItemWithTitle:@"Window" action:NULL keyEquivalent:@""].submenu = winMenu;

    [NSApp setMainMenu:mainMenu];
}

- (void)clearConsole {
    [_consoleViewController clear];
}

- (void)showAbout {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"SiliconV";
    alert.informativeText = @"Virtual Phone Hardware Platform\nVersion 0.1\n\nA hypervisor platform for running AOSP Android in a virtual machine.\nSupports KVM (Linux) and HVF (macOS).";
    alert.icon = [NSImage imageNamed:@"NSApplicationIcon"];
    [alert runModal];
}

- (void)showAlert:(NSString *)message {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"SiliconV";
    alert.informativeText = message;
    [alert runModal];
}

/* ── NSToolbarDelegate ─────────────────────────── */

- (nullable NSToolbarItem *)toolbar:(NSToolbar *)toolbar
              itemForItemIdentifier:(NSString *)itemIdentifier
          willBeInsertedIntoToolbar:(BOOL)flag {
    if ([itemIdentifier isEqualToString:kToolbarFlex]) {
        return [[NSToolbarItem alloc] initWithItemIdentifier:NSToolbarFlexibleSpaceItemIdentifier];
    }

    NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier];

    if ([itemIdentifier isEqualToString:kToolbarStart]) {
        item.label = @"Start";
        item.paletteLabel = @"Start VM";
        item.toolTip = @"Start the virtual machine";
        item.image = [NSImage imageWithSystemSymbolName:@"play.fill" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(startVM);
        _startItem = item;
    } else if ([itemIdentifier isEqualToString:kToolbarStop]) {
        item.label = @"Stop";
        item.paletteLabel = @"Stop VM";
        item.toolTip = @"Stop the virtual machine";
        item.image = [NSImage imageWithSystemSymbolName:@"stop.fill" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(stopVM);
        item.enabled = NO;
        _stopItem = item;
    } else if ([itemIdentifier isEqualToString:kToolbarPause]) {
        item.label = @"Pause";
        item.paletteLabel = @"Pause / Resume VM";
        item.toolTip = @"Pause or resume the virtual machine";
        item.image = [NSImage imageWithSystemSymbolName:@"pause.fill" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(pauseVM);
        item.enabled = NO;
        _pauseItem = item;
    } else if ([itemIdentifier isEqualToString:kToolbarKernel]) {
        item.label = @"Kernel";
        item.paletteLabel = @"Select Kernel";
        item.toolTip = @"Select a kernel image or Android boot.img";
        item.image = [NSImage imageWithSystemSymbolName:@"folder.badge.gearshape" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(browseKernel);
    } else if ([itemIdentifier isEqualToString:kToolbarRootfs]) {
        item.label = @"Rootfs";
        item.paletteLabel = @"Select Root Filesystem";
        item.toolTip = @"Select a root filesystem image";
        item.image = [NSImage imageWithSystemSymbolName:@"internaldrive" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(browseRootfs);
    } else if ([itemIdentifier isEqualToString:kToolbarSettings]) {
        item.label = @"Settings";
        item.paletteLabel = @"VM Configuration";
        item.toolTip = @"Configure CPUs, RAM, kernel command line";
        item.image = [NSImage imageWithSystemSymbolName:@"gearshape" accessibilityDescription:nil];
        item.target = self;
        item.action = @selector(showSettings);
    }

    return item;
}

- (NSArray<NSString *> *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {
    return @[kToolbarStart, kToolbarStop, kToolbarPause,
             kToolbarFlex,
             kToolbarKernel, kToolbarRootfs,
             kToolbarFlex,
             kToolbarSettings];
}

- (NSArray<NSString *> *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {
    return @[kToolbarStart, kToolbarStop, kToolbarPause,
             kToolbarKernel, kToolbarRootfs, kToolbarSettings,
             kToolbarFlex, NSToolbarFlexibleSpaceItemIdentifier];
}

/* ── NSSplitViewDelegate ───────────────────────── */

- (CGFloat)splitView:(NSSplitView *)splitView constrainMinCoordinate:(CGFloat)proposedMinimum ofSubviewAt:(NSInteger)dividerIndex {
    switch (dividerIndex) {
        case 0: return 140;   /* sidebar min */
        case 1: return 400;   /* VM display min */
        default: return proposedMinimum;
    }
}

- (CGFloat)splitView:(NSSplitView *)splitView constrainMaxCoordinate:(CGFloat)proposedMaximum ofSubviewAt:(NSInteger)dividerIndex {
    switch (dividerIndex) {
        case 0: return 280;   /* sidebar max */
        case 1: return proposedMaximum;
        default: return proposedMaximum;
    }
}

/* ── NSOutlineView DataSource ──────────────────── */

- (NSInteger)outlineView:(NSOutlineView *)outlineView numberOfChildrenOfItem:(nullable id)item {
    NSArray *arr = item ? item[kDeviceChildren] : _deviceTree;
    return arr ? [arr count] : 0;
}

- (id)outlineView:(NSOutlineView *)outlineView child:(NSInteger)index ofItem:(nullable id)item {
    NSArray *arr = item ? item[kDeviceChildren] : _deviceTree;
    return arr[index];
}

- (BOOL)outlineView:(NSOutlineView *)outlineView isItemExpandable:(id)item {
    return [item[kDeviceChildren] count] > 0;
}

/* ── NSOutlineView Delegate ────────────────────── */

- (NSView *)outlineView:(NSOutlineView *)outlineView viewForTableColumn:(nullable NSTableColumn *)tableColumn item:(id)item {
    NSTableCellView *cellView = [outlineView makeViewWithIdentifier:@"DeviceCell" owner:self];
    if (!cellView) {
        cellView = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 160, 24)];
        cellView.identifier = @"DeviceCell";

        NSImageView *iconView = [[NSImageView alloc] initWithFrame:NSMakeRect(2, 4, 14, 14)];
        iconView.imageScaling = NSImageScaleProportionallyDown;
        iconView.tag = 1;
        [cellView addSubview:iconView];

        NSTextField *label = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 2, 130, 20)];
        label.bezeled = NO;
        label.drawsBackground = NO;
        label.editable = NO;
        label.selectable = NO;
        label.font = [NSFont systemFontOfSize:12];
        label.tag = 2;
        [cellView addSubview:label];

        NSTextField *info = [[NSTextField alloc] initWithFrame:NSMakeRect(20, -12, 130, 14)];
        info.bezeled = NO;
        info.drawsBackground = NO;
        info.editable = NO;
        info.selectable = NO;
        info.font = [NSFont systemFontOfSize:9];
        info.textColor = [NSColor tertiaryLabelColor];
        info.tag = 3;
        [cellView addSubview:info];
    }

    NSImageView *iconView = [cellView viewWithTag:1];
    NSTextField *label    = [cellView viewWithTag:2];
    NSTextField *info     = [cellView viewWithTag:3];

    NSString *iconName = item[kDeviceIcon] ?: @"circle";
    iconView.image = [NSImage imageWithSystemSymbolName:iconName accessibilityDescription:nil];

    BOOL enabled = [item[kDeviceEnabled] boolValue];
    iconView.contentTintColor = enabled ? [NSColor systemGreenColor] : [NSColor tertiaryLabelColor];

    label.stringValue = item[kDeviceName];
    label.textColor = enabled ? [NSColor labelColor] : [NSColor secondaryLabelColor];

    info.stringValue = item[kDeviceInfo] ?: @"";
    info.hidden = !enabled;

    return cellView;
}

- (CGFloat)outlineView:(NSOutlineView *)outlineView heightOfRowByItem:(id)item {
    BOOL enabled = [item[kDeviceEnabled] boolValue];
    return enabled ? 36 : 24;
}

/* ── Drag & Drop ───────────────────────────────── */

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSPasteboard *pb = [sender draggingPasteboard];
    NSArray *types = [pb types];

    if ([types containsObject:NSPasteboardTypeFileURL]) {
        return NSDragOperationCopy;
    }
    return NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSPasteboard *pb = [sender draggingPasteboard];
    NSArray *items = [pb readObjectsForClasses:@[[NSURL class]]
                                       options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];

    for (NSURL *url in items) {
        NSString *path = url.path;
        NSString *ext = path.pathExtension.lowercaseString;

        if ([ext isEqualToString:@"img"] || [ext isEqualToString:@"qcow2"] ||
            [ext isEqualToString:@"raw"] || [ext isEqualToString:@"iso"]) {
            self.rootfsPath = path;
            self.config.rootfsPath = path;
        } else {
            self.kernelPath = path;
            self.config.kernelPath = path;
        }
    }

    [self updateDeviceStatus];
    self.statusLabel.stringValue = [NSString stringWithFormat:@"Kernel: %@ | Rootfs: %@",
                                    self.kernelPath.lastPathComponent ?: @"—",
                                    self.rootfsPath.lastPathComponent ?: @"—"];
    return YES;
}

/* ── NSWindowDelegate ──────────────────────────── */

- (void)windowWillClose:(NSNotification *)notification {
    [_engine stop];
}

- (void)windowDidBecomeKey:(NSNotification *)notification {
    /* Pass keyboard focus to the VM display */
    [_window makeFirstResponder:_vmViewController.view];
}

@end
