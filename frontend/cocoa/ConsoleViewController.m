/*
 * SiliconV — Console View Controller (Implementation)
 */

#import "ConsoleViewController.h"

/* ANSI escape codes */
#define ANSI_RESET   @"\033[0m"
#define ANSI_BOLD    @"\033[1m"

/* ── Internal Tab Model ────────────────────────── */

@interface ConsoleTabModel : NSObject
@property (nonatomic, strong) NSTextStorage  *textStorage;
@property (nonatomic, assign) BOOL            autoScroll;
@property (nonatomic, assign) NSInteger       maxLines;
@property (nonatomic, strong) NSMutableString *lineBuffer;
@property (nonatomic, assign) BOOL            inEscape;
@property (nonatomic, strong) NSMutableString *escapeBuf;
@end

@implementation ConsoleTabModel

- (instancetype)init {
    self = [super init];
    if (self) {
        _textStorage = [[NSTextStorage alloc] init];
        _autoScroll  = YES;
        _maxLines    = 10000;
        _lineBuffer  = [NSMutableString stringWithCapacity:1024];
        _inEscape    = NO;
        _escapeBuf   = [NSMutableString stringWithCapacity:32];
    }
    return self;
}

@end

/* ── Console View ──────────────────────────────── */

@interface ConsoleTextView : NSTextView
@property (nonatomic, weak) ConsoleViewController *controller;
@end

/* ── Console View Controller Implementation ───── */

@interface ConsoleViewController () <NSSearchFieldDelegate>
@property (nonatomic, strong) NSSegmentedControl   *tabSegment;
@property (nonatomic, strong) NSScrollView         *scrollView;
@property (nonatomic, strong) ConsoleTextView      *textView;
@property (nonatomic, strong) NSSearchField        *searchField;
@property (nonatomic, strong) NSView               *searchBar;
@property (nonatomic, strong) NSMutableArray<ConsoleTabModel *> *tabs;
@property (nonatomic, assign) ConsoleTab            activeTab;
@property (nonatomic, strong) NSDateFormatter      *timeFormatter;
@property (nonatomic, assign) BOOL                  searchVisible;
@property (nonatomic, strong) NSTimer              *flushTimer;
@end

/* ── Font & Color Constants ────────────────────── */

static NSFont     *consoleFont   = nil;
static NSColor    *kernelFgColor = nil;
static NSColor    *kernelBgColor = nil;
static NSColor    *androidFgColor = nil;
static NSColor    *androidBgColor = nil;
static NSColor    *logcatErrorColor  = nil;
static NSColor    *logcatWarnColor   = nil;
static NSColor    *logcatInfoColor   = nil;
static NSColor    *logcatDebugColor  = nil;
static NSColor    *logcatVerboseColor = nil;

@implementation ConsoleViewController

/* ── Init ──────────────────────────────────────── */

+ (void)initialize {
    if (self == [ConsoleViewController class]) {
        consoleFont       = [NSFont fontWithName:@"Menlo" size:12] ?: [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
        kernelFgColor     = [NSColor colorWithCalibratedRed:0.0 green:1.0 blue:0.0 alpha:1.0];
        kernelBgColor     = [NSColor colorWithCalibratedRed:0.05 green:0.05 blue:0.08 alpha:1.0];
        androidFgColor    = [NSColor colorWithCalibratedRed:0.85 green:0.85 blue:0.85 alpha:1.0];
        androidBgColor    = [NSColor colorWithCalibratedRed:0.08 green:0.08 blue:0.12 alpha:1.0];
        logcatErrorColor   = [NSColor colorWithCalibratedRed:0.95 green:0.3 blue:0.25 alpha:1.0];
        logcatWarnColor    = [NSColor colorWithCalibratedRed:1.0 green:0.75 blue:0.1 alpha:1.0];
        logcatInfoColor    = [NSColor colorWithCalibratedRed:0.4 green:0.85 blue:0.4 alpha:1.0];
        logcatDebugColor   = [NSColor colorWithCalibratedRed:0.35 green:0.55 blue:0.85 alpha:1.0];
        logcatVerboseColor = [NSColor colorWithCalibratedRed:0.5 green:0.5 blue:0.5 alpha:1.0];
    }
}

- (instancetype)init {
    self = [super init];
    if (self) {
        [self setupTabs];
        [self setupTimeFormatter];
    }
    return self;
}

- (void)dealloc {
    [_flushTimer invalidate];
}

/* ── Tab Setup ─────────────────────────────────── */

- (void)setupTabs {
    _tabs = [NSMutableArray arrayWithCapacity:3];
    for (int i = 0; i < 3; i++) {
        [_tabs addObject:[[ConsoleTabModel alloc] init]];
    }
    _activeTab = ConsoleTabKernel;
}

- (void)setupTimeFormatter {
    _timeFormatter = [[NSDateFormatter alloc] init];
    _timeFormatter.dateFormat = @"HH:mm:ss.SSS";
}

/* ── Load View ─────────────────────────────────── */

- (void)loadView {
    NSView *container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 600)];
    container.wantsLayer = YES;
    self.view = container;

    /* ── Tab Bar (Segment Control) ─────────────── */
    _tabSegment = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
    _tabSegment.segmentCount = 3;
    [_tabSegment setLabel:@"Kernel"  forSegment:0];
    [_tabSegment setLabel:@"Android" forSegment:1];
    [_tabSegment setLabel:@"Logcat"  forSegment:2];
    _tabSegment.selectedSegment = 0;
    _tabSegment.segmentStyle = NSSegmentStyleCapsule;
    _tabSegment.target = self;
    _tabSegment.action = @selector(tabChanged:);
    _tabSegment.translatesAutoresizingMaskIntoConstraints = NO;
    [container addSubview:_tabSegment];

    /* ── Search Bar (hidden by default) ────────── */
    _searchBar = [[NSView alloc] initWithFrame:NSZeroRect];
    _searchBar.wantsLayer = YES;
    _searchBar.layer.backgroundColor = [[NSColor controlBackgroundColor] CGColor];
    _searchBar.hidden = YES;
    _searchBar.translatesAutoresizingMaskIntoConstraints = NO;

    _searchField = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    _searchField.placeholderString = @"Search console...";
    _searchField.delegate = self;
    _searchField.translatesAutoresizingMaskIntoConstraints = NO;
    [_searchBar addSubview:_searchField];

    NSButton *closeSearch = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"xmark.circle.fill" accessibilityDescription:nil]
                                               target:self
                                               action:@selector(hideSearchBar)];
    closeSearch.bordered = NO;
    closeSearch.bezelStyle = NSBezelStyleRegularSquare;
    closeSearch.translatesAutoresizingMaskIntoConstraints = NO;
    [_searchBar addSubview:closeSearch];

    [container addSubview:_searchBar];

    /* ── Console NSTextView ────────────────────── */
    NSSize contentSize = [NSScrollView contentSizeForFrameSize:NSMakeSize(400, 500)
                                        horizontalScrollClass:nil
                                          verticalScrollClass:[NSScroller class]
                                                     borderType:NSNoBorder
                                                    controlSize:NSControlSizeRegular
                                                     scrollerStyle:NSScrollerStyleOverlay];

    _scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, contentSize.width, contentSize.height)];
    _scrollView.hasVerticalScroller   = YES;
    _scrollView.hasHorizontalScroller = NO;
    _scrollView.autohidesScrollers    = YES;
    _scrollView.borderType            = NSNoBorder;
    _scrollView.drawsBackground       = NO;
    _scrollView.translatesAutoresizingMaskIntoConstraints = NO;

    _textView = [[ConsoleTextView alloc] initWithFrame:_scrollView.contentView.bounds];
    _textView.controller = self;
    _textView.minSize = NSMakeSize(0, contentSize.height);
    _textView.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    _textView.verticallyResizable = YES;
    _textView.horizontallyResizable = NO;
    _textView.editable = NO;
    _textView.selectable = YES;
    _textView.font = consoleFont;
    _textView.textColor = kernelFgColor;
    _textView.backgroundColor = kernelBgColor;
    _textView.insertionPointColor = kernelFgColor;
    _textView.drawsBackground = YES;
    _textView.usesFindBar = YES;
    _textView.automaticQuoteSubstitutionEnabled = NO;
    _textView.automaticDashSubstitutionEnabled = NO;
    _textView.automaticTextReplacementEnabled = NO;
    _textView.richText = NO;

    [_scrollView setDocumentView:_textView];
    [container addSubview:_scrollView];

    /* ── Layout ────────────────────────────────── */
    [NSLayoutConstraint activateConstraints:@[
        [_tabSegment.topAnchor    constraintEqualToAnchor:container.topAnchor constant:8],
        [_tabSegment.centerXAnchor constraintEqualToAnchor:container.centerXAnchor],
        [_tabSegment.widthAnchor  constraintEqualToConstant:280],

        [_searchBar.topAnchor    constraintEqualToAnchor:_tabSegment.bottomAnchor constant:4],
        [_searchBar.leadingAnchor  constraintEqualToAnchor:container.leadingAnchor],
        [_searchBar.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
        [_searchBar.heightAnchor   constraintEqualToConstant:30],

        [_searchField.leadingAnchor  constraintEqualToAnchor:_searchBar.leadingAnchor constant:8],
        [_searchField.trailingAnchor constraintEqualToAnchor:closeSearch.leadingAnchor constant:-4],
        [_searchField.centerYAnchor  constraintEqualToAnchor:_searchBar.centerYAnchor],

        [closeSearch.trailingAnchor constraintEqualToAnchor:_searchBar.trailingAnchor constant:-8],
        [closeSearch.centerYAnchor  constraintEqualToAnchor:_searchBar.centerYAnchor],
        [closeSearch.widthAnchor    constraintEqualToConstant:20],
        [closeSearch.heightAnchor   constraintEqualToConstant:20],

        [_scrollView.topAnchor    constraintEqualToAnchor:_searchBar.bottomAnchor constant:0],
        [_scrollView.leadingAnchor  constraintEqualToAnchor:container.leadingAnchor],
        [_scrollView.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
        [_scrollView.bottomAnchor   constraintEqualToAnchor:container.bottomAnchor],
    ]];

    /* Auto-flush line buffer every 50ms */
    _flushTimer = [NSTimer scheduledTimerWithTimeInterval:0.05
                                                   repeats:YES
                                                     block:^(NSTimer *timer) {
        [self flushActiveTabBuffer];
    }];
}

/* ── Tab Switching ─────────────────────────────── */

- (void)tabChanged:(NSSegmentedControl *)sender {
    _activeTab = (ConsoleTab)sender.selectedSegment;
    [self switchToTab:_activeTab];
}

- (void)switchToTab:(ConsoleTab)tab {
    _activeTab = tab;
    ConsoleTabModel *model = _tabs[tab];
    [_textView.layoutManager replaceTextStorage:model.textStorage];

    switch (tab) {
        case ConsoleTabKernel:
            _textView.textColor = kernelFgColor;
            _textView.backgroundColor = kernelBgColor;
            break;
        case ConsoleTabAndroid:
            _textView.textColor = androidFgColor;
            _textView.backgroundColor = androidBgColor;
            break;
        case ConsoleTabLogcat:
            _textView.textColor = [NSColor labelColor];
            _textView.backgroundColor = [NSColor textBackgroundColor];
            break;
    }

    /* Scroll to bottom */
    [self scrollToBottomIfAuto];
}

/* ── Append Bytes (UART callback path — called on main queue) ── */

- (void)appendByte:(uint8_t)byte {
    ConsoleTabModel *model = _tabs[ConsoleTabKernel];

    if (byte == '\n') {
        [model.lineBuffer appendFormat:@"\n"];
        [self flushTab:model];
    } else if (byte >= 0x20 && byte < 0x7F) {
        [model.lineBuffer appendFormat:@"%c", byte];
    } else if (byte == '\r') {
        /* CR — ignore, \n handles newline */
    } else if (byte == '\t') {
        [model.lineBuffer appendString:@"    "];
    } else if (byte == 0x1B) {
        /* ANSI escape start */
        model.inEscape = YES;
        [model.escapeBuf setString:@"\033"];
    } else {
        [model.lineBuffer appendFormat:@"[0x%02X]", byte];
    }
}

/* ── Append Strings ────────────────────────────── */

- (void)appendString:(NSString *)str {
    ConsoleTabModel *model = _tabs[_activeTab];
    [model.lineBuffer appendString:str];
}

- (void)appendLine:(NSString *)line {
    ConsoleTabModel *model = _tabs[_activeTab];
    NSString *timestamp = [_timeFormatter stringFromDate:[NSDate date]];
    [model.lineBuffer appendFormat:@"[%@] %@\n", timestamp, line];
    [self flushTab:model];
}

- (void)appendANSIEscapedLine:(NSString *)line {
    /* Simple ANSI color code handling — strip or convert to NSAttributedString */
    ConsoleTabModel *model = _tabs[_activeTab];
    NSString *timestamp = [_timeFormatter stringFromDate:[NSDate date]];
    [model.lineBuffer appendFormat:@"[%@] %@\n", timestamp, line];
    [self flushTab:model];
}

/* ── Line Buffering ────────────────────────────── */

- (void)flushActiveTabBuffer {
    ConsoleTabModel *model = _tabs[_activeTab];
    if (model.lineBuffer.length > 0 && [model.lineBuffer containsString:@"\n"]) {
        [self flushTab:model];
    }
}

- (void)flushTab:(ConsoleTabModel *)model {
    /* Append text to storage on main queue */
    dispatch_async(dispatch_get_main_queue(), ^{
        NSAttributedString *attr = [[NSAttributedString alloc] initWithString:model.lineBuffer
                                                                    attributes:@{
            NSFontAttributeName: consoleFont,
        }];
        [model.textStorage beginEditing];
        [model.textStorage appendAttributedString:attr];
        [model.textStorage endEditing];

        /* Trim old lines if exceeding max */
        [self trimTabIfNeeded:model];

        /* Clear buffer */
        [model.lineBuffer setString:@""];

        /* Auto-scroll */
        [self scrollToBottomIfAuto];
    });
}

- (void)trimTabIfNeeded:(ConsoleTabModel *)model {
    /* Count lines roughly */
    NSString *str = model.textStorage.string;
    NSUInteger lines = 0, length = str.length;
    for (NSUInteger i = 0; i < length; i++) {
        if ([str characterAtIndex:i] == '\n') lines++;
    }

    if (lines > model.maxLines) {
        /* Remove oldest 25% */
        NSUInteger linesToRemove = model.maxLines / 4;
        NSUInteger charCount = 0;
        for (NSUInteger i = 0; i < length && linesToRemove > 0; i++) {
            if ([str characterAtIndex:i] == '\n') linesToRemove--;
            charCount = i + 1;
        }
        [model.textStorage beginEditing];
        [model.textStorage deleteCharactersInRange:NSMakeRange(0, charCount)];
        [model.textStorage endEditing];
    }
}

- (void)scrollToBottomIfAuto {
    ConsoleTabModel *model = _tabs[_activeTab];
    if (!model.autoScroll) return;

    NSRange endRange = NSMakeRange(model.textStorage.length, 0);
    [_textView scrollRangeToVisible:endRange];
}

/* ── Clear ─────────────────────────────────────── */

- (void)clear {
    for (ConsoleTabModel *model in _tabs) {
        [model.lineBuffer setString:@""];
        [model.textStorage beginEditing];
        [model.textStorage deleteCharactersInRange:NSMakeRange(0, model.textStorage.length)];
        [model.textStorage endEditing];
    }
}

/* ── Search ────────────────────────────────────── */

- (void)showSearchBar {
    _searchBar.hidden = NO;
    _searchVisible = YES;
    [_searchField becomeFirstResponder];
}

- (void)hideSearchBar {
    _searchBar.hidden = YES;
    _searchVisible = NO;
    [self.view.window makeFirstResponder:_textView];
}

- (void)controlTextDidChange:(NSNotification *)obj {
    NSString *query = _searchField.stringValue;
    if (query.length == 0) return;

    ConsoleTabModel *model = _tabs[_activeTab];
    NSString *text = model.textStorage.string;
    NSRange found = [text rangeOfString:query options:NSCaseInsensitiveSearch];
    if (found.location != NSNotFound) {
        [_textView setSelectedRange:found];
        [_textView scrollRangeToVisible:found];
    }
}

/* ── Console Tab Bar ───────────────────────────── */

- (NSView *)tabBar {
    return _tabSegment;
}

- (NSView *)consoleView {
    return _scrollView;
}

@end

/* ── ConsoleTextView ───────────────────────────── */

@implementation ConsoleTextView

- (void)keyDown:(NSEvent *)event {
    /* ⌘F → show search bar */
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        NSString *chars = event.charactersIgnoringModifiers;
        if ([chars isEqualToString:@"f"]) {
            [self.controller showSearchBar];
            return;
        }
        if ([chars isEqualToString:@"c"]) {
            [self copy:self];
            return;
        }
    }

    /* ESC → hide search bar */
    if (event.keyCode == 53 && self.controller.searchVisible) {
        [self.controller hideSearchBar];
        return;
    }

    [super keyDown:event];
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

@end
