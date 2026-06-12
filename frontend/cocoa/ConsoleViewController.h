/*
 * SiliconV — Console View Controller
 *
 * Enhanced terminal console with tabbed views:
 *   - Kernel: Kernel boot messages (green-on-black, ANSI colors)
 *   - Android: Android init/service output
 *   - Logcat: Filtered Android log output
 *
 * Features:
 *   - ANSI escape code rendering (colors, bold)
 *   - Auto-scroll with manual lock
 *   - ⌘F search, ⌘C copy
 *   - Millisecond-precision timestamps
 *   - Line buffering for performance
 */

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

/* Console tab identifiers */
typedef NS_ENUM(NSInteger, ConsoleTab) {
    ConsoleTabKernel  = 0,
    ConsoleTabAndroid = 1,
    ConsoleTabLogcat  = 2,
};

@interface ConsoleViewController : NSViewController

/* ── Tab Control ──────────────────────────────── */

/// Segment control for switching between Kernel / Android / Logcat
@property (nonatomic, readonly) NSView *tabBar;

/// The main content area (scrollable text view)
@property (nonatomic, readonly) NSView *consoleView;

/* ── Output ───────────────────────────────────── */

/// Append a raw byte (from UART callback) to the active tab.
- (void)appendByte:(uint8_t)byte;

/// Append a formatted string to the active tab.
- (void)appendString:(NSString *)str;

/// Append a line with timestamp.
- (void)appendLine:(NSString *)line;

/// Append with ANSI color code support.
- (void)appendANSIEscapedLine:(NSString *)line;

/// Clear all tabs.
- (void)clear;

/* ── Search ───────────────────────────────────── */

/// Show/hide the search bar.
- (void)showSearchBar;
- (void)hideSearchBar;

@end

NS_ASSUME_NONNULL_END
