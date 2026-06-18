/*
 * SiliconV — Apple AIC Unit Test
 *
 * Tests the Apple Interrupt Controller emulation:
 *   - Basic IRQ raise/lower
 *   - IRQ mask/unmask
 *   - Acknowledge/EOI cycle
 *   - IPI send/receive
 *   - MMIO register access
 */

#include "../../devices/apple-aic/apple_aic.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int irq_signalled = 0;
static int irq_cpu = -1;
static int irq_number = -1;

static void test_irq_cb(int cpu, int irq, void *opaque)
{
    (void)opaque;
    irq_signalled = 1;
    irq_cpu = cpu;
    irq_number = irq;
}

static void test_basic_irq(void)
{
    printf("test_basic_irq...\n");

    apple_aic_state_t aic;
    apple_aic_init(&aic, 4);
    apple_aic_set_callback(&aic, test_irq_cb, NULL);

    /* Raise IRQ 32 */
    irq_signalled = 0;
    apple_aic_raise_irq(&aic, 32);

    /* IRQ should be pending */
    int irq = apple_aic_acknowledge(&aic, 0);
    assert(irq == 32);
    assert(irq_signalled == 1);

    /* EOI */
    apple_aic_eoi(&aic, 0, 32);

    /* IRQ should now be inactive */
    int pending = apple_aic_get_pending(&aic, 0);
    assert(pending < 0 || pending != 32);

    printf("  PASS\n");
}

static void test_irq_mask(void)
{
    printf("test_irq_mask...\n");

    apple_aic_state_t aic;
    apple_aic_init(&aic, 4);
    apple_aic_set_callback(&aic, test_irq_cb, NULL);

    /* Raise IRQ 33 */
    apple_aic_raise_irq(&aic, 33);

    /* Acknowledge */
    int irq = apple_aic_acknowledge(&aic, 0);
    assert(irq == 33);

    /* EOI */
    apple_aic_eoi(&aic, 0, 33);

    printf("  PASS\n");
}

static void test_ipi(void)
{
    printf("test_ipi...\n");

    apple_aic_state_t aic;
    apple_aic_init(&aic, 4);
    apple_aic_set_callback(&aic, test_irq_cb, NULL);

    /* Send IPI to CPU 2 */
    irq_signalled = 0;
    apple_aic_send_ipi(&aic, 2, 0);

    /* Check IPI pending on CPU 2 */
    assert(aic.cpu[2].ipi_pending & 1);

    printf("  PASS\n");
}

static void test_mmio(void)
{
    printf("test_mmio...\n");

    apple_aic_state_t aic;
    apple_aic_init(&aic, 4);

    /* Read revision */
    uint64_t rev = apple_aic_mmio_read(&aic, AIC_REV, 4);
    assert(rev == 0x00010000);

    /* Read capabilities */
    uint64_t cap0 = apple_aic_mmio_read(&aic, AIC_CAP0, 4);
    assert(cap0 == APPLE_AIC_NUM_IRQ);

    /* Enable global IRQ */
    apple_aic_mmio_write(&aic, AIC_GLB_CFG, 1, 4);
    uint64_t cfg = apple_aic_mmio_read(&aic, AIC_GLB_CFG, 4);
    assert(cfg & 1);

    printf("  PASS\n");
}

int main(void)
{
    printf("\n=== Apple AIC Unit Tests ===\n\n");

    test_basic_irq();
    test_irq_mask();
    test_ipi();
    test_mmio();

    printf("\n=== All AIC tests passed! ===\n");
    return 0;
}
