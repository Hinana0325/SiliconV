/*
 * SiliconV — VM View Controller (Implementation)
 */

#import "VMViewController.h"
#import "VMEngine.h"

/* ── VMDisplayView — Custom NSView for VM Rendering ── */

@interface VMDisplayView : NSView
@property (nonatomic, assign) BOOL   vmRunning;
@property (nonatomic, assign) double  animationPhase;
@property (nonatomic, strong) CALayer *pulseLayer;
@property (nonatomic, strong) CATextLayer *statusTextLayer;
@property (nonatomic, strong) CATextLayer *brandLayer;
@property (nonatomic, strong) CAShapeLayer *cpuCoreLayers[4];
@property (nonatomic, weak)   VMViewController *controller;
@end

@implementation VMDisplayView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        self.wantsLayer = YES;
        self.layer.backgroundColor = [[NSColor colorWithCalibratedRed:0.06
                                                                green:0.06
                                                                 blue:0.1
                                                                alpha:1.0] CGColor];
        [self setupBrandLayers];
    }
    return self;
}

- (void)setupBrandLayers {
    /* ── Brand Text Layer ──────────────────────── */
    _brandLayer = [CATextLayer layer];
    _brandLayer.string = @"SILICON V";
    _brandLayer.fontSize = 42;
    _brandLayer.font = (__bridge CFTypeRef _Nullable)([NSFont fontWithName:@"Menlo" size:42]
                         ?: [NSFont monospacedSystemFontOfSize:42 weight:NSFontWeightBold]);
    _brandLayer.foregroundColor = [[NSColor colorWithCalibratedRed:0.95
                                                             green:0.85
                                                              blue:0.3
                                                             alpha:0.9] CGColor];
    _brandLayer.alignmentMode = kCAAlignmentCenter;
    _brandLayer.contentsScale = 2.0;
    [self.layer addSublayer:_brandLayer];

    /* ── Subtitle ──────────────────────────────── */
    _statusTextLayer = [CATextLayer layer];
    _statusTextLayer.string = @"Virtual Phone Hardware Platform";
    _statusTextLayer.fontSize = 14;
    _statusTextLayer.font = (__bridge CTFontRef _Nullable)([NSFont systemFontOfSize:14 weight:NSFontWeightLight]);
    _statusTextLayer.foregroundColor = [[NSColor colorWithCalibratedRed:0.5
                                                                  green:0.5
                                                                   blue:0.6
                                                                  alpha:0.8] CGColor];
    _statusTextLayer.alignmentMode = kCAAlignmentCenter;
    _statusTextLayer.contentsScale = 2.0;
    [self.layer addSublayer:_statusTextLayer];

    /* ── CPU Core Indicators ───────────────────── */
    NSArray<NSColor *> *coreColors = @[
        [NSColor colorWithCalibratedRed:0.3 green:0.7  blue:1.0 alpha:0.6],
        [NSColor colorWithCalibratedRed:1.0 green:0.5  blue:0.3 alpha:0.6],
        [NSColor colorWithCalibratedRed:0.3 green:1.0  blue:0.5 alpha:0.6],
        [NSColor colorWithCalibratedRed:0.9 green:0.4  blue:0.8 alpha:0.6],
    ];

    for (int i = 0; i < 4; i++) {
        CAShapeLayer *core = [CAShapeLayer layer];
        core.path = CGPathCreateWithEllipseInRect(CGRectMake(0, 0, 12, 12), NULL);
        core.fillColor = [coreColors[i] CGColor];
        core.opacity = 0.0;
        core.contentsScale = 2.0;
        [self.layer addSublayer:core];
        _cpuCoreLayers[i] = core;
    }

    /* ── Pulse Ring ────────────────────────────── */
    _pulseLayer = [CAShapeLayer layer];
    CGFloat pulseRadius = 50;
    _pulseLayer.path = CGPathCreateWithEllipseInRect(
        CGRectMake(-pulseRadius, -pulseRadius, pulseRadius * 2, pulseRadius * 2), NULL);
    _pulseLayer.fillColor = [[NSColor colorWithCalibratedRed:0.95
                                                       green:0.85
                                                        blue:0.3
                                                       alpha:0.15] CGColor];
    _pulseLayer.opacity = 0.0;
    _pulseLayer.contentsScale = 2.0;
    [self.layer addSublayer:_pulseLayer];
}

- (void)layout {
    [super layout];

    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;
    CGFloat cx = w / 2.0;
    CGFloat cy = h / 2.0;

    /* Brand text */
    _brandLayer.frame = CGRectMake(0, 0, w, 50);
    _brandLayer.position = CGPointMake(cx, cy + 20);

    _statusTextLayer.frame = CGRectMake(0, 0, w, 20);
    _statusTextLayer.position = CGPointMake(cx, cy - 16);

    /* CPU cores in a row below subtitle */
    for (int i = 0; i < 4; i++) {
        CGFloat coreX = cx - 36 + i * 24;
        _cpuCoreLayers[i].position = CGPointMake(coreX, cy - 56);
    }

    /* Pulse ring at center */
    _pulseLayer.position = CGPointMake(cx, cy);
}

- (void)setVmRunning:(BOOL)running {
    if (_vmRunning == running) return;
    _vmRunning = running;

    [CATransaction begin];
    [CATransaction setAnimationDuration:0.6];
    [CATransaction setAnimationTimingFunction:[CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut]];

    if (running) {
        _brandLayer.opacity = 0.3;
        _statusTextLayer.string = @"VM Running";
        _statusTextLayer.foregroundColor = [[NSColor colorWithCalibratedRed:0.3
                                                                      green:0.9
                                                                       blue:0.3
                                                                      alpha:0.9] CGColor];

        CGFloat pulseOpacity = 0.3;
        _pulseLayer.opacity = pulseOpacity;
        CABasicAnimation *pulse = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
        pulse.fromValue = @(1.0);
        pulse.toValue = @(1.8);
        pulse.duration = 2.0;
        pulse.repeatCount = HUGE_VALF;
        pulse.autoreverses = YES;
        [_pulseLayer addAnimation:pulse forKey:@"pulse"];
    } else {
        _brandLayer.opacity = 1.0;
        _statusTextLayer.string = @"Virtual Phone Hardware Platform";
        _statusTextLayer.foregroundColor = [[NSColor colorWithCalibratedRed:0.5
                                                                      green:0.5
                                                                       blue:0.6
                                                                      alpha:0.8] CGColor];
        _pulseLayer.opacity = 0.0;
        [_pulseLayer removeAnimationForKey:@"pulse"];
    }

    /* Animate CPU cores */
    for (int i = 0; i < 4; i++) {
        _cpuCoreLayers[i].opacity = running ? 0.8 : 0.0;
    }

    [CATransaction commit];
}

- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    BOOL isDark = [self.effectiveAppearance.name isEqualToString:NSAppearanceNameDarkAqua] ||
                  [self.effectiveAppearance.name isEqualToString:NSAppearanceNameVibrantDark];
    CGFloat bgBrightness = isDark ? 0.06 : 0.12;
    self.layer.backgroundColor = [[NSColor colorWithCalibratedRed:bgBrightness
                                                            green:bgBrightness
                                                             blue:bgBrightness + 0.04
                                                            alpha:1.0] CGColor];
}

/* ── Keyboard Input ────────────────────────────── */

- (void)keyDown:(NSEvent *)event {
    if (!_vmRunning || !_controller.engine) {
        [super keyDown:event];
        return;
    }

    NSString *chars = event.characters;
    for (NSUInteger i = 0; i < chars.length; i++) {
        unichar ch = [chars characterAtIndex:i];
        [_controller.engine sendKey:ch];
    }
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

@end

/* ── VM View Controller Implementation ─────────── */

@interface VMViewController ()
@property (nonatomic, strong) VMDisplayView *display;
@end

@implementation VMViewController

- (void)loadView {
    _display = [[VMDisplayView alloc] initWithFrame:NSMakeRect(0, 0, 560, 600)];
    _display.controller = self;
    self.view = _display;
}

- (void)viewDidAppear {
    [super viewDidAppear];
    /* Make the display the first responder to capture keyboard events */
    [self.view.window makeFirstResponder:_display];
}

/* ── Framebuffer Update (future virtio-gpu) ────── */

- (void)updateFramebuffer:(const uint8_t *)data
                     size:(CGSize)size
               pixelFormat:(OSType)format {
    /* TODO: Metal framebuffer rendering
     * - Upload data to MTLTexture (BGRA8Unorm for kCGBitmapByteOrder32Little)
     * - Render via MTLRenderCommandEncoder
     * - Present via CAMetalLayer drawable
     *
     * For now, this is a placeholder for the virtio-gpu integration (Phase 5).
     */
    (void)data;
    (void)size;
    (void)format;
}

- (NSView *)displayView {
    return _display;
}

/* ── Expose state to MainWindowController ──────── */

- (void)setEngine:(VMEngine *)engine {
    _engine = engine;
    _display.vmRunning = (engine != nil && engine.running);
}

@end
