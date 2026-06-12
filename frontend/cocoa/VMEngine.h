/*
 * SiliconV — VM Engine
 *
 * Thread-safe wrapper around the C machine API.
 * Manages VM lifecycle on a background queue.
 * Bridges UART output and state changes to the UI layer.
 *
 * Architecture:
 *   VMEngine (ObjC)  →  sv_machine_t (C)  →  KVM/HVF backend
 *        ↓                                    ↓
 *   ConsoleViewController                sv_machine_run()
 *   (dispatch on main queue)             (on background queue)
 */

#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

NS_ASSUME_NONNULL_BEGIN

/* ── VM State ──────────────────────────────────── */

typedef NS_ENUM(NSInteger, VMState) {
    VMStateIdle,
    VMStateStarting,
    VMStateRunning,
    VMStatePaused,
    VMStateStopping,
    VMStateError,
};

/* Notification names */
extern NSString * const VMEngineStateDidChangeNotification;
extern NSString * const VMEngineConsoleOutputNotification;

/* ── VM Configuration ──────────────────────────── */

@interface VMConfig : NSObject
@property (nonatomic, copy)     NSString *kernelPath;
@property (nonatomic, copy, nullable) NSString *rootfsPath;
@property (nonatomic, copy, nullable) NSString *dtbPath;
@property (nonatomic, copy, nullable) NSString *cmdline;
@property (nonatomic, assign)   int       numCPUs;
@property (nonatomic, assign)   int       ramMB;
@property (nonatomic, assign)   BOOL      dryRun;
@end

/* ── VM Engine ─────────────────────────────────── */

@interface VMEngine : NSObject

@property (nonatomic, readonly) VMState state;
@property (nonatomic, readonly) BOOL    running;
@property (nonatomic, readonly) BOOL    paused;
@property (nonatomic, readonly, nullable) VMConfig *config;

/* ── Lifecycle ────────────────────────────────── */

/// Start a VM with the given configuration.
/// Returns YES if startup sequence began successfully.
/// The VM runs on a background queue; subscribe to notifications for state updates.
- (BOOL)startWithConfig:(VMConfig *)config error:(NSError **)error;

/// Request graceful shutdown.
/// Sets the internal running flag and waits for the main loop to exit.
- (void)stop;

/// Pause the VM (set running=NO without destroying state).
/// May not be supported by all backends.
- (void)pause;

/// Resume a paused VM.
- (void)resume;

/* ── Input ────────────────────────────────────── */

/// Send a key press to the VM (via UART RX).
- (void)sendKey:(unichar)key;

/// Inject a raw byte into the guest UART.
- (void)sendByte:(uint8_t)byte;

@end

NS_ASSUME_NONNULL_END
