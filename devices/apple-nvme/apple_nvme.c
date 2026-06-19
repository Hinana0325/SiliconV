/*
 * SiliconV — Apple NVMe Storage Controller (Implementation)
 *
 * Minimal NVMe 1.4 controller for Apple platform profile.
 * Handles admin queue + 1 I/O queue pair for block read/write.
 * DMA goes through DART IOMMU via callbacks.
 */

#include "apple_nvme.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

/* ── Logging ────────────────────────────────────── */
#define NVME_LOG(fmt, ...) \
    printf("[NVMe] " fmt "\n", ##__VA_ARGS__)

#define NVME_DBG(fmt, ...) /* disabled by default */

/* ── Identify Namespace Data (4096 bytes) ───────── */
typedef struct __attribute__((packed)) {
    uint64_t nsze;          /* Namespace Size (in blocks) */
    uint64_t ncap;          /* Namespace Capacity */
    uint64_t nuse;          /* Namespace Utilization */
    uint8_t  nsfeat;        /* Namespace Features */
    uint8_t  nlbaf;         /* Number of LBA Formats (0-based) */
    uint8_t  flbas;         /* Formatted LBA Size */
    uint8_t  mc;            /* Metadata Capabilities */
    uint8_t  dpc;           /* Data Protection Capabilities */
    uint8_t  dps;           /* Data Protection Settings */
    uint8_t  nmic;          /* Namespace Multi-path I/O */
    uint8_t  rescap;        /* Reservation Capabilities */
    uint8_t  fpi;           /* Format Progress Indicator */
    uint8_t  dlfeat;        /* Deallocate Logical Block Features */
    uint8_t  nawun;         /* Namespace Atomic Write Unit Normal */
    uint8_t  nawupf;        /* Namespace Atomic Write Unit Power Fail */
    uint16_t nacwu;         /* Namespace Atomic Compare & Write Unit */
    uint16_t nabsn;         /* Atomic Boundary Size Normal */
    uint16_t nabo;          /* Atomic Boundary Offset */
    uint16_t nabspf;        /* Atomic Boundary Size Power Fail */
    uint8_t  rsvd64[2];
    uint64_t nvmcap[2];     /* NVM Capacity */
    uint8_t  rsvd80[40];
    uint8_t  nguid[16];     /* Namespace Globally Unique Identifier */
    uint64_t eui64;         /* IEEE Extended Unique Identifier */
    /* LBA Format Support (up to 64) */
    struct {
        uint16_t ms;        /* Metadata Size */
        uint8_t  lbads;     /* LBA Data Size (2^n) */
        uint8_t  rp;        /* Relative Performance */
    } lbaf[64];
    uint8_t  rsvd384[192];
    uint8_t  vendor[3712];
} nvme_identify_ns_t;

/* ── Set/Get Features ───────────────────────────── */
#define NVME_FEAT_NUM_QUEUES    0x07
#define NVME_FEAT_WRITE_CACHE   0x06

/* ── Status Code Types ──────────────────────────── */
#define NVME_SCT_GENERIC    0x0
#define NVME_SCT_CMD_SPEC   0x1

/* ── Generic Status Codes ───────────────────────── */
#define NVME_SC_SUCCESS             0x00
#define NVME_SC_INVALID_OPCODE      0x01
#define NVME_SC_INVALID_FIELD       0x02
#define NVME_SC_DATA_TRANSFER       0x04
#define NVME_SC_ABORT_REQUESTED     0x07
#define NVME_SC_INTERNAL            0x06
#define NVME_SC_INVALID_NS          0x0B
#define NVME_SC_LBA_RANGE           0x80

/* ── Helper: build CQE status ───────────────────── */
static inline uint16_t nvme_mk_status(uint16_t sc, uint16_t sct)
{
    /* Status: DNR(15) | M(14) | CRD(13:12) | SCT(11:9) | SC(8:1) | P(0) */
    return (uint16_t)((sct << 9) | (sc << 1));
}

/* ── Helper: raise/lower IRQ ────────────────────── */
static void nvme_raise_irq(apple_nvme_state_t *nvme)
{
    if (nvme->irq_raise && nvme->irq_context)
        nvme->irq_raise(nvme->irq_context, nvme->irq_num);
}

/* ── Helper: guest memory read/write via DMA ────── */
static int nvme_guest_read(apple_nvme_state_t *nvme, uint64_t addr,
                            void *buf, size_t len)
{
    if (nvme->dma_read)
        return nvme->dma_read(nvme->dma_ctx, addr, buf, len);
    return -1;
}

static int nvme_guest_write(apple_nvme_state_t *nvme, uint64_t addr,
                             const void *buf, size_t len)
{
    if (nvme->dma_write)
        return nvme->dma_write(nvme->dma_ctx, addr, buf, len);
    return -1;
}

/* ═══════════════════════════════════════════════════
 *  Command Processing
 * ═══════════════════════════════════════════════════ */

/* ── Fill Identify Controller data ──────────────── */
static void nvme_fill_identify_ctrl(apple_nvme_state_t *nvme,
                                     nvme_identify_ctrl_t *data)
{
    memset(data, 0, sizeof(*data));

    data->vid = 0x106B;     /* Apple */
    data->ssvid = 0x106B;

    /* Serial, Model, Firmware */
    memset(data->sn, ' ', 20);
    size_t sn_len = strlen(nvme->serial);
    if (sn_len > 20) sn_len = 20;
    memcpy(data->sn, nvme->serial, sn_len);

    memset(data->mn, ' ', 40);
    size_t mn_len = strlen(nvme->model);
    if (mn_len > 40) mn_len = 40;
    memcpy(data->mn, nvme->model, mn_len);

    memset(data->fr, ' ', 8);
    size_t fr_len = strlen(nvme->firmware);
    if (fr_len > 8) fr_len = 8;
    memcpy(data->fr, nvme->firmware, fr_len);

    data->rab = 0;
    data->ieee[0] = 0x00;
    data->ieee[1] = 0x1A;
    data->ieee[2] = 0x2B;   /* Apple OUI (fake) */
    data->mdts = 5;          /* Max 2^5 * 4KB = 128KB transfer */
    data->cntlid = 0x0001;
    data->ver = nvme->vs;

    /* SQ/CQ entry sizes: 64 bytes / 16 bytes */
    data->sqes = 0x66;       /* min 64, max 64 */
    data->cqes = 0x44;       /* min 16, max 16 */
    data->maxcmd = 32;
    data->nn = nvme->num_ns;
    data->oncs = 0x03;       /* Write Uncorrectable + Dataset Management */
    data->vwc = 0x01;        /* Volatile Write Cache present */
    data->awun = 0;
    data->awupf = 0;

    /* Number of queues */
    data->oacs[0] = 0x00;
    data->oacs[1] = 0x00;
    data->acl = 3;           /* Abort Command Limit - 1 */
    data->aerl = 3;          /* Async Event Request Limit - 1 */
    data->frmw = 0x05;       /* Slot 1 read-only, no reset needed */
    data->npss = 0;          /* 1 power state */
}

/* ── Fill Identify Namespace data ───────────────── */
static void nvme_fill_identify_ns(apple_nvme_state_t *nvme, int nsid,
                                   nvme_identify_ns_t *data)
{
    memset(data, 0, sizeof(*data));

    if (nsid < 1 || nsid > nvme->num_ns) return;

    nvme_ns_t *ns = &nvme->ns[nsid - 1];
    if (!ns->active) return;

    data->nsze = ns->size_blocks;
    data->ncap = ns->size_blocks;
    data->nuse = ns->size_blocks;

    /* LBA format 0: 512 bytes, no metadata */
    data->nlbaf = 0;         /* 1 LBA format (index 0) */
    data->flbas = 0;         /* Selected: LBA format 0 */
    data->lbaf[0].ms = 0;   /* No metadata */
    data->lbaf[0].lbads = 9; /* 2^9 = 512 bytes */
    data->lbaf[0].rp = 0;   /* Best performance */
}

/* ── Process Admin Identify command ─────────────── */
static void nvme_admin_identify(apple_nvme_state_t *nvme,
                                 nvme_sqe_t *cmd)
{
    uint32_t cdw10 = cmd->cdw10;
    uint8_t cns = cdw10 & 0xFF;
    uint16_t ctrlid = (cdw10 >> 16) & 0xFFFF;
    uint64_t dptr_addr = cmd->dptr[0]; /* PRP1 */
    (void)ctrlid;

    nvme_cqe_t cqe;
    memset(&cqe, 0, sizeof(cqe));
    cqe.cid = NVME_SQE_CID(cmd);
    cqe.sq_id = 0;

    if (cns == NVME_IDENTIFY_CTRL) {
        nvme_identify_ctrl_t ctrl_data;
        nvme_fill_identify_ctrl(nvme, &ctrl_data);
        if (nvme_guest_write(nvme, dptr_addr, &ctrl_data, sizeof(ctrl_data)) < 0) {
            NVME_LOG("DMA write failed for Identify Controller");
            cqe.status = nvme_mk_status(NVME_SC_INTERNAL, NVME_SCT_GENERIC);
        } else {
            cqe.status = nvme_mk_status(NVME_SC_SUCCESS, NVME_SCT_GENERIC);
        }
    } else if (cns == NVME_IDENTIFY_NS) {
        uint32_t nsid = cmd->nsid;
        nvme_identify_ns_t ns_data;
        nvme_fill_identify_ns(nvme, nsid, &ns_data);
        if (nvme_guest_write(nvme, dptr_addr, &ns_data, sizeof(ns_data)) < 0) {
            NVME_LOG("DMA write failed for Identify NS %u", nsid);
            cqe.status = nvme_mk_status(NVME_SC_INTERNAL, NVME_SCT_GENERIC);
        } else {
            cqe.status = nvme_mk_status(NVME_SC_SUCCESS, NVME_SCT_GENERIC);
        }
    } else if (cns == NVME_IDENTIFY_NS_LIST) {
        /* Return empty NS list */
        uint8_t zeros[4096];
        memset(zeros, 0, sizeof(zeros));
        nvme_guest_write(nvme, dptr_addr, zeros, sizeof(zeros));
        cqe.status = nvme_mk_status(NVME_SC_SUCCESS, NVME_SCT_GENERIC);
    } else {
        NVME_LOG("Identify: unsupported CNS 0x%02x", cns);
        cqe.status = nvme_mk_status(NVME_SC_INVALID_FIELD, NVME_SCT_GENERIC);
    }

    /* Post completion to Admin CQ */
    if (nvme->admin_cq.valid) {
        uint16_t cq_tail = nvme->admin_cq.head;
        uint64_t cq_addr = nvme->admin_cq.phys_base +
                            cq_tail * sizeof(nvme_cqe_t);
        cqe.status |= 0x01; /* Set Phase bit */
        nvme_guest_write(nvme, cq_addr, &cqe, sizeof(cqe));
        nvme->admin_cq.head = (cq_tail + 1) %
                              (nvme->admin_cq.size + 1);
        nvme_raise_irq(nvme);
    }
}

/* ── Process Admin Create I/O CQ ────────────────── */
static void nvme_admin_create_io_cq(apple_nvme_state_t *nvme,
                                     nvme_sqe_t *cmd)
{
    uint32_t cdw10 = cmd->cdw10;
    uint32_t cdw11 = cmd->cdw11;
    uint16_t qid = cdw10 & 0xFFFF;
    uint16_t qsize = (cdw10 >> 16) & 0xFFFF;
    uint64_t base_addr = cmd->dptr[0];
    bool pc = (cdw11 & 0x01) != 0;  /* Physically contiguous */
    uint16_t iv = (cdw11 >> 16) & 0xFFFF; /* Interrupt vector */

    nvme_cqe_t cqe;
    memset(&cqe, 0, sizeof(cqe));
    cqe.cid = NVME_SQE_CID(cmd);
    cqe.sq_id = 0;

    if (qid == 0 || qid >= NVME_MAX_QUEUES) {
        NVME_LOG("Create I/O CQ: invalid QID %u", qid);
        cqe.status = nvme_mk_status(NVME_SC_INVALID_FIELD, NVME_SCT_GENERIC);
        goto post_cqe;
    }

    NVME_LOG("Create I/O CQ: qid=%u size=%u base=0x%lx pc=%d iv=%u",
             qid, qsize + 1, (unsigned long)base_addr, pc, iv);

    nvme->io_cq[qid].phys_base = base_addr;
    nvme->io_cq[qid].size = qsize;
    nvme->io_cq[qid].head = 0;
    nvme->io_cq[qid].cq_vector = iv;
    nvme->io_cq[qid].pc = pc;
    nvme->io_cq[qid].id = qid;
    nvme->io_cq[qid].valid = true;

    cqe.status = nvme_mk_status(NVME_SC_SUCCESS, NVME_SCT_GENERIC);

post_cqe:
    if (nvme->admin_cq.valid) {
        uint16_t tail = nvme->admin_cq.head;
        uint64_t cq_addr = nvme->admin_cq.phys_base +
                            tail * sizeof(nvme_cqe_t);
        cqe.status |= 0x01;
        nvme_guest_write(nvme, cq_addr, &cqe, sizeof(cqe));
        nvme->admin_cq.head = (tail + 1) % (nvme->admin_cq.size + 1);
        nvme_raise_irq(nvme);
    }
}

/* ── Process Admin Create I/O SQ ────────────────── */
static void nvme_admin_create_io_sq(apple_nvme_state_t *nvme,
                                     nvme_sqe_t *cmd)
{
    uint32_t cdw10 = cmd->cdw10;
    uint32_t cdw11 = cmd->cdw11;
    uint16_t qid = cdw10 & 0xFFFF;
    uint16_t qsize = (cdw10 >> 16) & 0xFFFF;
    uint64_t base_addr = cmd->dptr[0];
    uint16_t cqid = cdw11 & 0xFFFF;
    bool pc = (cdw11 & 0x01) != 0;

    nvme_cqe_t cqe;
    memset(&cqe, 0, sizeof(cqe));
    cqe.cid = NVME_SQE_CID(cmd);
    cqe.sq_id = 0;

    if (qid == 0 || qid >= NVME_MAX_QUEUES) {
        NVME_LOG("Create I/O SQ: invalid QID %u", qid);
        cqe.status = nvme_mk_status(NVME_SC_INVALID_FIELD, NVME_SCT_GENERIC);
        goto post_cqe;
    }

    if (!nvme->io_cq[cqid].valid) {
        NVME_LOG("Create I/O SQ: CQ %u not created", cqid);
        cqe.status = nvme_mk_status(NVME_SC_INVALID_FIELD, NVME_SCT_GENERIC);
        goto post_cqe;
    }

    NVME_LOG("Create I/O SQ: qid=%u size=%u base=0x%lx cqid=%u",
             qid, qsize + 1, (unsigned long)base_addr, cqid);

    nvme->io_sq[qid].phys_base = base_addr;
    nvme->io_sq[qid].size = qsize;
    nvme->io_sq[qid].head = 0;
    nvme->io_sq[qid].tail = 0;
    nvme->io_sq[qid].pc = pc;
    nvme->io_sq[qid].id = qid;
    nvme->io_sq[qid].valid = true;
    nvme->num_io_queues++;

    cqe.status = nvme_mk_status(NVME_SC_SUCCESS, NVME_SCT_GENERIC);

post_cqe:
    if (nvme->admin_cq.valid) {
        uint16_t tail = nvme->admin_cq.head;
        uint64_t cq_addr = nvme->admin_cq.phys_base +
                            tail * sizeof(nvme_cqe_t);
        cqe.status |= 0x01;
        nvme_guest_write(nvme, cq_addr, &cqe, sizeof(cqe));
        nvme->admin_cq.head = (tail + 1) % (nvme->admin_cq.size + 1);
        nvme_raise_irq(nvme);
    }
}

/* ── Process Admin Delete I/O SQ/CQ ─────────────── */
static void nvme_admin_delete_io_queue(apple_nvme_state_t *nvme,
                                        nvme_sqe_t *cmd, bool is_sq)
{
    uint16_t qid = cmd->cdw10 & 0xFFFF;

    nvme_cqe_t cqe;
    memset(&cqe, 0, sizeof(cqe));
    cqe.cid = NVME_SQE_CID(cmd);
    cqe.sq_id = 0;

    if (qid == 0 || qid >= NVME_MAX_QUEUES) {
        cqe.status = nvme_mk_status(NVME_SC_INVALID_FIELD, NVME_SCT_GENERIC);
        goto post_cqe;
    }

    if (is_sq) {
        NVME_LOG("Delete I/O SQ: qid=%u", qid);
        nvme->io_sq[qid].valid = false;
        if (nvme->num_io_queues > 0) nvme->num_io_queues--;
    } else {
        NVME_LOG("Delete I/O CQ: qid=%u", qid);
        nvme->io_cq[qid].valid = false;
    }

    cqe.status = nvme_mk_status(NVME_SC_SUCCESS, NVME_SCT_GENERIC);

post_cqe:
    if (nvme->admin_cq.valid) {
        uint16_t tail = nvme->admin_cq.head;
        uint64_t cq_addr = nvme->admin_cq.phys_base +
                            tail * sizeof(nvme_cqe_t);
        cqe.status |= 0x01;
        nvme_guest_write(nvme, cq_addr, &cqe, sizeof(cqe));
        nvme->admin_cq.head = (tail + 1) % (nvme->admin_cq.size + 1);
        nvme_raise_irq(nvme);
    }
}

/* ── Process Admin Set/Get Features ─────────────── */
static void nvme_admin_feature(apple_nvme_state_t *nvme,
                                nvme_sqe_t *cmd, bool is_set)
{
    uint8_t fid = cmd->cdw10 & 0xFF;

    nvme_cqe_t cqe;
    memset(&cqe, 0, sizeof(cqe));
    cqe.cid = NVME_SQE_CID(cmd);
    cqe.sq_id = 0;

    if (fid == NVME_FEAT_NUM_QUEUES) {
        if (is_set) {
            NVME_LOG("Set Feature: Number of Queues (cdw11=0x%08x)", cmd->cdw11);
            /* Accept any value, we support up to NVME_MAX_QUEUES */
            cqe.cdw0 = ((NVME_MAX_QUEUES - 1) << 16) | (NVME_MAX_QUEUES - 1);
        } else {
            cqe.cdw0 = ((NVME_MAX_QUEUES - 1) << 16) | (NVME_MAX_QUEUES - 1);
        }
        cqe.status = nvme_mk_status(NVME_SC_SUCCESS, NVME_SCT_GENERIC);
    } else if (fid == NVME_FEAT_WRITE_CACHE) {
        if (is_set) {
            NVME_LOG("Set Feature: Write Cache (cdw11=0x%08x)", cmd->cdw11);
        } else {
            cqe.cdw0 = 0x01; /* Write cache enabled */
        }
        cqe.status = nvme_mk_status(NVME_SC_SUCCESS, NVME_SCT_GENERIC);
    } else {
        NVME_LOG("%s Feature: fid=0x%02x (unsupported)",
                 is_set ? "Set" : "Get", fid);
        cqe.status = nvme_mk_status(NVME_SC_INVALID_FIELD, NVME_SCT_GENERIC);
    }

    if (nvme->admin_cq.valid) {
        uint16_t tail = nvme->admin_cq.head;
        uint64_t cq_addr = nvme->admin_cq.phys_base +
                            tail * sizeof(nvme_cqe_t);
        cqe.status |= 0x01;
        nvme_guest_write(nvme, cq_addr, &cqe, sizeof(cqe));
        nvme->admin_cq.head = (tail + 1) % (nvme->admin_cq.size + 1);
        nvme_raise_irq(nvme);
    }
}

/* ── Process I/O Read/Write ─────────────────────── */
static void nvme_io_rw(apple_nvme_state_t *nvme,
                        nvme_sqe_t *cmd, nvme_queue_t *sq,
                        nvme_queue_t *cq)
{
    uint8_t opcode = cmd->cdw0 & 0xFF;
    uint32_t nsid = cmd->nsid;
    uint64_t slba = ((uint64_t)cmd->cdw11 << 32) | cmd->cdw10;
    uint16_t nlb = (cmd->cdw12 & 0xFFFF) + 1;  /* 0-based */
    uint64_t dptr_addr = cmd->dptr[0];  /* PRP1 */

    nvme_cqe_t cqe;
    memset(&cqe, 0, sizeof(cqe));
    cqe.cid = NVME_SQE_CID(cmd);
    cqe.sq_id = sq->id;

    if (nsid < 1 || nsid > (uint32_t)nvme->num_ns || !nvme->ns[nsid - 1].active) {
        cqe.status = nvme_mk_status(NVME_SC_INVALID_NS, NVME_SCT_GENERIC);
        goto post_cqe;
    }

    nvme_ns_t *ns = &nvme->ns[nsid - 1];
    uint32_t block_size = ns->block_size;

    /* Bounds check */
    if (slba + nlb > ns->size_blocks) {
        NVME_LOG("I/O %s: LBA range error (slba=%lu nlb=%u nsze=%lu)",
                 opcode == NVME_IO_READ ? "Read" : "Write",
                 (unsigned long)slba, nlb, (unsigned long)ns->size_blocks);
        cqe.status = nvme_mk_status(NVME_SC_LBA_RANGE, NVME_SCT_GENERIC);
        goto post_cqe;
    }

    if (!nvme->disk_image) {
        cqe.status = nvme_mk_status(NVME_SC_INTERNAL, NVME_SCT_GENERIC);
        goto post_cqe;
    }

    uint64_t byte_offset = slba * block_size;
    uint64_t byte_count = (uint64_t)nlb * block_size;

    if (opcode == NVME_IO_READ) {
        /* Read from disk image → guest memory */
        uint8_t buf[131072]; /* 128KB max transfer */
        if (byte_count > sizeof(buf)) {
            cqe.status = nvme_mk_status(NVME_SC_DATA_TRANSFER, NVME_SCT_GENERIC);
            goto post_cqe;
        }

        if (fseek(nvme->disk_image, byte_offset, SEEK_SET) != 0 ||
            fread(buf, 1, byte_count, nvme->disk_image) != byte_count) {
            NVME_LOG("Read error at offset 0x%lx", (unsigned long)byte_offset);
            cqe.status = nvme_mk_status(NVME_SC_INTERNAL, NVME_SCT_GENERIC);
            goto post_cqe;
        }

        if (nvme_guest_write(nvme, dptr_addr, buf, byte_count) < 0) {
            NVME_LOG("DMA write failed for Read");
            cqe.status = nvme_mk_status(NVME_SC_DATA_TRANSFER, NVME_SCT_GENERIC);
            goto post_cqe;
        }

        NVME_DBG("Read: lba=%lu nlb=%u → OK", (unsigned long)slba, nlb);
    } else if (opcode == NVME_IO_WRITE) {
        /* Read from guest memory → disk image */
        if (nvme->read_only) {
            cqe.status = nvme_mk_status(NVME_SC_INVALID_OPCODE, NVME_SCT_GENERIC);
            goto post_cqe;
        }

        uint8_t buf[131072];
        if (byte_count > sizeof(buf)) {
            cqe.status = nvme_mk_status(NVME_SC_DATA_TRANSFER, NVME_SCT_GENERIC);
            goto post_cqe;
        }

        if (nvme_guest_read(nvme, dptr_addr, buf, byte_count) < 0) {
            NVME_LOG("DMA read failed for Write");
            cqe.status = nvme_mk_status(NVME_SC_DATA_TRANSFER, NVME_SCT_GENERIC);
            goto post_cqe;
        }

        if (fseek(nvme->disk_image, byte_offset, SEEK_SET) != 0 ||
            fwrite(buf, 1, byte_count, nvme->disk_image) != byte_count) {
            NVME_LOG("Write error at offset 0x%lx", (unsigned long)byte_offset);
            cqe.status = nvme_mk_status(NVME_SC_INTERNAL, NVME_SCT_GENERIC);
            goto post_cqe;
        }

        fflush(nvme->disk_image);
        NVME_DBG("Write: lba=%lu nlb=%u → OK", (unsigned long)slba, nlb);
    } else if (opcode == NVME_IO_FLUSH) {
        if (nvme->disk_image)
            fflush(nvme->disk_image);
    }

    cqe.status = nvme_mk_status(NVME_SC_SUCCESS, NVME_SCT_GENERIC);

post_cqe:
    if (cq->valid) {
        uint16_t tail = cq->head;
        uint64_t cq_addr = cq->phys_base + tail * sizeof(nvme_cqe_t);
        cqe.status |= 0x01;
        nvme_guest_write(nvme, cq_addr, &cqe, sizeof(cqe));
        cq->head = (tail + 1) % (cq->size + 1);
        nvme_raise_irq(nvme);
    }
}

/* ── Process a single admin command ─────────────── */
static void nvme_process_admin_cmd(apple_nvme_state_t *nvme)
{
    nvme_sqe_t cmd;
    uint16_t tail = nvme->admin_sq.tail;
    uint16_t head = nvme->admin_sq.head;

    if (head == tail) return; /* Empty */

    while (head != tail) {
        uint64_t cmd_addr = nvme->admin_sq.phys_base +
                             head * sizeof(nvme_sqe_t);
        if (nvme_guest_read(nvme, cmd_addr, &cmd, sizeof(cmd)) < 0) {
            NVME_LOG("DMA read failed for admin SQE");
            break;
        }

        uint8_t opcode = cmd.cdw0 & 0xFF;
        NVME_DBG("Admin cmd: opcode=0x%02x cid=%u", opcode, NVME_SQE_CID(&cmd));

        switch (opcode) {
        case NVME_ADMIN_IDENTIFY:
            nvme_admin_identify(nvme, &cmd);
            break;
        case NVME_ADMIN_CREATE_IO_CQ:
            nvme_admin_create_io_cq(nvme, &cmd);
            break;
        case NVME_ADMIN_CREATE_IO_SQ:
            nvme_admin_create_io_sq(nvme, &cmd);
            break;
        case NVME_ADMIN_DELETE_IO_SQ:
            nvme_admin_delete_io_queue(nvme, &cmd, true);
            break;
        case NVME_ADMIN_DELETE_IO_CQ:
            nvme_admin_delete_io_queue(nvme, &cmd, false);
            break;
        case NVME_ADMIN_SET_FEATURES:
            nvme_admin_feature(nvme, &cmd, true);
            break;
        case NVME_ADMIN_GET_FEATURES:
            nvme_admin_feature(nvme, &cmd, false);
            break;
        case NVME_ADMIN_ABORT: {
            /* Acknowledge abort */
            nvme_cqe_t cqe;
            memset(&cqe, 0, sizeof(cqe));
            cqe.cid = NVME_SQE_CID(&cmd);
            cqe.status = nvme_mk_status(NVME_SC_ABORT_REQUESTED, NVME_SCT_GENERIC);
            if (nvme->admin_cq.valid) {
                uint16_t ct = nvme->admin_cq.head;
                uint64_t ca = nvme->admin_cq.phys_base + ct * sizeof(nvme_cqe_t);
                cqe.status |= 0x01;
                nvme_guest_write(nvme, ca, &cqe, sizeof(cqe));
                nvme->admin_cq.head = (ct + 1) % (nvme->admin_cq.size + 1);
            }
            break;
        }
        case NVME_ADMIN_GET_LOG_PAGE:
            /* Minimal: just succeed */
            NVME_LOG("Get Log Page (stub)");
            break;
        default:
            NVME_LOG("Unknown admin opcode: 0x%02x", opcode);
            break;
        }

        head = (head + 1) % (nvme->admin_sq.size + 1);
    }

    nvme->admin_sq.head = head;
}

/* ── Process I/O commands from a specific queue ─── */
static void nvme_process_io_sq(apple_nvme_state_t *nvme, int sqid)
{
    nvme_queue_t *sq = &nvme->io_sq[sqid];
    nvme_queue_t *cq = &nvme->io_cq[sqid];

    if (!sq->valid || !cq->valid) return;

    uint16_t tail = sq->tail;
    uint16_t head = sq->head;

    if (head == tail) return;

    while (head != tail) {
        nvme_sqe_t cmd;
        uint64_t cmd_addr = sq->phys_base + head * sizeof(nvme_sqe_t);
        if (nvme_guest_read(nvme, cmd_addr, &cmd, sizeof(cmd)) < 0) {
            NVME_LOG("DMA read failed for I/O SQE (sqid=%d)", sqid);
            break;
        }

        uint8_t opcode = cmd.cdw0 & 0xFF;

        switch (opcode) {
        case NVME_IO_READ:
        case NVME_IO_WRITE:
        case NVME_IO_FLUSH:
            nvme_io_rw(nvme, &cmd, sq, cq);
            break;
        default:
            NVME_LOG("Unknown I/O opcode: 0x%02x (sqid=%d)", opcode, sqid);
            break;
        }

        head = (head + 1) % (sq->size + 1);
    }

    sq->head = head;
}

/* ═══════════════════════════════════════════════════
 *  MMIO Interface
 * ═══════════════════════════════════════════════════ */

/* ── Initialize ─────────────────────────────────── */
void apple_nvme_init(apple_nvme_state_t *nvme)
{
    memset(nvme, 0, sizeof(*nvme));

    /* NVMe 1.4 version */
    nvme->vs = 0x00010400;  /* Major=1, Minor=4, Patch=0 */

    /* CAP: 256 max entries, 4KB page min, 500ms timeout, doorbell stride=0 */
    nvme->cap = 0;
    nvme->cap |= (255ULL & NVME_CAP_MQES_MASK);        /* MQES=255 (256 entries) */
    nvme->cap |= (1ULL << NVME_CAP_CQR_SHIFT);          /* Contiguous queues */
    nvme->cap |= (5ULL << NVME_CAP_TO_SHIFT);            /* 2.5s timeout */
    nvme->cap |= (0ULL << NVME_CAP_DSTRD_SHIFT);         /* 4-byte stride */
    nvme->cap |= (0ULL << NVME_CAP_CSS_SHIFT);            /* NVM command set */
    nvme->cap |= (0ULL << NVME_CAP_MPSMIN_SHIFT);         /* 4KB min page */
    nvme->cap |= (0ULL << NVME_CAP_MPSMAX_SHIFT);         /* 4KB max page */

    /* Default controller info */
    strncpy(nvme->serial, "SV-NVME-0001", sizeof(nvme->serial) - 1);
    strncpy(nvme->model, "SiliconV NVMe Controller", sizeof(nvme->model) - 1);
    strncpy(nvme->firmware, "0.1.0", sizeof(nvme->firmware) - 1);

    /* One namespace by default */
    nvme->num_ns = 1;
    nvme->ns[0].block_size = 512;
    nvme->ns[0].active = false; /* Set to true when disk is attached */

    NVME_LOG("Initialized (NVMe 1.4, Apple profile)");
}

/* ── Attach disk image ──────────────────────────── */
int apple_nvme_attach_disk(apple_nvme_state_t *nvme,
                            const char *path, bool read_only)
{
    const char *mode = read_only ? "rb" : "r+b";
    nvme->disk_image = fopen(path, mode);
    if (!nvme->disk_image) {
        NVME_LOG("Failed to open disk image: %s (%s)", path, strerror(errno));
        return -1;
    }

    /* Get file size */
    fseek(nvme->disk_image, 0, SEEK_END);
    nvme->disk_size = ftell(nvme->disk_image);
    fseek(nvme->disk_image, 0, SEEK_SET);

    nvme->read_only = read_only;

    /* Setup namespace */
    nvme->ns[0].size_blocks = nvme->disk_size / nvme->ns[0].block_size;
    nvme->ns[0].capacity = nvme->disk_size;
    nvme->ns[0].active = true;

    NVME_LOG("Disk attached: %s (%lu MB, %s)",
             path,
             (unsigned long)(nvme->disk_size / (1024 * 1024)),
             read_only ? "read-only" : "read-write");

    return 0;
}

/* ── DMA / IRQ wiring ───────────────────────────── */
void apple_nvme_set_dma(apple_nvme_state_t *nvme,
                         nvme_dma_read_fn read_fn,
                         nvme_dma_write_fn write_fn,
                         void *ctx)
{
    nvme->dma_read = read_fn;
    nvme->dma_write = write_fn;
    nvme->dma_ctx = ctx;
}

void apple_nvme_set_irq_ctx(apple_nvme_state_t *nvme, void *ctx)
{
    nvme->irq_context = ctx;
}

void apple_nvme_set_irq_callbacks(apple_nvme_state_t *nvme,
                                   void (*raise)(void *ctx, int irq),
                                   void (*lower)(void *ctx, int irq))
{
    nvme->irq_raise = raise;
    nvme->irq_lower = lower;
}

/* ── MMIO Read ──────────────────────────────────── */
uint64_t apple_nvme_mmio_read(apple_nvme_state_t *nvme,
                               uint64_t offset, int size)
{
    (void)size;

    /* Register region (0x00 - 0x3F) */
    if (offset < NVME_DOORBELL_BASE) {
        switch (offset) {
        case NVME_REG_CAP:
            return nvme->cap & 0xFFFFFFFF;
        case NVME_REG_CAP + 4:
            return nvme->cap >> 32;
        case NVME_REG_VS:
            return nvme->vs;
        case NVME_REG_INTMS:
            return nvme->intms;
        case NVME_REG_INTMC:
            return nvme->intmc;
        case NVME_REG_CC:
            return nvme->cc;
        case NVME_REG_CSTS:
            return nvme->csts;
        case NVME_REG_AQA:
            return nvme->aqa;
        case NVME_REG_ASQ:
            return nvme->asq & 0xFFFFFFFF;
        case NVME_REG_ASQ + 4:
            return nvme->asq >> 32;
        case NVME_REG_ACQ:
            return nvme->acq & 0xFFFFFFFF;
        case NVME_REG_ACQ + 4:
            return nvme->acq >> 32;
        case NVME_REG_CMBLOC:
            return 0; /* No CMB */
        case NVME_REG_CMBSZ:
            return 0;
        default:
            NVME_DBG("Read unknown reg 0x%lx", (unsigned long)offset);
            return 0;
        }
    }

    /* Doorbell region (0x1000+) */
    if (offset >= NVME_DOORBELL_BASE) {
        uint32_t db_offset = offset - NVME_DOORBELL_BASE;
        uint32_t qid = db_offset / NVME_DOORBELL_STRIDE;
        bool is_cq = (db_offset / NVME_DOORBELL_STRIDE) & 1;
        /* Actually: doorbell layout is SQ0_TDBL, CQ0_HDBL, SQ1_TDBL, CQ1_HDBL... */
        /* So qid = db_offset / (2 * STRIDE), is_cq = (db_offset / STRIDE) & 1 */
        uint32_t doorbell_idx = db_offset / NVME_DOORBELL_STRIDE;
        qid = doorbell_idx / 2;
        is_cq = doorbell_idx & 1;

        if (qid == 0) {
            if (is_cq)
                return nvme->admin_cq.head;
            else
                return nvme->admin_sq.tail;
        } else if (qid < NVME_MAX_QUEUES) {
            if (is_cq)
                return nvme->io_cq[qid].valid ? nvme->io_cq[qid].head : 0;
            else
                return nvme->io_sq[qid].valid ? nvme->io_sq[qid].tail : 0;
        }
    }

    return 0;
}

/* ── MMIO Write ─────────────────────────────────── */
void apple_nvme_mmio_write(apple_nvme_state_t *nvme,
                            uint64_t offset, uint64_t value, int size)
{
    (void)size;

    /* Register region */
    if (offset < NVME_DOORBELL_BASE) {
        switch (offset) {
        case NVME_REG_CC: {
            uint32_t old_cc = nvme->cc;
            nvme->cc = (uint32_t)value;

            /* Check for enable transition */
            bool was_enabled = (old_cc & NVME_CC_EN) != 0;
            bool now_enabled = (nvme->cc & NVME_CC_EN) != 0;

            if (!was_enabled && now_enabled) {
                /* Controller enable */
                NVME_LOG("Controller Enable");
                nvme->enabled = true;

                /* Parse admin queue attributes */
                uint32_t aqa = nvme->aqa;
                uint16_t asqs = (aqa & 0xFFF);          /* Admin SQ size */
                uint16_t acqs = (aqa >> 16) & 0xFFF;    /* Admin CQ size */

                nvme->admin_sq.phys_base = nvme->asq;
                nvme->admin_sq.size = asqs;
                nvme->admin_sq.head = 0;
                nvme->admin_sq.tail = 0;
                nvme->admin_sq.valid = true;

                nvme->admin_cq.phys_base = nvme->acq;
                nvme->admin_cq.size = acqs;
                nvme->admin_cq.head = 0;
                nvme->admin_cq.valid = true;

                NVME_LOG("Admin SQ: base=0x%lx size=%u",
                         (unsigned long)nvme->asq, asqs + 1);
                NVME_LOG("Admin CQ: base=0x%lx size=%u",
                         (unsigned long)nvme->acq, acqs + 1);

                /* Set CSTS.RDY */
                nvme->csts |= NVME_CSTS_RDY;
                nvme->ready = true;
            } else if (was_enabled && !now_enabled) {
                /* Controller disable */
                NVME_LOG("Controller Disable");
                nvme->enabled = false;
                nvme->ready = false;
                nvme->csts &= ~NVME_CSTS_RDY;
                nvme->admin_sq.valid = false;
                nvme->admin_cq.valid = false;
                for (int i = 0; i < NVME_MAX_QUEUES; i++) {
                    nvme->io_sq[i].valid = false;
                    nvme->io_cq[i].valid = false;
                }
                nvme->num_io_queues = 0;
            }

            /* Shutdown notification */
            uint32_t shn = (nvme->cc & NVME_CC_SHN_MASK) >> NVME_CC_SHN_SHIFT;
            if (shn) {
                NVME_LOG("Shutdown notification: shn=%u", shn);
                nvme->csts |= (shn << NVME_CSTS_SHST_SHIFT);
            }
            break;
        }
        case NVME_REG_AQA:
            nvme->aqa = (uint32_t)value;
            break;
        case NVME_REG_ASQ:
            nvme->asq = (nvme->asq & 0xFFFFFFFF00000000ULL) | (uint32_t)value;
            break;
        case NVME_REG_ASQ + 4:
            nvme->asq = (nvme->asq & 0xFFFFFFFFULL) | ((uint64_t)(uint32_t)value << 32);
            break;
        case NVME_REG_ACQ:
            nvme->acq = (nvme->acq & 0xFFFFFFFF00000000ULL) | (uint32_t)value;
            break;
        case NVME_REG_ACQ + 4:
            nvme->acq = (nvme->acq & 0xFFFFFFFFULL) | ((uint64_t)(uint32_t)value << 32);
            break;
        case NVME_REG_INTMS:
            nvme->intms |= (uint32_t)value;
            break;
        case NVME_REG_INTMC:
            nvme->intms &= ~(uint32_t)value;
            break;
        default:
            NVME_DBG("Write unknown reg 0x%lx = 0x%lx",
                     (unsigned long)offset, (unsigned long)value);
            break;
        }
        return;
    }

    /* Doorbell region */
    if (offset >= NVME_DOORBELL_BASE) {
        uint32_t doorbell_idx = (offset - NVME_DOORBELL_BASE) / NVME_DOORBELL_STRIDE;
        uint32_t qid = doorbell_idx / 2;
        bool is_cq = doorbell_idx & 1;

        if (!nvme->enabled) return;

        if (qid == 0) {
            if (is_cq) {
                /* CQ0 Head Doorbell */
                nvme->admin_cq.head = (uint16_t)value;
                NVME_DBG("Admin CQ head → %u", (uint16_t)value);
            } else {
                /* SQ0 Tail Doorbell — process admin commands */
                nvme->admin_sq.tail = (uint16_t)value;
                NVME_DBG("Admin SQ tail → %u", (uint16_t)value);
                nvme_process_admin_cmd(nvme);
            }
        } else if (qid < NVME_MAX_QUEUES) {
            if (is_cq) {
                if (nvme->io_cq[qid].valid)
                    nvme->io_cq[qid].head = (uint16_t)value;
            } else {
                if (nvme->io_sq[qid].valid) {
                    nvme->io_sq[qid].tail = (uint16_t)value;
                    nvme_process_io_sq(nvme, qid);
                }
            }
        }
    }
}

/* ── Destroy ────────────────────────────────────── */
void apple_nvme_destroy(apple_nvme_state_t *nvme)
{
    if (nvme->disk_image) {
        fclose(nvme->disk_image);
        nvme->disk_image = NULL;
    }
    NVME_LOG("Destroyed");
}
