/*
 * SiliconV — VM View Controller
 *
 * The VM display area. Shows:
 *   - SiliconV brand splash when idle
 *   - Guest framebuffer when VM is running (Metal-backed, future)
 *   - Key forwarding to VM UART
 *
 * Architecture:
 *   VMDisplayView (NSView + CALayer)  ←  renders framebuffer
 *   VMViewController                  ←  manages keyboard input → VMEngine
 */

#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>

@class VMEngine;

NS_ASSUME_NONNULL_BEGIN

@interface VMViewController : NSViewController

/// The VM engine for keyboard forwarding. Set by MainWindowController.
@property (nonatomic, weak, nullable) VMEngine *engine;

/// The display view that renders the guest framebuffer.
@property (nonatomic, readonly) NSView *displayView;

/// Update the display with a new framebuffer.
/// For future virtio-gpu integration.
- (void)updateFramebuffer:(const uint8_t *)data
                     size:(CGSize)size
               pixelFormat:(OSType)format;

@end

NS_ASSUME_NONNULL_END
