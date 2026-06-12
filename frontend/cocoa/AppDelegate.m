/*
 * SiliconV — App Delegate (Implementation)
 */

#import "AppDelegate.h"
#import "MainWindowController.h"

@implementation AppDelegate {
    MainWindowController *_mainWC;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    _mainWC = [[MainWindowController alloc] init];
    [_mainWC.window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    _mainWC = nil;
}

@end
