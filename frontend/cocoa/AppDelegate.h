#import <Cocoa/Cocoa.h>

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate,
                                    NSToolbarDelegate, NSDraggingDestination>
@property (strong) NSWindow *window;
@property (strong) NSTextView *consoleView;
@property (strong) NSToolbarItem *startItem;
@property (strong) NSToolbarItem *stopItem;
@property (strong) NSTextField *statusLabel;
@property (copy) NSString *kernelPath;
@property (copy) NSString *rootfsPath;
@property (assign) int numCPUs;
@property (assign) int ramMB;
@property (assign) BOOL vmRunning;
@end
