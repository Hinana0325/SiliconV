#import "AppDelegate.h"
#include "../../core/vm/machine.h"

static AppDelegate *g_app = NULL;

static void cocoa_uart_tx(uint8_t byte, void *opaque) {
    (void)opaque;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!g_app) return;
        char s[2] = {(char)byte, '\0'};
        NSAttributedString *as = [[NSAttributedString alloc]
            initWithString:@(s)
            attributes:@{
                NSFontAttributeName: [NSFont fontWithName:@"Menlo" size:13],
                NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.2 green:0.8 blue:0.2 alpha:1]
            }];
        [[g_app.consoleView textStorage] appendAttributedString:as];
        [g_app.consoleView scrollToEndOfDocument:nil];
    });
}

/* ─── Toolbar Identifiers ───────────────────── */
static NSString * const kToolbarStart    = @"com.siliconv.toolbar.start";
static NSString * const kToolbarStop     = @"com.siliconv.toolbar.stop";
static NSString * const kToolbarClear    = @"com.siliconv.toolbar.clear";
static NSString * const kToolbarConfig   = @"com.siliconv.toolbar.config";
static NSString * const kToolbarKernel   = @"com.siliconv.toolbar.kernel";
static NSString * const kToolbarRootfs   = @"com.siliconv.toolbar.rootfs";

@interface AppDelegate ()
@property (strong) dispatch_queue_t vmQueue;
@property (assign) sv_machine_t vm;
@property (strong) NSPopover *configPopover;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    (void)n;
    g_app = self;
    self.vmQueue = dispatch_queue_create("com.siliconv.vm", DISPATCH_QUEUE_SERIAL);
    self.numCPUs = 4;
    self.ramMB = 4096;
    [self buildMenu];
    [self setupWindow];
}

/* ─── Menu Bar ───────────────────────────────── */
- (void)buildMenu {
    NSMenu *main = [[NSMenu alloc] init];

    // App menu
    NSString *appName = NSLocalizedString(@"menu.apple", @"App menu name");
    NSMenuItem *appItem = [main addItemWithTitle:appName action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:NSLocalizedString(@"menu.about", @"About menu")
                       action:@selector(showAbout:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:NSLocalizedString(@"menu.quit", @"Quit menu")
                       action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];

    // File menu
    NSMenuItem *fileItem = [main addItemWithTitle:NSLocalizedString(@"menu.file", @"File menu")
                                          action:nil keyEquivalent:@""];
    NSMenu *fileMenu = [[NSMenu alloc] init];
    [fileMenu addItemWithTitle:NSLocalizedString(@"menu.open.kernel", @"Open Kernel menu")
                        action:@selector(browseKernel:) keyEquivalent:@"o"];
    [fileMenu addItemWithTitle:NSLocalizedString(@"menu.open.rootfs", @"Open Rootfs menu")
                        action:@selector(browseRootfs:) keyEquivalent:@"r"];
    [fileItem setSubmenu:fileMenu];

    // VM menu
    NSMenuItem *vmItem = [main addItemWithTitle:NSLocalizedString(@"menu.vm", @"VM menu")
                                        action:nil keyEquivalent:@""];
    NSMenu *vmMenu = [[NSMenu alloc] init];
    [vmMenu addItemWithTitle:NSLocalizedString(@"menu.start", @"Start menu")
                      action:@selector(startVM:) keyEquivalent:@""];
    [vmMenu addItemWithTitle:NSLocalizedString(@"menu.stop", @"Stop menu")
                      action:@selector(stopVM:) keyEquivalent:@"."];
    [vmItem setSubmenu:vmMenu];

    // Window menu
    NSMenuItem *winItem = [main addItemWithTitle:NSLocalizedString(@"menu.window", @"Window menu")
                                         action:nil keyEquivalent:@""];
    NSMenu *winMenu = [[NSMenu alloc] init];
    [winMenu addItemWithTitle:NSLocalizedString(@"menu.minimize", @"Minimize menu")
                       action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [winItem setSubmenu:winMenu];

    NSApp.mainMenu = main;
}

/* ─── Window + Toolbar ───────────────────────── */
- (void)setupWindow {
    NSRect frame = NSMakeRect(0, 0, 920, 720);
    self.window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|
                NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    [self.window setTitle:@"SiliconV"];
    [self.window setMinSize:NSMakeSize(640, 480)];
    [self.window center];

    // Toolbar
    NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"siliconv.main"];
    [toolbar setDelegate:self];
    [toolbar setDisplayMode:NSToolbarDisplayModeIconAndLabel];
    [toolbar setSizeMode:NSToolbarSizeModeRegular];
    [toolbar setAllowsUserCustomization:NO];
    [self.window setToolbar:toolbar];

    // Main content: split config panel + console
    NSView *content = [self.window contentView];

    // ── Config panel (top area, collapsible) ──
    NSView *configPanel = [[NSView alloc] initWithFrame:NSMakeRect(0, 660, 920, 60)];
    [configPanel setWantsLayer:YES];
    [[configPanel layer] setBackgroundColor:[[NSColor colorWithSRGBRed:0.12 green:0.12 blue:0.18 alpha:1] CGColor]];
    [configPanel setIdentifier:@"configPanel"];

    // Kernel path
    NSTextField *kp = [[NSTextField alloc] initWithFrame:NSMakeRect(10, 32, 640, 22)];
        [kp setPlaceholderString:NSLocalizedString(@"field.kernel.placeholder", @"Kernel placeholder")];
    [kp setBezeled:YES];
    [kp setFont:[NSFont systemFontOfSize:12]];
    [kp setDrawsBackground:YES];
    [kp setBackgroundColor:[NSColor colorWithSRGBRed:0.08 green:0.08 blue:0.12 alpha:1]];
    [kp setTextColor:[NSColor whiteColor]];
    [kp setTarget:self];
    [kp setAction:@selector(kernelFieldEdited:)];
    [kp setTag:100];
    [configPanel addSubview:kp];

    // Rootfs path
    NSTextField *rp = [[NSTextField alloc] initWithFrame:NSMakeRect(10, 6, 640, 22)];
    [rp setPlaceholderString:NSLocalizedString(@"field.rootfs.placeholder", @"Rootfs placeholder")];
    [rp setBezeled:YES];
    [rp setFont:[NSFont systemFontOfSize:12]];
    [rp setDrawsBackground:YES];
    [rp setBackgroundColor:[NSColor colorWithSRGBRed:0.08 green:0.08 blue:0.12 alpha:1]];
    [rp setTextColor:[NSColor whiteColor]];
    [rp setTag:101];
    [configPanel addSubview:rp];

    [content addSubview:configPanel];

    // ── Console ──
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 24, 920, 660)];
    [scroll setHasVerticalScroller:YES];
    [scroll setHasHorizontalScroller:NO];
    [scroll setAutohidesScrollers:YES];
    [scroll setBorderType:NSNoBorder];
    [scroll setAutoresizingMask:NSViewWidthSizable|NSViewHeightSizable];

    // Scroll to bottom when console resizes
    [scroll setPostsBoundsChangedNotifications:YES];

    self.consoleView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 900, 660)];
    [self.consoleView setMinSize:NSMakeSize(0, 660)];
    [self.consoleView setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
    [self.consoleView setVerticallyResizable:YES];
    [self.consoleView setHorizontallyResizable:NO];
    [self.consoleView setAutoresizingMask:NSViewWidthSizable|NSViewHeightSizable];
    [self.consoleView setBackgroundColor:[NSColor colorWithSRGBRed:0.06 green:0.06 blue:0.1 alpha:1]];
    [self.consoleView setTextColor:[NSColor colorWithSRGBRed:0.2 green:0.8 blue:0.2 alpha:1]];
    [self.consoleView setFont:[NSFont fontWithName:@"Menlo" size:13]];
    [self.consoleView setEditable:NO];
    [self.consoleView setSelectable:YES];
    [scroll setDocumentView:self.consoleView];
    [content addSubview:scroll];

    // ── Status bar ──
    NSView *statusBar = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 920, 24)];
    [statusBar setWantsLayer:YES];
    [[statusBar layer] setBackgroundColor:[[NSColor colorWithSRGBRed:0.1 green:0.1 blue:0.15 alpha:1] CGColor]];
    [statusBar setAutoresizingMask:NSViewWidthSizable|NSViewMinYMargin];

    self.statusLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(10, 2, 900, 20)];
    [self.statusLabel setStringValue:NSLocalizedString(@"status.ready", @"Status: Ready")];
    [self.statusLabel setBezeled:NO];
    [self.statusLabel setDrawsBackground:NO];
    [self.statusLabel setTextColor:[NSColor lightGrayColor]];
    [self.statusLabel setEditable:NO];
    [self.statusLabel setFont:[NSFont systemFontOfSize:11]];
    [self.statusLabel setAutoresizingMask:NSViewWidthSizable];
    [statusBar addSubview:self.statusLabel];
    [content addSubview:statusBar];

    // ── Register drag-drop ──
    [self.window registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    [self.window setDelegate:self];

    [self.window makeKeyAndOrderFront:nil];
}

/* ─── NSToolbar Delegate ─────────────────────── */
- (NSToolbarItem *)toolbar:(NSToolbar *)t
     itemForItemIdentifier:(NSString *)id
 willBeInsertedIntoToolbar:(BOOL)flag {
    (void)t; (void)flag;

    if ([id isEqual:kToolbarStart]) {
        NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:id];
        [item setLabel:NSLocalizedString(@"toolbar.start", @"Toolbar: Start")];
        [item setImage:[NSImage imageNamed:NSImageNameTouchBarPlayTemplate]];
        [item setTarget:self];
        [item setAction:@selector(startVM:)];
        self.startItem = item;
        return item;
    }
    if ([id isEqual:kToolbarStop]) {
        NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:id];
        [item setLabel:NSLocalizedString(@"toolbar.stop", @"Toolbar: Stop")];
        [item setImage:[NSImage imageNamed:NSImageNameStopProgressTemplate]];
        [item setTarget:self];
        [item setAction:@selector(stopVM:)];
        [item setEnabled:NO];
        self.stopItem = item;
        return item;
    }
    if ([id isEqual:kToolbarClear]) {
        NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:id];
        [item setLabel:NSLocalizedString(@"toolbar.clear", @"Toolbar: Clear")];
        [item setImage:[NSImage imageNamed:NSImageNameTrashEmpty]];
        [item setTarget:self];
        [item setAction:@selector(clearConsole:)];
        return item;
    }
    if ([id isEqual:kToolbarConfig]) {
        NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:id];
        [item setLabel:NSLocalizedString(@"toolbar.configure", @"Toolbar: Configure")];
        [item setImage:[NSImage imageNamed:NSImageNameAdvanced]];
        [item setTarget:self];
        [item setAction:@selector(showConfig:)];
        return item;
    }
    if ([id isEqual:kToolbarKernel]) {
        NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:id];
        [item setLabel:NSLocalizedString(@"toolbar.kernel", @"Toolbar: Kernel…")];
        [item setImage:[NSImage imageNamed:NSImageNameFolder]];
        [item setTarget:self];
        [item setAction:@selector(browseKernel:)];
        return item;
    }
    if ([id isEqual:kToolbarRootfs]) {
        NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:id];
        [item setLabel:NSLocalizedString(@"toolbar.rootfs", @"Toolbar: Rootfs…")];
        [item setImage:[NSImage imageNamed:NSImageNameFolder]];
        [item setTarget:self];
        [item setAction:@selector(browseRootfs:)];
        return item;
    }
    return nil;
}

- (NSArray *)toolbarAllowedItemIdentifiers:(NSToolbar *)t {
    (void)t;
    return @[kToolbarStart, kToolbarStop,
             NSToolbarFlexibleSpaceItemIdentifier,
             kToolbarClear,
             NSToolbarFlexibleSpaceItemIdentifier,
             kToolbarKernel, kToolbarRootfs,
             NSToolbarFlexibleSpaceItemIdentifier,
             kToolbarConfig];
}

- (NSArray *)toolbarDefaultItemIdentifiers:(NSToolbar *)t {
    (void)t;
    return @[kToolbarStart, kToolbarStop,
             NSToolbarFlexibleSpaceItemIdentifier,
             kToolbarClear,
             NSToolbarFlexibleSpaceItemIdentifier,
             kToolbarKernel, kToolbarRootfs,
             NSToolbarFlexibleSpaceItemIdentifier,
             kToolbarConfig];
}

/* ─── Drag & Drop ────────────────────────────── */
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSPasteboard *pb = [sender draggingPasteboard];
    NSURL *url = [NSURL URLFromPasteboard:pb];
    if (!url) return NO;

    NSString *path = [url path];
    NSString *ext = [[path pathExtension] lowercaseString];
    BOOL isDir = NO;
    [[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&isDir];
    if (isDir) return NO;

    if ([ext isEqualToString:@"img"] || [ext isEqualToString:@"qcow2"] ||
        [ext isEqualToString:@"raw"] || [ext isEqualToString:@"iso"]) {
        self.rootfsPath = path;
        NSTextField *rf = [self.window.contentView viewWithTag:101];
        [rf setStringValue:path];
        [self log:[NSString stringWithFormat:NSLocalizedString(@"console.rootfs.set", @"Rootfs set"), path]];
    } else {
        self.kernelPath = path;
        NSTextField *kf = [self.window.contentView viewWithTag:100];
        [kf setStringValue:path];
        [self log:[NSString stringWithFormat:NSLocalizedString(@"console.kernel.set", @"Kernel set"), path]];
    }
    return YES;
}

/* ─── Actions ────────────────────────────────── */
- (void)browseKernel:(id)sender {
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:NSLocalizedString(@"menu.open.kernel", @"Open panel title")];
    if ([panel runModal] == NSModalResponseOK) {
        NSString *path = [[[panel URLs] firstObject] path];
        self.kernelPath = path;
        NSTextField *kf = [self.window.contentView viewWithTag:100];
        [kf setStringValue:path];
    }
}

- (void)browseRootfs:(id)sender {
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:NSLocalizedString(@"menu.open.rootfs", @"Open panel title")];
    if ([panel runModal] == NSModalResponseOK) {
        NSString *path = [[[panel URLs] firstObject] path];
        self.rootfsPath = path;
        NSTextField *rf = [self.window.contentView viewWithTag:101];
        [rf setStringValue:path];
    }
}

- (void)kernelFieldEdited:(id)sender {
    self.kernelPath = [(NSTextField *)sender stringValue];
}

- (void)showConfig:(id)sender {
    (void)sender;
    if (self.configPopover) {
        [self.configPopover close];
        self.configPopover = nil;
        return;
    }

    NSViewController *vc = [[NSViewController alloc] init];
    NSView *cv = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 280, 180)];
    [cv setWantsLayer:YES];
    [[cv layer] setBackgroundColor:[[NSColor colorWithSRGBRed:0.15 green:0.15 blue:0.22 alpha:1] CGColor]];

    // CPU stepper
    NSTextField *cpuLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 150, 80, 20)];
    [cpuLabel setStringValue:NSLocalizedString(@"config.cpus", @"CPUs label")];
    [cpuLabel setBezeled:NO];
    [cpuLabel setDrawsBackground:NO];
    [cpuLabel setTextColor:[NSColor whiteColor]];
    [cpuLabel setEditable:NO];
    [cpuLabel setFont:[NSFont systemFontOfSize:13]];
    [cv addSubview:cpuLabel];

    NSStepper *cpuStepper = [[NSStepper alloc] initWithFrame:NSMakeRect(200, 148, 20, 24)];
    [cpuStepper setMinValue:1];
    [cpuStepper setMaxValue:8];
    [cpuStepper setIntValue:self.numCPUs];
    [cpuStepper setTarget:self];
    [cpuStepper setAction:@selector(cpuStepped:)];
    [cpuStepper setTag:200];
    [cv addSubview:cpuStepper];

    NSTextField *cpuVal = [[NSTextField alloc] initWithFrame:NSMakeRect(160, 150, 36, 20)];
    [cpuVal setStringValue:[NSString stringWithFormat:@"%d", self.numCPUs]];
    [cpuVal setBezeled:NO];
    [cpuVal setDrawsBackground:NO];
    [cpuVal setTextColor:[NSColor whiteColor]];
    [cpuVal setEditable:NO];
    [cpuVal setAlignment:NSTextAlignmentRight];
    [cpuVal setFont:[NSFont systemFontOfSize:13]];
    [cpuVal setTag:201];
    [cv addSubview:cpuVal];

    // RAM slider
    NSTextField *ramLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 115, 80, 20)];
    [ramLabel setStringValue:NSLocalizedString(@"config.ram", @"RAM label")];
    [ramLabel setBezeled:NO];
    [ramLabel setDrawsBackground:NO];
    [ramLabel setTextColor:[NSColor whiteColor]];
    [ramLabel setEditable:NO];
    [ramLabel setFont:[NSFont systemFontOfSize:13]];
    [cv addSubview:ramLabel];

    NSSlider *ramSlider = [[NSSlider alloc] initWithFrame:NSMakeRect(80, 115, 150, 24)];
    [ramSlider setMinValue:512];
    [ramSlider setMaxValue:16384];
    [ramSlider setDoubleValue:self.ramMB];
    [ramSlider setAllowsTickMarkValuesOnly:YES];
    [ramSlider setNumberOfTickMarks:10];
    [ramSlider setTarget:self];
    [ramSlider setAction:@selector(ramSlid:)];
    [ramSlider setTag:202];
    [cv addSubview:ramSlider];

    NSTextField *ramVal = [[NSTextField alloc] initWithFrame:NSMakeRect(236, 115, 40, 20)];
    [ramVal setStringValue:[NSString stringWithFormat:@"%dM", self.ramMB]];
    [ramVal setBezeled:NO];
    [ramVal setDrawsBackground:NO];
    [ramVal setTextColor:[NSColor whiteColor]];
    [ramVal setEditable:NO];
    [ramVal setFont:[NSFont systemFontOfSize:11]];
    [ramVal setTag:203];
    [cv addSubview:ramVal];

    // Cmdline field
    NSTextField *cmdLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 80, 80, 20)];
    [cmdLabel setStringValue:NSLocalizedString(@"config.cmdline", @"Cmdline label")];
    [cmdLabel setBezeled:NO];
    [cmdLabel setDrawsBackground:NO];
    [cmdLabel setTextColor:[NSColor whiteColor]];
    [cmdLabel setEditable:NO];
    [cmdLabel setFont:[NSFont systemFontOfSize:13]];
    [cv addSubview:cmdLabel];

    NSTextField *cmdField = [[NSTextField alloc] initWithFrame:NSMakeRect(80, 80, 190, 22)];
    [cmdField setStringValue:@"console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw"];
    [cmdField setBezeled:YES];
    [cmdField setFont:[NSFont systemFontOfSize:11]];
    [cmdField setDrawsBackground:YES];
    [cmdField setBackgroundColor:[NSColor colorWithSRGBRed:0.08 green:0.08 blue:0.12 alpha:1]];
    [cmdField setTextColor:[NSColor whiteColor]];
    [cmdField setTag:204];
    [cv addSubview:cmdField];

    // Apply button
    NSButton *applyBtn = [[NSButton alloc] initWithFrame:NSMakeRect(180, 10, 90, 24)];
    [applyBtn setTitle:NSLocalizedString(@"config.apply", @"Apply button")];
    [applyBtn setBezelStyle:NSBezelStyleSmallSquare];
    [applyBtn setTarget:self];
    [applyBtn setAction:@selector(applyConfig:)];
    [cv addSubview:applyBtn];

    [vc setView:cv];

    self.configPopover = [[NSPopover alloc] init];
    [self.configPopover setContentViewController:vc];
    [self.configPopover setBehavior:NSPopoverBehaviorSemitransient];
    [self.configPopover showRelativeToRect:NSZeroRect
                                    ofView:[sender isKindOfClass:[NSView class]] ? sender : self.window.contentView
                             preferredEdge:NSRectEdgeMaxY];
}

- (void)cpuStepped:(id)sender {
    self.numCPUs = (int)[(NSStepper *)sender intValue];
    NSTextField *v = [self.window.contentView viewWithTag:201];
    [v setStringValue:[NSString stringWithFormat:@"%d", self.numCPUs]];
}

- (void)ramSlid:(id)sender {
    self.ramMB = (int)[(NSSlider *)sender doubleValue];
    NSTextField *v = [self.window.contentView viewWithTag:203];
    [v setStringValue:[NSString stringWithFormat:@"%dM", self.ramMB]];
}

- (void)applyConfig:(id)sender {
    (void)sender;
    [self.configPopover close];
    self.configPopover = nil;
    [self setStatus:[NSString stringWithFormat:NSLocalizedString(@"config.status", @"Config applied status"),
                     self.numCPUs, self.ramMB]];
}

- (void)clearConsole:(id)sender {
    (void)sender;
    [[self.consoleView textStorage] setAttributedString:[[NSAttributedString alloc] init]];
}

- (void)showAbout:(id)sender {
    (void)sender;
    NSString *ver = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"0.1";
    NSString *build = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"] ?: @"1";

    NSAttributedString *credits = [[NSAttributedString alloc]
        initWithString:NSLocalizedString(@"about.credits", @"About credits")
        attributes:@{NSFontAttributeName: [NSFont systemFontOfSize:11],
                     NSForegroundColorAttributeName: [NSColor grayColor]}];

    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:NSLocalizedString(@"about.title", @"About title")];
    [alert setInformativeText:[NSString stringWithFormat:NSLocalizedString(@"about.version", @"About version"),
                               ver, build]];
    [alert setAlertStyle:NSAlertStyleInformational];
    [alert accessoryView];
    [alert runModal];
}

/* ─── Console Output ─────────────────────────── */
- (void)log:(NSString *)text {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSAttributedString *as = [[NSAttributedString alloc]
            initWithString:text
            attributes:@{
                NSFontAttributeName: [NSFont fontWithName:@"Menlo" size:13],
                NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.3 green:0.8 blue:0.3 alpha:1]
            }];
        [[self.consoleView textStorage] appendAttributedString:as];
        [self.consoleView scrollToEndOfDocument:nil];
    });
}

- (void)setStatus:(NSString *)status {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.statusLabel setStringValue:status];
    });
}

/* ─── VM Lifecycle ───────────────────────────── */
- (void)startVM:(id)sender {
    (void)sender;
    if (self.vmRunning) return;

    if ([self.kernelPath length] == 0) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:NSLocalizedString(@"alert.no.kernel.title", @"Alert title")];
        [alert setInformativeText:NSLocalizedString(@"alert.no.kernel.message", @"Alert message")];
        [alert runModal];
        return;
    }

    [self.startItem setEnabled:NO];
    [self.stopItem setEnabled:YES];
    self.vmRunning = YES;
    [self setStatus:NSLocalizedString(@"status.starting", @"Status: starting")];

    dispatch_async(self.vmQueue, ^{
        [self runVM];
    });
}

- (void)runVM {
    @autoreleasepool {
        memset(&_vm, 0, sizeof(_vm));
        uint64_t ramBytes = (uint64_t)self.ramMB * 1024 * 1024;
        if (sv_machine_init(&_vm, self.numCPUs, ramBytes) < 0) {
            [self log:NSLocalizedString(@"console.fail.init", @"Init failed")];
            goto done;
        }

        pl011_set_tx_callback(&_vm.uart, cocoa_uart_tx, NULL);

        [self log:NSLocalizedString(@"console.header", @"Header")];
        [self log:[NSString stringWithFormat:NSLocalizedString(@"console.cpus", @"CPUs line"), self.numCPUs]];
        [self log:[NSString stringWithFormat:NSLocalizedString(@"console.ram", @"RAM line"), self.ramMB]];
        [self log:[NSString stringWithFormat:NSLocalizedString(@"console.kernel", @"Kernel line"), self.kernelPath]];
        if (self.rootfsPath)
            [self log:[NSString stringWithFormat:NSLocalizedString(@"console.rootfs", @"Rootfs line"), self.rootfsPath]];

        if (sv_machine_load_kernel(&_vm, [self.kernelPath UTF8String]) < 0) {
            [self log:NSLocalizedString(@"console.fail.kernel", @"Kernel load failed")];
            sv_machine_destroy(&_vm);
            goto done;
        }
        [self log:NSLocalizedString(@"console.kernel.loaded", @"Kernel loaded")];

        // Attach rootfs
        if (self.rootfsPath) {
            if (!sv_machine_attach_virtio_blk(&_vm, [self.rootfsPath UTF8String], false)) {
                [self log:NSLocalizedString(@"console.rootfs.attached", @"Rootfs attached")];
            } else {
                [self log:NSLocalizedString(@"console.fail.rootfs", @"Rootfs attach failed")];
            }
        }

        // Generate DTB
        [self log:NSLocalizedString(@"console.dtb.generating", @"Generating DTB")];
        uint8_t dtb_buf[16384];
        int dtb_size = dtb_generate(dtb_buf, sizeof(dtb_buf), &_vm.dtb_config);
        if (dtb_size <= 0) {
            [self log:NSLocalizedString(@"console.fail.dtb", @"DTB failed")];
            sv_machine_destroy(&_vm);
            goto done;
        }
        uint64_t dtb_off = 2 * 1024 * 1024;
        memcpy(_vm.ram + dtb_off, dtb_buf, dtb_size);
        _vm.dtb_addr = _vm.ram_base + dtb_off;
        [self log:[NSString stringWithFormat:NSLocalizedString(@"console.dtb.ok", @"DTB ok"),
                   dtb_size, (unsigned long long)_vm.dtb_addr]];

        [self setStatus:NSLocalizedString(@"status.running.demo", @"Status: demo")];

        _vm.running = true;
        [self log:NSLocalizedString(@"console.demo.header", @"Demo header")];
        [self log:NSLocalizedString(@"console.demo.mode", @"Demo mode")];
        [self log:[NSString stringWithFormat:NSLocalizedString(@"console.demo.boot", @"Demo boot"),
                   (unsigned long long)_vm.kernel_entry,
                   (unsigned long long)_vm.dtb_addr]];
        [self log:NSLocalizedString(@"console.demo.success", @"Demo success")];
        _vm.running = false;

        sv_machine_destroy(&_vm);
        [self log:NSLocalizedString(@"console.stopped", @"VM stopped")];

    done:
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.startItem setEnabled:YES];
            [self.stopItem setEnabled:NO];
            self.vmRunning = NO;
            [self setStatus:NSLocalizedString(@"status.ready", @"Status: ready")];
        });
    }
}

- (void)stopVM:(id)sender {
    (void)sender;
    _vm.running = false;
    [self log:NSLocalizedString(@"console.stop.requested", @"Stop requested")];
}

/* ─── Window Delegate ────────────────────────── */
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a {
    (void)a;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)n {
    (void)n;
    _vm.running = false;
    g_app = NULL;
}

@end
