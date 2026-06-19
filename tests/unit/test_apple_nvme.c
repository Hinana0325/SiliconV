/*
 * SiliconV — Apple NVMe Unit Tests
 *
 * Tests the NVMe controller emulation: init, register read/write,
 * controller enable, admin commands (Identify), and I/O read/write.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

#include "apple_nvme.h"

/* ── Test Helpers ───────────────────────────────── */

/* Simple in-memory "disk" for testing */
#define TEST_DISK_SIZE  (1024 * 1024)   /* 1 MB */
#define TEST_BLOCK_SIZE 512

static uint8_t test_disk_buf[TEST_DISK_SIZE];

/* DMA context — pretend guest RAM */
#define GUEST_RAM_BASE  0x800000000ULL
#define GUEST_RAM_SIZE  (16 * 1024 * 1024)  /* 16 MB */
static uint8_t guest_ram[GUEST_RAM_SIZE];

static int test_dma_read(void *ctx, uint64_t guest_phys,
                          void *host_buf, size_t len)
{
    (void)ctx;
    if (guest_phys < GUEST_RAM_BASE ||
        guest_phys + len > GUEST_RAM_BASE + GUEST_RAM_SIZE)
        return -1;
    memcpy(host_buf, guest_ram + (guest_phys - GUEST_RAM_BASE), len);
    return 0;
}

static int test_dma_write(void *ctx, uint64_t guest_phys,
                           const void *host_buf, size_t len)
{
    (void)ctx;
    if (guest_phys < GUEST_RAM_BASE ||
        guest_phys + len > GUEST_RAM_BASE + GUEST_RAM_SIZE)
        return -1;
    memcpy(guest_ram + (guest_phys - GUEST_RAM_BASE), host_buf, len);
    return 0;
}

/* IRQ tracking */
static int last_irq = -1;
static int irq_count = 0;

static void test_irq_raise(void *ctx, int irq)
{
    (void)ctx;
    last_irq = irq;
    irq_count++;
}

static void test_irq_lower(void *ctx, int irq)
{
    (void)ctx;
    (void)irq;
}

/* Create a temporary disk image */
static const char *create_test_disk(char *path_buf, size_t buf_size)
{
    snprintf(path_buf, buf_size, "/tmp/sv_nvme_test_%d", getpid());
    int fd = open(path_buf, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0) return NULL;

    /* Write 1MB of pattern data */
    for (size_t i = 0; i < TEST_DISK_SIZE; i++)
        test_disk_buf[i] = (uint8_t)(i & 0xFF);

    write(fd, test_disk_buf, TEST_DISK_SIZE);
    close(fd);
    return path_buf;
}

/* Helper: build cdw0 with opcode + CID */
static inline uint32_t mk_cdw0(uint8_t opcode, uint16_t cid)
{
    return (uint32_t)opcode | ((uint32_t)cid << 16);
}

/* ── Tests ──────────────────────────────────────── */

static void test_init(void)
{
    printf("  test_init... ");

    apple_nvme_state_t nvme;
    apple_nvme_init(&nvme);

    assert(nvme.vs == 0x00010400);  /* NVMe 1.4 */
    assert(nvme.num_ns == 1);
    assert(nvme.ns[0].block_size == 512);
    assert(!nvme.enabled);
    assert(!nvme.ready);
    assert(nvme.disk_image == NULL);

    apple_nvme_destroy(&nvme);
    printf("OK\n");
}

static void test_register_read(void)
{
    printf("  test_register_read... ");

    apple_nvme_state_t nvme;
    apple_nvme_init(&nvme);

    /* CAP */
    uint64_t cap = apple_nvme_mmio_read(&nvme, NVME_REG_CAP, 4);
    assert((cap & NVME_CAP_MQES_MASK) == 255);  /* MQES=255 */

    /* VS */
    uint32_t vs = (uint32_t)apple_nvme_mmio_read(&nvme, NVME_REG_VS, 4);
    assert(vs == 0x00010400);

    /* CC — should be 0 initially */
    uint32_t cc = (uint32_t)apple_nvme_mmio_read(&nvme, NVME_REG_CC, 4);
    assert(cc == 0);

    /* CSTS — should be 0 initially (not ready) */
    uint32_t csts = (uint32_t)apple_nvme_mmio_read(&nvme, NVME_REG_CSTS, 4);
    assert(csts == 0);

    apple_nvme_destroy(&nvme);
    printf("OK\n");
}

static void test_controller_enable(void)
{
    printf("  test_controller_enable... ");

    apple_nvme_state_t nvme;
    apple_nvme_init(&nvme);

    /* Set admin queue attributes */
    apple_nvme_mmio_write(&nvme, NVME_REG_AQA, 0x003F003F, 4);

    /* Set admin queue base addresses */
    uint64_t asq_addr = GUEST_RAM_BASE;
    uint64_t acq_addr = GUEST_RAM_BASE + 4096;
    apple_nvme_mmio_write(&nvme, NVME_REG_ASQ, asq_addr & 0xFFFFFFFF, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ASQ + 4, asq_addr >> 32, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ACQ, acq_addr & 0xFFFFFFFF, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ACQ + 4, acq_addr >> 32, 4);

    /* Enable controller */
    apple_nvme_mmio_write(&nvme, NVME_REG_CC,
                           NVME_CC_EN | (6 << NVME_CC_IOSQES_SHIFT) |
                           (4 << NVME_CC_IOCQES_SHIFT), 4);

    /* Check ready */
    uint32_t csts = (uint32_t)apple_nvme_mmio_read(&nvme, NVME_REG_CSTS, 4);
    assert(csts & NVME_CSTS_RDY);
    assert(nvme.enabled);
    assert(nvme.ready);
    assert(nvme.admin_sq.valid);
    assert(nvme.admin_cq.valid);

    /* Disable */
    apple_nvme_mmio_write(&nvme, NVME_REG_CC, 0, 4);
    csts = (uint32_t)apple_nvme_mmio_read(&nvme, NVME_REG_CSTS, 4);
    assert(!(csts & NVME_CSTS_RDY));

    apple_nvme_destroy(&nvme);
    printf("OK\n");
}

static void test_identify_controller(void)
{
    printf("  test_identify_controller... ");

    apple_nvme_state_t nvme;
    apple_nvme_init(&nvme);

    /* Wire DMA + IRQ */
    apple_nvme_set_dma(&nvme, test_dma_read, test_dma_write, NULL);
    apple_nvme_set_irq_ctx(&nvme, &nvme);
    apple_nvme_set_irq_callbacks(&nvme, test_irq_raise, test_irq_lower);

    /* Setup admin queues in guest RAM */
    memset(guest_ram, 0, sizeof(guest_ram));

    uint64_t sq_base = GUEST_RAM_BASE;
    uint64_t cq_base = GUEST_RAM_BASE + 8192;
    uint64_t data_buf = GUEST_RAM_BASE + 16384;

    /* Build Identify Controller command in admin SQ */
    nvme_sqe_t *sqe = (nvme_sqe_t *)(guest_ram + 0);
    memset(sqe, 0, sizeof(*sqe));
    sqe->cdw0 = mk_cdw0(NVME_ADMIN_IDENTIFY, 0x42);
    sqe->cdw10 = NVME_IDENTIFY_CTRL;  /* CNS=1 */
    sqe->dptr[0] = data_buf;          /* PRP1 */

    /* Enable controller */
    apple_nvme_mmio_write(&nvme, NVME_REG_AQA, 0x003F003F, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ASQ, sq_base & 0xFFFFFFFF, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ASQ + 4, sq_base >> 32, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ACQ, cq_base & 0xFFFFFFFF, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ACQ + 4, cq_base >> 32, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_CC,
                           NVME_CC_EN | (6 << NVME_CC_IOSQES_SHIFT) |
                           (4 << NVME_CC_IOCQES_SHIFT), 4);

    /* Ring admin SQ doorbell (tail=1) */
    irq_count = 0;
    apple_nvme_mmio_write(&nvme, NVME_DOORBELL_BASE, 1, 4);

    /* Check completion */
    nvme_cqe_t *cqe = (nvme_cqe_t *)(guest_ram + (cq_base - GUEST_RAM_BASE));
    assert(cqe->cid == 0x42);
    assert((cqe->status & 0x7FE) == 0); /* SC=0 (success) */
    assert(irq_count > 0);

    /* Check identify data */
    nvme_identify_ctrl_t *ctrl = (nvme_identify_ctrl_t *)(guest_ram + (data_buf - GUEST_RAM_BASE));
    assert(ctrl->vid == 0x106B);  /* Apple */
    assert(ctrl->nn == 1);        /* 1 namespace */

    apple_nvme_destroy(&nvme);
    printf("OK\n");
}

static void test_attach_disk(void)
{
    printf("  test_attach_disk... ");

    char disk_path[64];
    const char *path = create_test_disk(disk_path, sizeof(disk_path));
    assert(path != NULL);

    apple_nvme_state_t nvme;
    apple_nvme_init(&nvme);

    int ret = apple_nvme_attach_disk(&nvme, path, false);
    assert(ret == 0);
    assert(nvme.disk_image != NULL);
    assert(nvme.ns[0].active);
    assert(nvme.ns[0].size_blocks == TEST_DISK_SIZE / TEST_BLOCK_SIZE);

    apple_nvme_destroy(&nvme);
    unlink(path);
    printf("OK\n");
}

static void test_io_read(void)
{
    printf("  test_io_read... ");

    char disk_path[64];
    const char *path = create_test_disk(disk_path, sizeof(disk_path));
    assert(path != NULL);

    apple_nvme_state_t nvme;
    apple_nvme_init(&nvme);
    apple_nvme_set_dma(&nvme, test_dma_read, test_dma_write, NULL);
    apple_nvme_set_irq_ctx(&nvme, &nvme);
    apple_nvme_set_irq_callbacks(&nvme, test_irq_raise, test_irq_lower);
    apple_nvme_attach_disk(&nvme, path, false);

    memset(guest_ram, 0, sizeof(guest_ram));

    /* Setup queues */
    uint64_t admin_sq_base = GUEST_RAM_BASE;
    uint64_t admin_cq_base = GUEST_RAM_BASE + 8192;
    uint64_t io_sq_base    = GUEST_RAM_BASE + 16384;
    uint64_t io_cq_base    = GUEST_RAM_BASE + 32768;
    uint64_t read_buf      = GUEST_RAM_BASE + 65536;

    /* Enable controller */
    apple_nvme_mmio_write(&nvme, NVME_REG_AQA, 0x003F003F, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ASQ, admin_sq_base & 0xFFFFFFFF, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ASQ + 4, admin_sq_base >> 32, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ACQ, admin_cq_base & 0xFFFFFFFF, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_ACQ + 4, admin_cq_base >> 32, 4);
    apple_nvme_mmio_write(&nvme, NVME_REG_CC,
                           NVME_CC_EN | (6 << NVME_CC_IOSQES_SHIFT) |
                           (4 << NVME_CC_IOCQES_SHIFT), 4);

    /* --- Create I/O CQ (id=1) --- */
    nvme_sqe_t *sqe = (nvme_sqe_t *)(guest_ram + 0);
    memset(sqe, 0, sizeof(*sqe));
    sqe->cdw0 = mk_cdw0(NVME_ADMIN_CREATE_IO_CQ, 1);
    sqe->dptr[0] = io_cq_base;
    sqe->cdw10 = (31 << 16) | 1;  /* qsize=32, qid=1 */
    sqe->cdw11 = 0x00010001;      /* iv=1, pc=1 */

    /* Ring admin SQ doorbell */
    irq_count = 0;
    apple_nvme_mmio_write(&nvme, NVME_DOORBELL_BASE, 1, 4);

    /* --- Create I/O SQ (id=1, linked to CQ 1) --- */
    sqe = (nvme_sqe_t *)(guest_ram + sizeof(nvme_sqe_t));
    memset(sqe, 0, sizeof(*sqe));
    sqe->cdw0 = mk_cdw0(NVME_ADMIN_CREATE_IO_SQ, 2);
    sqe->dptr[0] = io_sq_base;
    sqe->cdw10 = (31 << 16) | 1;  /* qsize=32, qid=1 */
    sqe->cdw11 = 0x00010001;      /* cqid=1, pc=1 */

    apple_nvme_mmio_write(&nvme, NVME_DOORBELL_BASE, 2, 4);

    /* --- Submit Read command on I/O SQ 1 --- */
    nvme_sqe_t *io_sqe = (nvme_sqe_t *)(guest_ram + (io_sq_base - GUEST_RAM_BASE));
    memset(io_sqe, 0, sizeof(*io_sqe));
    io_sqe->cdw0 = mk_cdw0(NVME_IO_READ, 100);
    io_sqe->nsid = 1;
    io_sqe->dptr[0] = read_buf;
    io_sqe->cdw10 = 0;            /* SLBA low */
    io_sqe->cdw11 = 0;            /* SLBA high */
    io_sqe->cdw12 = 3;            /* NLB=4 (0-based) */

    /* Ring I/O SQ doorbell (sqid=1 → offset = (1*2)*4 = 8) */
    irq_count = 0;
    apple_nvme_mmio_write(&nvme, NVME_DOORBELL_BASE + 8, 1, 4);

    /* Verify read data matches disk image */
    uint8_t *read_data = guest_ram + (read_buf - GUEST_RAM_BASE);
    assert(memcmp(read_data, test_disk_buf, 4 * TEST_BLOCK_SIZE) == 0);

    /* Verify completion */
    nvme_cqe_t *io_cqe = (nvme_cqe_t *)(guest_ram + (io_cq_base - GUEST_RAM_BASE));
    assert(io_cqe->cid == 100);
    assert((io_cqe->status & 0x7FE) == 0); /* success */
    assert(irq_count > 0);

    apple_nvme_destroy(&nvme);
    unlink(path);
    printf("OK\n");
}

/* ── Main ───────────────────────────────────────── */
int main(void)
{
    printf("=== Apple NVMe Unit Tests ===\n");

    test_init();
    test_register_read();
    test_controller_enable();
    test_identify_controller();
    test_attach_disk();
    test_io_read();

    printf("\nAll NVMe tests passed! ✅\n");
    return 0;
}
