#ifndef __CPU_AXI_H
#define __CPU_AXI_H

#define CPU_AXI_QOS_PRIORITY    0x08
#define CPU_AXI_QOS_MODE        0x0c
#define CPU_AXI_QOS_BANDWIDTH   0x10
#define CPU_AXI_QOS_SATURATION  0x14
#define CPU_AXI_QOS_EXTCONTROL  0x18

#define CPU_AXI_QOS_MODE_NONE           0
#define CPU_AXI_QOS_MODE_FIXED          1
#define CPU_AXI_QOS_MODE_LIMITER        2
#define CPU_AXI_QOS_MODE_REGULATOR      3

#define CPU_AXI_QOS_PRIORITY_LEVEL(h, l) \
	((((h) & 3) << 8) | (((h) & 3) << 2) | ((l) & 3))
#define CPU_AXI_SET_QOS_PRIORITY(h, l, base) \
	writel_relaxed(CPU_AXI_QOS_PRIORITY_LEVEL(h, l), base + CPU_AXI_QOS_PRIORITY)

#define CPU_AXI_SET_QOS_MODE(mode, base) \
	writel_relaxed((mode) & 3, base + CPU_AXI_QOS_MODE)

#define CPU_AXI_SET_QOS_BANDWIDTH(bandwidth, base) \
	writel_relaxed((bandwidth) & 0x7ff, base + CPU_AXI_QOS_BANDWIDTH)

#define CPU_AXI_SET_QOS_SATURATION(saturation, base) \
	writel_relaxed((saturation) & 0x3ff, base + CPU_AXI_QOS_SATURATION)

#define CPU_AXI_SET_QOS_EXTCONTROL(extcontrol, base) \
	writel_relaxed((extcontrol) & 7, base + CPU_AXI_QOS_EXTCONTROL)

#define CPU_AXI_QOS_NUM_REGS 5
#define CPU_AXI_SAVE_QOS(array, base) do { \
	array[0] = readl_relaxed(base + CPU_AXI_QOS_PRIORITY); \
	array[1] = readl_relaxed(base + CPU_AXI_QOS_MODE); \
	array[2] = readl_relaxed(base + CPU_AXI_QOS_BANDWIDTH); \
	array[3] = readl_relaxed(base + CPU_AXI_QOS_SATURATION); \
	array[4] = readl_relaxed(base + CPU_AXI_QOS_EXTCONTROL); \
} while (0)
#define CPU_AXI_RESTORE_QOS(array, base) do { \
	writel_relaxed(array[0], base + CPU_AXI_QOS_PRIORITY); \
	writel_relaxed(array[1], base + CPU_AXI_QOS_MODE); \
	writel_relaxed(array[2], base + CPU_AXI_QOS_BANDWIDTH); \
	writel_relaxed(array[3], base + CPU_AXI_QOS_SATURATION); \
	writel_relaxed(array[4], base + CPU_AXI_QOS_EXTCONTROL); \
} while (0)

#define RK3188_CPU_AXI_DMAC_QOS_VIRT    (RK_CPU_AXI_BUS_VIRT + 0x1000)
#define RK3188_CPU_AXI_CPU0_QOS_VIRT    (RK_CPU_AXI_BUS_VIRT + 0x2000)
#define RK3188_CPU_AXI_CPU1R_QOS_VIRT   (RK_CPU_AXI_BUS_VIRT + 0x2080)
#define RK3188_CPU_AXI_CPU1W_QOS_VIRT   (RK_CPU_AXI_BUS_VIRT + 0x2100)
#define RK3188_CPU_AXI_PERI_QOS_VIRT    (RK_CPU_AXI_BUS_VIRT + 0x4000)
#define RK3188_CPU_AXI_GPU_QOS_VIRT     (RK_CPU_AXI_BUS_VIRT + 0x5000)
#define RK3188_CPU_AXI_VPU_QOS_VIRT     (RK_CPU_AXI_BUS_VIRT + 0x6000)
#define RK3188_CPU_AXI_LCDC0_QOS_VIRT   (RK_CPU_AXI_BUS_VIRT + 0x7000)
#define RK3188_CPU_AXI_CIF0_QOS_VIRT    (RK_CPU_AXI_BUS_VIRT + 0x7080)
#define RK3188_CPU_AXI_IPP_QOS_VIRT     (RK_CPU_AXI_BUS_VIRT + 0x7100)
#define RK3188_CPU_AXI_LCDC1_QOS_VIRT   (RK_CPU_AXI_BUS_VIRT + 0x7180)
#define RK3188_CPU_AXI_CIF1_QOS_VIRT    (RK_CPU_AXI_BUS_VIRT + 0x7200)
#define RK3188_CPU_AXI_RGA_QOS_VIRT     (RK_CPU_AXI_BUS_VIRT + 0x7280)

/* service core */
#define RK3288_SERVICE_CORE_VIRT                RK_CPU_AXI_BUS_VIRT
#define RK3288_CPU_AXI_CPUM_R_QOS_VIRT          (RK3288_SERVICE_CORE_VIRT + 0x80)
#define RK3288_CPU_AXI_CPUM_W_QOS_VIRT          (RK3288_SERVICE_CORE_VIRT + 0x100)
#define RK3288_CPU_AXI_CPUP_QOS_VIRT            (RK3288_SERVICE_CORE_VIRT + 0x0)
/* service dmac */
#define RK3288_SERVICE_DMAC_VIRT                (RK3288_SERVICE_CORE_VIRT + RK3288_SERVICE_CORE_SIZE)
#define RK3288_CPU_AXI_BUS_DMAC_QOS_VIRT        (RK3288_SERVICE_DMAC_VIRT + 0x0)
#define RK3288_CPU_AXI_CCP_QOS_VIRT             (RK3288_SERVICE_DMAC_VIRT + 0x180)
#define RK3288_CPU_AXI_CRYPTO_QOS_VIRT          (RK3288_SERVICE_DMAC_VIRT + 0x100)
#define RK3288_CPU_AXI_CCS_QOS_VIRT             (RK3288_SERVICE_DMAC_VIRT + 0x200)
#define RK3288_CPU_AXI_HOST_QOS_VIRT            (RK3288_SERVICE_DMAC_VIRT + 0x80)
/* service gpu */
#define RK3288_SERVICE_GPU_VIRT                 (RK3288_SERVICE_DMAC_VIRT + RK3288_SERVICE_DMAC_SIZE)
#define RK3288_CPU_AXI_GPU_R_QOS_VIRT           (RK3288_SERVICE_GPU_VIRT + 0x0)
#define RK3288_CPU_AXI_GPU_W_QOS_VIRT           (RK3288_SERVICE_GPU_VIRT + 0x80)
/* service peri */
#define RK3288_SERVICE_PERI_VIRT                (RK3288_SERVICE_GPU_VIRT + RK3288_SERVICE_GPU_SIZE)
#define RK3288_CPU_AXI_PERI_QOS_VIRT            (RK3288_SERVICE_PERI_VIRT + 0x0)
/* service bus */
#define RK3288_SERVICE_BUS_VIRT                 (RK3288_SERVICE_PERI_VIRT + RK3288_SERVICE_PERI_SIZE)
/* service vio */
#define RK3288_SERVICE_VIO_VIRT                 (RK3288_SERVICE_BUS_VIRT + RK3288_SERVICE_BUS_SIZE)
#define RK3288_CPU_AXI_VIO0_IEP_QOS_VIRT        (RK3288_SERVICE_VIO_VIRT + 0x500)
#define RK3288_CPU_AXI_VIO0_VIP_QOS_VIRT        (RK3288_SERVICE_VIO_VIRT + 0x480)
#define RK3288_CPU_AXI_VIO0_VOP_QOS_VIRT        (RK3288_SERVICE_VIO_VIRT + 0x400)
#define RK3288_CPU_AXI_VIO1_ISP_R_QOS_VIRT      (RK3288_SERVICE_VIO_VIRT + 0x900)
#define RK3288_CPU_AXI_VIO1_ISP_W0_QOS_VIRT     (RK3288_SERVICE_VIO_VIRT + 0x100)
#define RK3288_CPU_AXI_VIO1_ISP_W1_QOS_VIRT     (RK3288_SERVICE_VIO_VIRT + 0x180)
#define RK3288_CPU_AXI_VIO1_VOP_QOS_VIRT        (RK3288_SERVICE_VIO_VIRT + 0x0)
#define RK3288_CPU_AXI_VIO2_RGA_R_QOS_VIRT      (RK3288_SERVICE_VIO_VIRT + 0x800)
#define RK3288_CPU_AXI_VIO2_RGA_W_QOS_VIRT      (RK3288_SERVICE_VIO_VIRT + 0x880)
/* service video */
#define RK3288_SERVICE_VIDEO_VIRT               (RK3288_SERVICE_VIO_VIRT + RK3288_SERVICE_VIO_SIZE)
#define RK3288_CPU_AXI_VIDEO_QOS_VIRT           (RK3288_SERVICE_VIDEO_VIRT + 0x0)
/* service hevc */
#define RK3288_SERVICE_HEVC_VIRT                (RK3288_SERVICE_VIDEO_VIRT + RK3288_SERVICE_VIDEO_SIZE)
#define RK3288_CPU_AXI_HEVC_R_QOS_VIRT          (RK3288_SERVICE_HEVC_VIRT + 0x0)
#define RK3288_CPU_AXI_HEVC_W_QOS_VIRT          (RK3288_SERVICE_HEVC_VIRT + 0x100)

#define RK312X_CPU_AXI_QOS_NUM_REGS 4
#define RK312X_CPU_AXI_SAVE_QOS(array, base) do { \
	array[0] = readl_relaxed(base + CPU_AXI_QOS_PRIORITY); \
	array[1] = readl_relaxed(base + CPU_AXI_QOS_MODE); \
	array[2] = readl_relaxed(base + CPU_AXI_QOS_BANDWIDTH); \
	array[3] = readl_relaxed(base + CPU_AXI_QOS_SATURATION); \
} while (0)
#define RK312X_CPU_AXI_RESTORE_QOS(array, base) do { \
	writel_relaxed(array[0], base + CPU_AXI_QOS_PRIORITY); \
	writel_relaxed(array[1], base + CPU_AXI_QOS_MODE); \
	writel_relaxed(array[2], base + CPU_AXI_QOS_BANDWIDTH); \
	writel_relaxed(array[3], base + CPU_AXI_QOS_SATURATION); \
} while (0)
#define RK312X_SERVICE_VIO_VIRT                 (RK_CPU_AXI_BUS_VIRT + 0x7000)

#define RK312X_CPU_AXI_VIO_RGA_QOS_VIRT        (RK312X_SERVICE_VIO_VIRT)
#define RK312X_CPU_AXI_VIO_EBC_QOS_VIRT        (RK312X_SERVICE_VIO_VIRT + 0x80)
#define RK312X_CPU_AXI_VIO_IEP_QOS_VIRT      (RK312X_SERVICE_VIO_VIRT + 0x100)
#define RK312X_CPU_AXI_VIO_LCDC0_QOS_VIRT     (RK312X_SERVICE_VIO_VIRT + 0x180)
#define RK312X_CPU_AXI_VIO_VIP0_QOS_VIRT     (RK312X_SERVICE_VIO_VIRT + 0x200)

#define RK312X_SERVICE_GPU_VIRT                 (RK_CPU_AXI_BUS_VIRT + 0x5000)
#define RK312X_CPU_AXI_GPU_QOS_VIRT        (RK312X_SERVICE_GPU_VIRT)

#define RK312X_SERVICE_VIDEO_VIRT                 (RK_CPU_AXI_BUS_VIRT + 0x6000)
#define RK312X_CPU_AXI_VIDEO_QOS_VIRT        (RK312X_SERVICE_VIDEO_VIRT)
#endif
