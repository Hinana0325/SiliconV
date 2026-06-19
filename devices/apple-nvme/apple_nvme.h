/*
 * SiliconV — Apple NVMe Storage Controller
 *
 * Minimal NVMe 1.4 compliant controller for the Apple platform profile.
 * XNU's AppleANS2/3NVMeController expects standard NVMe registers + queues.
 *
 * Supports:
 *   - Admin queue (Identify, Create/Delete I/O SQ/CQ)
 *   - 1 I/O queue pair (Read/Write)
 *   - DMA via DART IOMMU
 *   - Interrupt via AIC
 *   - Backed by host disk image file
 */

#ifndef SILICONV_APPLE_NVME_H
#define SILICONV_APPLE_NVME_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ── NVMe Register Offsets ──────────────────────── */
#define NVME_REG_CAP        0x00    /* Controller Capabilities (64-bit) */
#define NVME_REG_VS         0x08    /* Version (32-bit) */
#define NVME_REG_INTMS      0x0C    /* Interrupt Mask Set (32-bit) */
#define NVME_REG_INTMC      0x10    /* Interrupt Mask Clear (32-bit) */
#define NVME_REG_CC         0x14    /* Controller Configuration (32-bit) */
#define NVME_REG_CSTS       0x1C    /* Controller Status (32-bit) */
#define NVME_REG_NSSR       0x20    /* NVM Subsystem Reset (32-bit) */
#define NVME_REG_AQA        0x24    /* Admin Queue Attributes (32-bit) */
#define NVME_REG_ASQ        0x28    /* Admin SQ Base Address (64-bit) */
#define NVME_REG_ACQ        0x30    /* Admin CQ Base Address (64-bit) */
#define NVME_REG_CMBLOC     0x38    /* Controller Memory Buffer Location */
#define NVME_REG_CMBSZ      0x3C    /* Controller Memory Buffer Size */

/* Doorbell registers start at 0x1000 */
#define NVME_DOORBELL_BASE  0x1000
#define NVME_DOORBELL_STRIDE 4      /* SQ Tail / CQ Head per queue */

/* ── CAP (Controller Capabilities) ──────────────── */
#define NVME_CAP_MQES_SHIFT     0       /* Max Queue Entries Supported - 1 */
#define NVME_CAP_MQES_MASK      0xFFFF
#define NVME_CAP_CQR_SHIFT      16      /* Contiguous Queues Required */
#define NVME_CAP_CQR            (1ULL << 16)
#define NVME_CAP_AMS_SHIFT      17      /* Arbitration Mechanism Supported */
#define NVME_CAP_AMS_MASK       (0x3ULL << 17)
#define NVME_CAP_TO_SHIFT       24      /* Timeout (in 500ms units) */
#define NVME_CAP_TO_MASK        (0xFFULL << 24)
#define NVME_CAP_DSTRD_SHIFT    32      /* Doorbell Stride */
#define NVME_CAP_DSTRD_MASK     (0xFULL << 32)
#define NVME_CAP_NSSRS_SHIFT    36      /* NVM Subsystem Reset Supported */
#define NVME_CAP_CSS_SHIFT      37      /* Command Sets Supported */
#define NVME_CAP_CSS_MASK       (0xFFULL << 37)
#define NVME_CAP_MPSMIN_SHIFT   48      /* Memory Page Size Minimum */
#define NVME_CAP_MPSMIN_MASK    (0xFULL << 48)
#define NVME_CAP_MPSMAX_SHIFT   52      /* Memory Page Size Maximum */
#define NVME_CAP_MPSMAX_MASK    (0xFULL << 52)

/* ── CC (Controller Configuration) ──────────────── */
#define NVME_CC_EN          (1 << 0)    /* Enable */
#define NVME_CC_CSS_SHIFT   4           /* I/O Command Set Selected */
#define NVME_CC_CSS_MASK    (0x7 << 4)
#define NVME_CC_MPS_SHIFT   7           /* Memory Page Size */
#define NVME_CC_MPS_MASK    (0xF << 7)
#define NVME_CC_AMS_SHIFT   11          /* Arbitration Mechanism Selected */
#define NVME_CC_AMS_MASK    (0x7 << 11)
#define NVME_CC_SHN_SHIFT   14          /* Shutdown Notification */
#define NVME_CC_SHN_MASK    (0x3 << 14)
#define NVME_CC_IOSQES_SHIFT 16         /* I/O Submission Queue Entry Size */
#define NVME_CC_IOSQES_MASK (0xF << 16)
#define NVME_CC_IOCQES_SHIFT 20         /* I/O Completion Queue Entry Size */
#define NVME_CC_IOCQES_MASK (0xF << 20)
#define NVME_CC_CRIME        (1 << 24)  /* Apple: CRIME enable (vendor) */

/* ── CSTS (Controller Status) ───────────────────── */
#define NVME_CSTS_RDY       (1 << 0)   /* Ready */
#define NVME_CSTS_CFS       (1 << 1)   /* Controller Fatal Status */
#define NVME_CSTS_SHST_SHIFT 2         /* Shutdown Status */
#define NVME_CSTS_SHST_MASK (0x3 << 2)
#define NVME_CSTS_NSSRO     (1 << 4)   /* NVM Subsystem Reset Occurred */

/* ── NVMe Opcodes ───────────────────────────────── */
/* Admin commands */
#define NVME_ADMIN_DELETE_IO_SQ     0x00
#define NVME_ADMIN_CREATE_IO_SQ     0x01
#define NVME_ADMIN_GET_LOG_PAGE     0x02
#define NVME_ADMIN_DELETE_IO_CQ     0x04
#define NVME_ADMIN_CREATE_IO_CQ     0x05
#define NVME_ADMIN_IDENTIFY         0x06
#define NVME_ADMIN_ABORT            0x08
#define NVME_ADMIN_SET_FEATURES     0x09
#define NVME_ADMIN_GET_FEATURES     0x0A

/* I/O commands */
#define NVME_IO_FLUSH               0x00
#define NVME_IO_WRITE               0x01
#define NVME_IO_READ                0x02

/* ── Identify CNS values ────────────────────────── */
#define NVME_IDENTIFY_NS            0x00
#define NVME_IDENTIFY_CTRL          0x01
#define NVME_IDENTIFY_NS_LIST       0x02

/* ── Identify Controller Data (1024 bytes) ──────── */
typedef struct __attribute__((packed)) {
    uint16_t vid;           /* PCI Vendor ID */
    uint16_t ssvid;         /* PCI Subsystem Vendor ID */
    char     sn[20];        /* Serial Number */
    char     mn[40];        /* Model Number */
    char     fr[8];         /* Firmware Revision */
    uint8_t  rab;           /* Recommended Arb Burst */
    uint8_t  ieee[3];       /* IEEE OUI Identifier */
    uint8_t  cmic;          /* Controller Multi-Path I/O */
    uint8_t  mdts;          /* Max Data Transfer Size */
    uint16_t cntlid;        /* Controller ID */
    uint32_t ver;           /* Version */
    uint32_t rtd3r;         /* RTD3 Resume Latency */
    uint32_t rtd3e;         /* RTD3 Entry Latency */
    uint32_t oaes;          /* Optional Async Events Supported */
    uint32_t ctratt;        /* Controller Attributes */
    uint8_t  rsvd100[12];
    uint8_t  fguid[16];     /* FRU GUID */
    uint8_t  rsvd128[128];
    uint8_t  oacs[2];       /* Optional Admin Command Support */
    uint8_t  acl;           /* Abort Command Limit */
    uint8_t  aerl;          /* Async Event Request Limit */
    uint8_t  frmw;          /* Firmware Updates */
    uint8_t  lpa;           /* Log Page Attributes */
    uint8_t  elpe;          /* Error Log Page Entries */
    uint8_t  npss;          /* Number of Power States Support */
    uint8_t  avscc;         /* Admin Vendor Specific Command Config */
    uint8_t  apsta;         /* Autonomous Power State Transition */
    uint16_t wctemp;        /* Warning Composite Temp Threshold */
    uint16_t cctemp;        /* Critical Composite Temp Threshold */
    uint16_t mtfa;          /* Max Time for Firmware Activation */
    uint32_t hmpre;         /* Host Memory Buffer Preferred Size */
    uint32_t hmmin;         /* Host Memory Buffer Minimum Size */
    uint8_t  tnvmcap[16];  /* Total NVM Capacity */
    uint8_t  unvmcap[16];  /* Unallocated NVM Capacity */
    uint32_t rpmbs;         /* Replay Protected Memory Block Support */
    uint16_t edstt;         /* Extended Device Self-test Time */
    uint8_t  dsto;          /* Device Self-test Options */
    uint8_t  fwug;          /* Firmware Update Granularity */
    uint16_t kas;           /* Keep Alive Support */
    uint16_t hctma;         /* Host Controlled Thermal Management */
    uint16_t mntmt;         /* Minimum Thermal Management Temperature */
    uint16_t mxtmt;         /* Maximum Thermal Management Temperature */
    uint32_t sanicap;       /* Sanitize Capabilities */
    uint8_t  rsvd332[180];
    uint8_t  sqes;          /* SQ Entry Size */
    uint8_t  cqes;          /* CQ Entry Size */
    uint16_t maxcmd;        /* Max Outstanding Commands */
    uint32_t nn;            /* Number of Namespaces */
    uint8_t  oncs;          /* Optional NVM Command Support */
    uint8_t  fuses;         /* Fused Operation Support */
    uint8_t  fna;           /* Format NVM Attributes */
    uint8_t  vwc;           /* Volatile Write Cache */
    uint16_t awun;          /* Atomic Write Unit Normal */
    uint16_t awupf;         /* Atomic Write Unit Power Fail */
    uint8_t  nvscc;         /* NVM Vendor Specific Command Config */
    uint8_t  nwpc;          /* Namespace Write Protection Cap */
    uint16_t acwu;          /* Atomic Compare & Write Unit */
    uint16_t rsvd534;
    uint32_t sgls;          /* SGL Support */
    uint8_t  rsvd540[228];
    uint8_t  rsvd768[256];  /* Vendor specific */
} nvme_identify_ctrl_t;

/* ── Completion Queue Entry ─────────────────────── */
typedef struct {
    uint32_t cdw0;      /* Command-specific (status in upper bits) */
    uint32_t reserved;
    uint16_t sq_head;   /* SQ Head Pointer */
    uint16_t sq_id;     /* SQ Identifier */
    uint16_t cid;       /* Command Identifier */
    uint16_t status;    /* Status field (P, SC, SCT, CRD, M, DNR) */
} __attribute__((packed)) nvme_cqe_t;

/* ── SQE CID extraction ────────────────────────── */
#define NVME_SQE_CID(sqe)  (((sqe)->cdw0 >> 16) & 0xFFFF)

/* ── Submission Queue Entry ─────────────────────── */
typedef struct {
    uint32_t cdw0;      /* Opcode(7:0), FUSE(9:8), PSDT(15:14), CID(31:16) */
    uint32_t nsid;      /* Namespace Identifier */
    uint64_t reserved;
    uint64_t mptr;      /* Metadata Pointer */
    uint64_t dptr[2];   /* Data Pointer (PRP or SGL) */
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_sqe_t;

/* ── Queue State ────────────────────────────────── */
#define NVME_MAX_QUEUE_ENTRIES  256
#define NVME_MAX_QUEUES         4

typedef struct {
    uint64_t phys_base;     /* Physical (guest) base address */
    uint16_t size;          /* Number of entries - 1 */
    uint16_t head;          /* Consumer head (CQ) or producer tail (SQ) */
    uint16_t tail;          /* Producer tail (SQ) */
    bool     valid;         /* Queue is configured */
    uint16_t id;            /* Queue ID */
    uint16_t cq_vector;     /* CQ interrupt vector */
    bool     pc;            /* Physically contiguous */
} nvme_queue_t;

/* ── Namespace State ────────────────────────────── */
#define NVME_MAX_NS     1

typedef struct {
    uint64_t size_blocks;   /* Total blocks */
    uint32_t block_size;    /* Bytes per block (512 or 4096) */
    uint64_t capacity;      /* Total bytes */
    bool     active;
} nvme_ns_t;

/* ── DMA Callback ───────────────────────────────── */
/* For reading from guest physical memory */
typedef int (*nvme_dma_read_fn)(void *ctx, uint64_t guest_phys,
                                 void *host_buf, size_t len);
/* For writing to guest physical memory */
typedef int (*nvme_dma_write_fn)(void *ctx, uint64_t guest_phys,
                                  const void *host_buf, size_t len);

/* ── Apple NVMe Controller State ────────────────── */
typedef struct {
    /* NVMe Registers */
    uint64_t cap;           /* Controller Capabilities */
    uint32_t vs;            /* Version (1.4) */
    uint32_t intms;         /* Interrupt Mask Set */
    uint32_t intmc;         /* Interrupt Mask Clear */
    uint32_t cc;            /* Controller Configuration */
    uint32_t csts;          /* Controller Status */
    uint32_t aqa;           /* Admin Queue Attributes */
    uint64_t asq;           /* Admin SQ Base Address */
    uint64_t acq;           /* Admin CQ Base Address */

    /* Admin Queue Pair */
    nvme_queue_t admin_sq;
    nvme_queue_t admin_cq;

    /* I/O Queue Pairs */
    nvme_queue_t io_sq[NVME_MAX_QUEUES];
    nvme_queue_t io_cq[NVME_MAX_QUEUES];
    int          num_io_queues;

    /* Namespaces */
    nvme_ns_t    ns[NVME_MAX_NS];
    int          num_ns;

    /* Backing storage */
    FILE        *disk_image;
    uint64_t     disk_size;
    bool         read_only;

    /* DMA callbacks */
    nvme_dma_read_fn  dma_read;
    nvme_dma_write_fn dma_write;
    void             *dma_ctx;

    /* IRQ */
    int          irq_num;
    void        *irq_context;     /* AIC state */
    void       (*irq_raise)(void *ctx, int irq);
    void       (*irq_lower)(void *ctx, int irq);

    /* Admin command buffer (for fetching SQEs from guest memory) */
    nvme_sqe_t   cmd_buf;

    /* Controller state */
    bool         enabled;         /* CC.EN set */
    bool         ready;           /* CSTS.RDY set */

    /* Controller serial/model (for Identify) */
    char         serial[21];
    char         model[41];
    char         firmware[9];
} apple_nvme_state_t;

/* ── API ───────────────────────────────────────── */

/* Initialize NVMe controller */
void apple_nvme_init(apple_nvme_state_t *nvme);

/* Attach a host disk image file */
int apple_nvme_attach_disk(apple_nvme_state_t *nvme,
                            const char *path, bool read_only);

/* Set DMA callbacks (for guest memory access via DART) */
void apple_nvme_set_dma(apple_nvme_state_t *nvme,
                         nvme_dma_read_fn read_fn,
                         nvme_dma_write_fn write_fn,
                         void *ctx);

/* Set IRQ callbacks (wire to AIC) */
void apple_nvme_set_irq_ctx(apple_nvme_state_t *nvme, void *ctx);
void apple_nvme_set_irq_callbacks(apple_nvme_state_t *nvme,
                                   void (*raise)(void *ctx, int irq),
                                   void (*lower)(void *ctx, int irq));

/* MMIO handlers */
uint64_t apple_nvme_mmio_read(apple_nvme_state_t *nvme,
                               uint64_t offset, int size);
void     apple_nvme_mmio_write(apple_nvme_state_t *nvme,
                                uint64_t offset, uint64_t value, int size);

/* Destroy */
void apple_nvme_destroy(apple_nvme_state_t *nvme);

#endif /* SILICONV_APPLE_NVME_H */
