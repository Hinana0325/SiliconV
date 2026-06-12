/*
 * SiliconV — Main Window Controller
 *
 * The central hub of the Cocoa GUI. Manages:
 *   - Window creation and layout (NSSplitView three-column)
 *   - Toolbar (Start/Stop/Pause, File pickers, Settings)
 *   - Menu bar
 *   - Device sidebar
 *   - Drag & Drop
 *   - Status bar
 *
 * Coordinates:
 *   VMViewController       — VM display + keyboard
 *   ConsoleViewController  — Serial console tabs
 *   VMEngine               — VM lifecycle
 */

#import <Cocoa/Cocoa.h>

@class VMEngine;
@class VMViewController;
@class ConsoleViewController;

NS_ASSUME_NONNULL_BEGIN

@interface MainWindowController : NSObject <NSWindowDelegate>

@property (nonatomic, readonly) NSWindow *window;

@end

NS_ASSUME_NONNULL_END
