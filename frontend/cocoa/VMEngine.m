/*
 * SiliconV — VM Engine (Implementation)
 *
 * Bridges the C machine API to Objective-C.
 * Runs sv_machine_run() on a background dispatch queue so the UI stays responsive.
 * Forwards UART output and state changes to the main queue via notifications.
 */

#import "VMEngine.h"

/* C API headers */
#include "../../core/vm/machine.h"
#include "../../devices/uart/pl011.h"

/* ── Notification Names ────────────────────────── */

NSString * const VMEngineStateDidChangeNotification = @"VMEngineStateDidChange";
NSString * const VMEngineConsoleOutputNotification   = @"VMEngineConsoleOutput";

/* Notification userInfo keys */
static NSString * const kVMStateKey    = @"state";
static NSString * const kVMOutputKey   = @"output";
static NSString * const kVMErrorKey    = @"error";

/* ── VMConfig ──────────────────────────────────── */

@implementation VMConfig
- (instancetype)init {
    self = [super init];
    if (self) {
        _numCPUs  = 4;
        _ramMB    = 4096;
        _cmdline  = @"console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw";
        _dryRun   = NO;
    }
    return self;
}
@end

/* ── Internal State ────────────────────────────── */

@interface VMEngine ()
@property (nonatomic, assign) VMState  state;
@property (nonatomic, strong) VMConfig *config;
@property (nonatomic, strong) dispatch_queue_t vmQueue;
@property (nonatomic, assign) BOOL             stopRequested;
@end

@implementation VMEngine {
    sv_machine_t _machine;
    BOOL         _initialized;
}

/* ── Init ──────────────────────────────────────── */

- (instancetype)init {
    self = [super init];
    if (self) {
        _state          = VMStateIdle;
        _vmQueue        = dispatch_queue_create("com.siliconv.vm", DISPATCH_QUEUE_SERIAL);
        _stopRequested  = NO;
        _initialized    = NO;
    }
    return self;
}

- (void)dealloc {
    [self stop];
    if (_initialized) {
        sv_machine_destroy(&_machine);
        _initialized = NO;
    }
}

/* ── State Management ──────────────────────────── */

- (void)setState:(VMState)state {
    if (_state == state) return;

    VMState oldState = _state;
    _state = state;

    NSLog(@"SiliconV: state %ld → %ld", (long)oldState, (long)state);

    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter]
            postNotificationName:VMEngineStateDidChangeNotification
            object:self
            userInfo:@{
                kVMStateKey: @(state),
            }];
    });
}

- (BOOL)running { return _state == VMStateRunning; }
- (BOOL)paused  { return _state == VMStatePaused; }

/* ── UART Callback (C → ObjC bridge) ───────────── */

static void engine_uart_tx_callback(uint8_t byte, void *ctx) {
    VMEngine *engine = (__bridge VMEngine *)ctx;

    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter]
            postNotificationName:VMEngineConsoleOutputNotification
            object:engine
            userInfo:@{
                kVMOutputKey: @(byte),
            }];
    });
}

/* ── Notify Output ─────────────────────────────── */

- (void)notifyOutput:(NSString *)line {
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter]
            postNotificationName:VMEngineConsoleOutputNotification
            object:self
            userInfo:@{
                kVMOutputKey: line,
            }];
    });
}

/* ── Start VM ──────────────────────────────────── */

- (BOOL)startWithConfig:(VMConfig *)config error:(NSError **)error {
    if (self.state != VMStateIdle) {
        if (error) {
            *error = [NSError errorWithDomain:@"VMEngine"
                                         code:-1
                                     userInfo:@{NSLocalizedDescriptionKey: @"VM is already running"}];
        }
        return NO;
    }

    self.config = config;
    self.stopRequested = NO;
    self.state = VMStateStarting;

    dispatch_async(self.vmQueue, ^{
        [self runVM];
    });

    return YES;
}

/* ── VM Execution (on vmQueue) ─────────────────── */

- (void)runVM {
    @autoreleasepool {
        uint64_t ramSize = (uint64_t)self.config.ramMB * 1024ULL * 1024ULL;

        /* Step 1: Initialize machine */
        [self notifyOutput:@"\n══ SiliconV v0.1 — Starting VM ══\n\n"];

        if (sv_machine_init(&_machine, self.config.numCPUs, ramSize) < 0) {
            [self notifyOutput:@"✗ Failed to initialize VM\n"];
            self.state = VMStateError;
            return;
        }
        _initialized = YES;

        /* Step 2: Set UART callback */
        pl011_set_tx_callback(&_machine.uart, engine_uart_tx_callback, (__bridge void *)self);

        [self notifyOutput:[NSString stringWithFormat:@"✓ Machine initialized (%d CPUs, %d MB RAM)\n",
                            self.config.numCPUs, self.config.ramMB]];

        /* Step 3: Load kernel */
        if (self.config.kernelPath.length > 0) {
            if (sv_machine_load_kernel(&_machine, self.config.kernelPath.UTF8String) < 0) {
                [self notifyOutput:@"✗ Failed to load kernel\n"];
                self.state = VMStateError;
                goto cleanup;
            }
            [self notifyOutput:[NSString stringWithFormat:@"✓ Kernel loaded: %@\n",
                                self.config.kernelPath.lastPathComponent]];
        }

        /* Step 4: Load DTB (if external) or generate */
        if (self.config.dtbPath.length > 0) {
            if (sv_machine_load_dtb(&_machine, self.config.dtbPath.UTF8String) < 0) {
                [self notifyOutput:@"✗ Failed to load DTB\n"];
                self.state = VMStateError;
                goto cleanup;
            }
        } else {
            /* Set cmdline before generating DTB */
            if (self.config.cmdline.length > 0) {
                _machine.dtb_config.cmdline = self.config.cmdline.UTF8String;
            }
            if (sv_machine_generate_dtb(&_machine) < 0) {
                [self notifyOutput:@"✗ Failed to generate DTB\n"];
                self.state = VMStateError;
                goto cleanup;
            }
            [self notifyOutput:@"✓ DTB generated\n"];
        }

        /* Step 5: Attach virtio-blk if rootfs provided */
        if (self.config.rootfsPath.length > 0) {
            if (sv_machine_attach_virtio_blk(&_machine, self.config.rootfsPath.UTF8String, NO) < 0) {
                [self notifyOutput:@"⚠ Failed to attach virtio-blk (continuing)\n"];
            } else {
                [self notifyOutput:[NSString stringWithFormat:@"✓ virtio-blk: %@\n",
                                    self.config.rootfsPath.lastPathComponent]];
            }
        }

        /* Step 6: Attach virtio-net */
        if (sv_machine_attach_virtio_net(&_machine) < 0) {
            [self notifyOutput:@"⚠ virtio-net unavailable\n"];
        } else {
            [self notifyOutput:@"✓ virtio-net: attached\n"];
        }

        /* Step 7: Attach virtio-console */
        if (sv_machine_attach_virtio_console(&_machine) < 0) {
            [self notifyOutput:@"⚠ virtio-console unavailable\n"];
        } else {
            [self notifyOutput:@"✓ virtio-console: attached\n"];
        }

        [self notifyOutput:@"\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"];

        /* Step 8: Dry run? */
        if (self.config.dryRun) {
            [self notifyOutput:@"Dry run complete — configuration is valid\n"];
            self.state = VMStateIdle;
            goto cleanup;
        }

        /* Step 9: Enter main VM loop */
        self.state = VMStateRunning;
        [self notifyOutput:@"Entering VM main loop...\n\n"];

        int ret = sv_machine_run(&_machine);

        if (ret < 0) {
            [self notifyOutput:@"✗ VM exited with error\n"];
            self.state = VMStateError;
        } else {
            [self notifyOutput:@"\n══ VM stopped ══\n"];
            self.state = VMStateIdle;
        }

    cleanup:
        if (_initialized) {
            sv_machine_destroy(&_machine);
            _initialized = NO;
        }
        if (self.state != VMStateError && self.state != VMStateIdle) {
            self.state = VMStateIdle;
        }
    }
}

/* ── Stop VM ───────────────────────────────────── */

- (void)stop {
    if (self.state == VMStateIdle || self.state == VMStateError) return;

    self.state = VMStateStopping;
    self.stopRequested = YES;

    /* Signal the VM main loop to exit */
    if (_initialized) {
        sv_machine_stop(&_machine);
    }
}

/* ── Pause / Resume ────────────────────────────── */

- (void)pause {
    if (self.state != VMStateRunning) return;

    /* Set running flag to false — the main loop will check this.
     * This is a soft pause: the loop exits but the state is preserved. */
    if (_initialized) {
        _machine.running = false;
    }
    self.state = VMStatePaused;
}

- (void)resume {
    if (self.state != VMStatePaused) return;

    /* Re-run the main loop on the same machine state */
    self.state = VMStateRunning;
    _machine.running = true;

    dispatch_async(self.vmQueue, ^{
        [self notifyOutput:@"[resumed]\n"];
        int ret = sv_machine_run(&_machine);
        if (ret < 0) {
            self.state = VMStateError;
        } else {
            self.state = VMStateIdle;
        }
    });
}

/* ── Keyboard Input ────────────────────────────── */

- (void)sendKey:(unichar)key {
    if (!_initialized || !_machine.running) return;

    /* Map special keys to escape sequences */
    if (key == '\r' || key == '\n') {
        pl011_rx_put(&_machine.uart, '\r');
    } else if (key == 0x7F) {
        /* Backspace → ^H */
        pl011_rx_put(&_machine.uart, 0x08);
    } else if (key < 0x80) {
        pl011_rx_put(&_machine.uart, (uint8_t)key);
    }
}

- (void)sendByte:(uint8_t)byte {
    if (!_initialized || !_machine.running) return;
    pl011_rx_put(&_machine.uart, byte);
}

@end
