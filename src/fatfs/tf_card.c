#include "pico.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#if !defined(BP_HOST_TEST)
#include "hardware/dma.h"
#endif
#include "pirate.h"
#include <string.h>
#include "system_config.h"

#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "fatfs/tf_card.h"
#include "kernel/bp_resource.h"

/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

/* MMC/SD command */
#define CMD0	(0)			/* GO_IDLE_STATE */
#define CMD1	(1)			/* SEND_OP_COND (MMC) */ 
#define	ACMD41	(0x80+41)	/* SEND_OP_COND (SDC) */ //(1)
#define CMD8	(8)			/* SEND_IF_COND */
#define CMD9	(9)			/* SEND_CSD */
#define CMD10	(10)		/* SEND_CID */
#define CMD12	(12)		/* STOP_TRANSMISSION */
#define ACMD13	(0x80+13)	/* SD_STATUS (SDC) */ //(9)
#define CMD16	(16)		/* SET_BLOCKLEN */
#define CMD17	(17)		/* READ_SINGLE_BLOCK */
#define CMD18	(18)		/* READ_MULTIPLE_BLOCK */
#define CMD23	(23)		/* SET_BLOCK_COUNT (MMC) */
#define	ACMD23	(0x80+23)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24	(24)		/* WRITE_BLOCK */
#define CMD25	(25)		/* WRITE_MULTIPLE_BLOCK */
#define CMD32	(32)		/* ERASE_ER_BLK_START */
#define CMD33	(33)		/* ERASE_ER_BLK_END */
#define CMD38	(38)		/* ERASE */
#define CMD55	(55)		/* APP_CMD */
#define CMD58	(58)		/* READ_OCR */

/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC         0x01            /* MMC ver 3 */
#define CT_SD1         0x02            /* SD ver 1 */
#define CT_SD2         0x04            /* SD ver 2 */
#define CT_SDC         (CT_SD1|CT_SD2) /* SD */
#define CT_BLOCK       0x08            /* Block addressing */

#define CLK_SLOW	(100000)
#ifndef CLK_FAST
#define CLK_FAST	BP_SPI_HIGH_SPEED
#endif

static volatile
DSTATUS Stat = STA_NOINIT;	/* Physical drive status */

static
BYTE CardType;			/* Card type flags */

/* Hardware SPI DMA channel state */
static int32_t s_storage_dma_rx = -1;
static int32_t s_storage_dma_tx = -1;
static const uint8_t s_storage_dummy_tx = 0xFF;
static uint8_t s_storage_dummy_rx = 0;

/* Authoritative card metadata and media generation */
static tf_card_metadata_t s_tf_card_meta = {0};
static uint32_t s_media_generation = 0;

bool tf_card_parse_csd(const uint8_t csd[16], tf_card_metadata_t *out_meta) {
    if (!csd || !out_meta) {
        return false;
    }
    memset(out_meta, 0, sizeof(tf_card_metadata_t));
    out_meta->sector_size = 512;

    /* Check for all zeros or all 0xFF (unpowered or floating bus) */
    bool all_zero = true;
    bool all_ones = true;
    for (int i = 0; i < 16; i++) {
        if (csd[i] != 0x00) all_zero = false;
        if (csd[i] != 0xFF) all_ones = false;
    }
    if (all_zero || all_ones) {
        return false;
    }

    uint8_t csd_struct = (csd[0] >> 6) & 0x03;

    if (csd_struct == 0) {
        /* CSD Version 1.0: Standard Capacity SD (SDSC, <= 2GB) */
        out_meta->csd_version = 1;
        out_meta->addr_mode = TF_CARD_ADDR_BYTE;

        uint8_t read_bl_len = csd[5] & 0x0F;
        if (read_bl_len < 9 || read_bl_len > 11) {
            /* Sector length must be 512 (9), 1024 (10), or 2048 (11) bytes */
            return false;
        }

        uint16_t c_size = (uint16_t)(((csd[6] & 0x03) << 10) |
                                     ((uint16_t)csd[7] << 2) |
                                     ((csd[8] & 0xC0) >> 6));
        uint8_t c_size_mult = (uint8_t)(((csd[9] & 0x03) << 1) |
                                        ((csd[10] & 0x80) >> 7));

        /* Number of sectors = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2 + READ_BL_LEN - 9) */
        uint32_t shift = (uint32_t)c_size_mult + 2 + read_bl_len - 9;
        if (shift >= 31) {
            return false;
        }

        uint64_t sectors = ((uint64_t)c_size + 1ULL) << shift;
        if (sectors == 0 || sectors > 4194304ULL) {
            /* Max 2GB (4,194,304 512-byte sectors) for SDSC */
            return false;
        }

        out_meta->sector_count = sectors;
        out_meta->valid = true;
        out_meta->write_protected = ((csd[14] & 0x30) != 0 || (csd[14] & 0x40) != 0);
        return true;

    } else if (csd_struct == 1) {
        /* CSD Version 2.0: High Capacity / Extended Capacity (SDHC/SDXC, 2GB to 2TB) */
        out_meta->csd_version = 2;
        out_meta->addr_mode = TF_CARD_ADDR_BLOCK;

        uint32_t c_size = (((uint32_t)(csd[7] & 0x3F) << 16) |
                           ((uint32_t)csd[8] << 8) |
                           (uint32_t)csd[9]);

        /* Max C_SIZE is 0x3FFFFF (4,194,303), yielding exactly 4,294,967,296 sectors (2TB) */
        if (c_size > 0x3FFFFFUL) {
            return false;
        }

        uint64_t sectors = ((uint64_t)c_size + 1ULL) * 1024ULL;
        if (sectors < 4096ULL) {
            /* SDHC requires minimum capacity >= 2MB */
            return false;
        }

        out_meta->sector_count = sectors;
        out_meta->valid = true;
        out_meta->write_protected = ((csd[14] & 0x30) != 0 || (csd[14] & 0x40) != 0);
        return true;

    } else if (csd_struct == 2) {
        /* CSD Version 3.0: SDUC (> 2TB up to 128TB) - unsupported address width */
        out_meta->csd_version = 3;
        out_meta->addr_mode = TF_CARD_ADDR_UNSUPPORTED;
        out_meta->valid = false;
        return false;

    } else {
        /* Reserved CSD version */
        out_meta->csd_version = 0;
        out_meta->addr_mode = TF_CARD_ADDR_UNSUPPORTED;
        out_meta->valid = false;
        return false;
    }
}

bool tf_card_get_metadata(tf_card_metadata_t *out_meta) {
    if (!out_meta) {
        return false;
    }
    if (!s_tf_card_meta.valid || (Stat & STA_NOINIT)) {
        memset(out_meta, 0, sizeof(tf_card_metadata_t));
        return false;
    }
    *out_meta = s_tf_card_meta;
    return true;
}

void tf_card_invalidate_metadata(void) {
    memset(&s_tf_card_meta, 0, sizeof(s_tf_card_meta));
    Stat |= STA_NOINIT;
}

uint32_t tf_card_get_media_generation(void) {
    if (!s_tf_card_meta.valid || (Stat & STA_NOINIT)) {
        return 0;
    }
    return s_tf_card_meta.media_generation;
}

int32_t tf_card_get_dma_rx_channel(void) {
    return s_storage_dma_rx;
}

int32_t tf_card_get_dma_tx_channel(void) {
    return s_storage_dma_tx;
}

bool tf_card_init_dma(void) {
    if (s_storage_dma_rx >= 0 && s_storage_dma_tx >= 0) {
        return true;
    }
    uint32_t ch_rx = 0;
    uint32_t ch_tx = 0;
    if (bp_res_claim_unused_dma_ch(BP_RES_OWNER_STORAGE, "tf_rx", &ch_rx) != BP_STATUS_OK) {
        return false;
    }
    if (bp_res_claim_unused_dma_ch(BP_RES_OWNER_STORAGE, "tf_tx", &ch_tx) != BP_STATUS_OK) {
        bp_res_release(BP_RES_CLASS_DMA_CH, ch_rx, BP_RES_OWNER_STORAGE);
        return false;
    }
    s_storage_dma_rx = (int32_t)ch_rx;
    s_storage_dma_tx = (int32_t)ch_tx;
    return true;
}

void tf_card_deinit_dma(void) {
    if (s_storage_dma_rx >= 0) {
        bp_res_release(BP_RES_CLASS_DMA_CH, (uint32_t)s_storage_dma_rx, BP_RES_OWNER_STORAGE);
        s_storage_dma_rx = -1;
    }
    if (s_storage_dma_tx >= 0) {
        bp_res_release(BP_RES_CLASS_DMA_CH, (uint32_t)s_storage_dma_tx, BP_RES_OWNER_STORAGE);
        s_storage_dma_tx = -1;
    }
}

static inline uint32_t _millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

/*-----------------------------------------------------------------------*/
/* SPI controls (Platform dependent)                                     */
/*-----------------------------------------------------------------------*/
/*
static inline void cs_select(uint cs_pin) {
	spi_busy_wait(true);
	spinlock = true;
    
	asm volatile("nop \n nop \n nop"); // FIXME
	busy_wait_us(10);
    gpio_put(cs_pin, 0);
	busy_wait_us(10);
    asm volatile("nop \n nop \n nop"); // FIXME
}

static inline void cs_deselect(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    busy_wait_us(10);
	gpio_put(cs_pin, 1);
	busy_wait_us(10);
    asm volatile("nop \n nop \n nop"); // FIXME
	
	spi_busy_wait(false);
	spinlock = false;
}*/

static void FCLK_SLOW(void)
{
    spi_set_baudrate(BP_SPI_PORT, CLK_SLOW);
}

static void FCLK_FAST(void)
{
    spi_set_baudrate(BP_SPI_PORT, CLK_FAST);
}
/*
static void CS_HIGH(void)
{
    cs_deselect(FLASH_STORAGE_CS);
}

static void CS_LOW(void)
{
    cs_select(FLASH_STORAGE_CS);
}
*/
/* Initialize MMC interface */
static
void init_spi(void)
{
	gpio_set_function(FLASH_STORAGE_CS, GPIO_FUNC_SIO);
	gpio_set_dir(FLASH_STORAGE_CS, GPIO_OUT);
	gpio_put(FLASH_STORAGE_CS, 1);
	gpio_pull_up(FLASH_STORAGE_CS);
	gpio_pull_up(BP_SPI_CDI);
	tf_card_init_dma();
}

/* Exchange a byte */
static
BYTE xchg_spi (
	BYTE dat	/* Data to send */
)
{
	uint8_t *buff = (uint8_t *) &dat;
	
	spi_write_read_blocking(BP_SPI_PORT, buff, buff, 1);
	return (BYTE) *buff;
}


/* Receive multiple byte */
static
void rcvr_spi_multi (
	BYTE *buff,		/* Pointer to data buffer */
	UINT btr		/* Number of bytes to receive (even number) */
)
{
#if !defined(BP_HOST_TEST)
	if (s_storage_dma_rx >= 0 && s_storage_dma_tx >= 0 && btr >= 32) {
		dma_channel_config c_tx = dma_channel_get_default_config((uint)s_storage_dma_tx);
		channel_config_set_transfer_data_size(&c_tx, DMA_SIZE_8);
		channel_config_set_dreq(&c_tx, spi_get_dreq(BP_SPI_PORT, true));
		channel_config_set_read_increment(&c_tx, false);
		channel_config_set_write_increment(&c_tx, false);
		dma_channel_configure((uint)s_storage_dma_tx, &c_tx,
		                      &spi_get_hw(BP_SPI_PORT)->dr,
		                      &s_storage_dummy_tx,
		                      btr,
		                      false);

		dma_channel_config c_rx = dma_channel_get_default_config((uint)s_storage_dma_rx);
		channel_config_set_transfer_data_size(&c_rx, DMA_SIZE_8);
		channel_config_set_dreq(&c_rx, spi_get_dreq(BP_SPI_PORT, false));
		channel_config_set_read_increment(&c_rx, false);
		channel_config_set_write_increment(&c_rx, true);
		dma_channel_configure((uint)s_storage_dma_rx, &c_rx,
		                      buff,
		                      &spi_get_hw(BP_SPI_PORT)->dr,
		                      btr,
		                      false);

		dma_channel_start((uint)s_storage_dma_rx);
		dma_channel_start((uint)s_storage_dma_tx);
		dma_channel_wait_for_finish_blocking((uint)s_storage_dma_rx);
		return;
	}
#endif
	uint8_t *b = (uint8_t *) buff;
	spi_read_blocking(BP_SPI_PORT, 0xff, b, btr);
}


/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static
int wait_ready (	/* 1:Ready, 0:Timeout */
	UINT wt			/* Timeout [ms] */
)
{
	BYTE d;

	uint32_t t = _millis();
	do {
		d = xchg_spi(0xFF);
		/* This loop takes a time. Insert rot_rdq() here for multitask envilonment. */
	} while (d != 0xFF && (uint32_t)(_millis() - t) < wt);	/* Wait for card goes ready or timeout */

	return (d == 0xFF) ? 1 : 0;
}



/*-----------------------------------------------------------------------*/
/* Deselect card and release SPI                                         */
/*-----------------------------------------------------------------------*/

static
void deselect (void)
{
	//CS_HIGH();		/* Set CS# high */
	__asm__ volatile("nop \n nop \n nop"); // FIXME
    //busy_wait_us(10);
	gpio_put(FLASH_STORAGE_CS, 1);
	//busy_wait_us(10);
    __asm__ volatile("nop \n nop \n nop"); // FIXME

	xchg_spi(0xFF);	/* Dummy clock (force DO hi-z for multiple slave SPI) */
	//spi_busy_wait(false);
}



/*-----------------------------------------------------------------------*/
/* Select card and wait for ready                                        */
/*-----------------------------------------------------------------------*/

static
int _select (void)	/* 1:OK, 0:Timeout */
{
	//CS_LOW();		/* Set CS# low */
	//spi_busy_wait(true);
    
	__asm__ volatile("nop \n nop \n nop"); // FIXME
	//busy_wait_us(10);
    gpio_put(FLASH_STORAGE_CS, 0);
	//busy_wait_us(10);
    __asm__ volatile("nop \n nop \n nop"); // FIXME

	xchg_spi(0xFF);	/* Dummy clock (force DO enabled) */
	if (wait_ready(500)) return 1;	/* Wait for card ready */

	deselect();
	return 0;	/* Timeout */
}



/*-----------------------------------------------------------------------*/
/* Receive a data packet from the MMC                                    */
/*-----------------------------------------------------------------------*/

static
int rcvr_datablock (	/* 1:OK, 0:Error */
	BYTE *buff,			/* Data buffer */
	UINT btr			/* Data block length (byte) */
)
{
	BYTE token;

	const uint32_t timeout = 200;
	uint32_t t = _millis();
	do {							/* Wait for DataStart token in timeout of 200ms */
		token = xchg_spi(0xFF);
		/* This loop will take a time. Insert rot_rdq() here for multitask envilonment. */
	} while (token == 0xFF && (uint32_t)(_millis() - t) < timeout);
	if(token != 0xFE) return 0;		/* Function fails if invalid DataStart token or timeout */

	rcvr_spi_multi(buff, btr);		/* Store trailing data to the buffer */
	xchg_spi(0xFF); xchg_spi(0xFF);			/* Discard CRC */

	return 1;						/* Function succeeded */
}


/*-----------------------------------------------------------------------*/
/* Send a command packet to the MMC                                      */
/*-----------------------------------------------------------------------*/

static
BYTE send_cmd (		/* Return value: R1 resp (bit7==1:Failed to send) */
	BYTE cmd,		/* Command index */
	DWORD arg		/* Argument */
)
{
	BYTE n, res;


	if (cmd & 0x80) {	/* Send a CMD55 prior to ACMD<n> */
		cmd &= 0x7F;
		res = send_cmd(CMD55, 0);
		if (res > 1) return res;
	}

	/* Select the card and wait for ready except to stop multiple block read or CMD0 */
	if (cmd != CMD12 && cmd != CMD0) {
		deselect();
		if (!_select()) return 0xFF;
	} else if (cmd == CMD0) {
		/* For CMD0, ensure CS is low but DO NOT wait for card ready (which can block on uninitialized/warm card) */
		deselect();
		gpio_put(FLASH_STORAGE_CS, 0);
		xchg_spi(0xFF);
	}
	//if(!spinlock) printf("disk_read: spinlock not set\n");
	/* Send command packet */
	xchg_spi(0x40 | cmd);				/* Start + command index */
	xchg_spi((BYTE)(arg >> 24));		/* Argument[31..24] */
	xchg_spi((BYTE)(arg >> 16));		/* Argument[23..16] */
	xchg_spi((BYTE)(arg >> 8));			/* Argument[15..8] */
	xchg_spi((BYTE)arg);				/* Argument[7..0] */
	n = 0x01;							/* Dummy CRC + Stop */
	if (cmd == CMD0) n = 0x95;			/* Valid CRC for CMD0(0) */
	if (cmd == CMD8) n = 0x87;			/* Valid CRC for CMD8(0x1AA) */
	xchg_spi(n);

	/* Receive command resp */
	if (cmd == CMD12) xchg_spi(0xFF);	/* Discard following one byte when CMD12 */
	n = (cmd == CMD0) ? 32 : 16;		/* Wait for response (up to 32 bytes for CMD0, 16 bytes for others) */
	do {
		res = xchg_spi(0xFF);
	} while ((res & 0x80) && --n);

	return res;							/* Return received response */
}

/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Initialize disk drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS diskio_initialize (
	BYTE drv		/* Physical drive number (0) */
)
{
	BYTE n, cmd, ty, ocr[4];
	const uint32_t timeout = 2000; /* Initialization timeout = 2 sec (slow SDSC cards) */
	uint32_t t;

	spi_busy_wait(true);

	if (drv) return STA_NOINIT;			/* Supports only drive 0 */
	init_spi();							/* Initialize SPI */
    //sleep_ms(10);

	if (Stat & STA_NODISK) return Stat;	/* Is card existing in the soket? */

	FCLK_SLOW();
	
	/* Step 1: Send >= 200 dummy clock cycles (25 bytes) with CS HIGH.
	 * SD spec requires >= 74 clocks with CS high and MOSI high for the card's
	 * power-on/reset synchronization sequence. */
	gpio_put(FLASH_STORAGE_CS, 1);
	for (n = 25; n; n--) xchg_spi(0xFF);

	/* Step 2: Put the card into SPI mode / Idle state with CMD0.
	 * In accordance with SD spec Section 6.4.1, CMD0 must be the first command
	 * with CS asserted to enter SPI mode from native SD mode.
	 * If CMD0 does not respond after initial attempts (e.g. warm reset during multi-block read),
	 * send CMD12 (STOP_TRANSMISSION) to abort in-flight streaming and retry CMD0. */
	ty = 0;
	uint32_t cmd0_start = _millis();
	BYTE r1 = 0xFF;
	for (int retry = 0; retry < 50; retry++) {
		deselect();
		for (n = 2; n; n--) xchg_spi(0xFF);

		r1 = send_cmd(CMD0, 0);
		if (r1 == 1) {
			break;
		}
		if (retry == 5) {
			/* Warm reset recovery: if CMD0 hasn't responded yet, card might be
			 * in middle of multi-block read. Send CMD12 stop transmission. */
			deselect();
			gpio_put(FLASH_STORAGE_CS, 0);
			xchg_spi(0xFF);
			send_cmd(CMD12, 0);
			deselect();
			for (n = 10; n; n--) xchg_spi(0xFF);
		}
		busy_wait_ms(10);
		if (_millis() - cmd0_start >= 1000) {
			break;
		}
	}

	if (r1 == 1) {			/* Card responded with R1=0x01 (Idle state) */
		t = _millis();
		if (send_cmd(CMD8, 0x1AA) == 1) {	/* SDv2? */
			for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);	/* Get 32 bit return value of R7 resp */
			if (ocr[2] == 0x01 && ocr[3] == 0xAA) {				/* Is the card supports vcc of 2.7-3.6V? */
				while ((uint32_t)(_millis() - t) < timeout && send_cmd(ACMD41, 1UL << 30)) {
					busy_wait_ms(10);
				}
				if ((uint32_t)(_millis() - t) < timeout && send_cmd(CMD58, 0) == 0) {		/* Check CCS bit in the OCR */
					for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
					ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;	/* Card is SDv2 */
				}
			}
		} else {	/* Not SDv2 card */
			if (send_cmd(ACMD41, 0) <= 1) 	{	/* SDv1 or MMC? */
				ty = CT_SD1; cmd = ACMD41;	/* SDv1 (ACMD41(0)) */
			} else {
				ty = CT_MMC; cmd = CMD1;	/* MMCv3 (CMD1(0)) */
			}
			while ((uint32_t)(_millis() - t) < timeout && send_cmd(cmd, 0)) {
				busy_wait_ms(10);
			}
			if ((uint32_t)(_millis() - t) >= timeout || send_cmd(CMD16, 512) != 0)	/* Set block length: 512 */
				ty = 0;
		}
	}
	CardType = ty;	/* Card type */
	deselect();
	for (n = 5; n; n--) xchg_spi(0xFF);

	if (ty) {			/* OK */
		FCLK_FAST();			/* Set fast clock */
		Stat &= ~STA_NOINIT;	/* Clear STA_NOINIT flag */

		/* Obtain capacity from validated card metadata at mount/initialization */
		uint8_t csd[16];
		if (send_cmd(CMD9, 0) == 0 && rcvr_datablock(csd, 16)) {
			if (tf_card_parse_csd(csd, &s_tf_card_meta)) {
				s_media_generation++;
				s_tf_card_meta.media_generation = s_media_generation;
				if (s_tf_card_meta.write_protected) {
					Stat |= STA_PROTECT;
				} else {
					Stat &= ~STA_PROTECT;
				}
				system_config.storage_size = (float)(s_tf_card_meta.sector_count * 512ULL) / (1024.0f * 1024.0f * 1024.0f);
			} else {
				tf_card_invalidate_metadata();
				Stat = STA_NOINIT;
				ty = 0;
			}
		} else {
			tf_card_invalidate_metadata();
			Stat = STA_NOINIT;
			ty = 0;
		}
		deselect();
		for (n = 5; n; n--) xchg_spi(0xFF);
	} else {			/* Failed */
		tf_card_invalidate_metadata();
		Stat = STA_NOINIT;
	}

	spi_busy_wait(false);

	return Stat;
}



/*-----------------------------------------------------------------------*/
/* Get disk status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS diskio_status (
	BYTE drv		/* Physical drive number (0) */
)
{
	if (drv) return STA_NOINIT;		/* Supports only drive 0 */

	return Stat;	/* Return disk status */
}



/*-----------------------------------------------------------------------*/
/* Read sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT diskio_read (
	BYTE drv,		/* Physical drive number (0) */
	BYTE *buff,		/* Pointer to the data buffer to store read data */
	LBA_t sector,	/* Start sector number (LBA) */
	UINT count		/* Number of sectors to read (1..128) */
)
{


	if (drv || !buff || !count || count > 128) return RES_PARERR;
	if (s_tf_card_meta.valid) {
		if ((uint64_t)count > s_tf_card_meta.sector_count ||
		    (uint64_t)sector > s_tf_card_meta.sector_count - (uint64_t)count) {
			return RES_PARERR;
		}
	} else if (sector > UINT32_MAX - (count - 1)) {
		return RES_PARERR;
	}
	if (!(CardType & CT_BLOCK)) {
		if (count > (UINT32_MAX / 512) || sector > (UINT32_MAX / 512) - count) {
			return RES_PARERR;
		}
	}
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check if drive is ready */

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* LBA ot BA conversion (byte addressing cards) */

	spi_busy_wait(true);

	if (count == 1) {	/* Single sector read */
		if ((send_cmd(CMD17, sector) == 0)	/* READ_SINGLE_BLOCK */
			&& rcvr_datablock(buff, 512)) {
			count = 0;
		}
	}
	else {				/* Multiple sector read */
		if (send_cmd(CMD18, sector) == 0) {	/* READ_MULTIPLE_BLOCK */
			do {
				if (!rcvr_datablock(buff, 512)) break;
				buff += 512;
			} while (--count);
			send_cmd(CMD12, 0);				/* STOP_TRANSMISSION */
		}
	}
	deselect();
	spi_busy_wait(false);
	return count ? RES_ERROR : RES_OK;	/* Return result */
}

 

#if !FF_FS_READONLY && !FF_FS_NORTC
/* get the current time */
DWORD get_fattime (void)
{
	return 0;
}
#endif

#if FF_FS_READONLY == 0
/* Transmit multiple byte */
static
void xmit_spi_multi (
	const BYTE *buff,		/* Pointer to data buffer */
	UINT btx		/* Number of bytes to transmit (even number) */
)
{
#if !defined(BP_HOST_TEST)
	if (s_storage_dma_rx >= 0 && s_storage_dma_tx >= 0 && btx >= 32) {
		dma_channel_config c_rx = dma_channel_get_default_config((uint)s_storage_dma_rx);
		channel_config_set_transfer_data_size(&c_rx, DMA_SIZE_8);
		channel_config_set_dreq(&c_rx, spi_get_dreq(BP_SPI_PORT, false));
		channel_config_set_read_increment(&c_rx, false);
		channel_config_set_write_increment(&c_rx, false);
		dma_channel_configure((uint)s_storage_dma_rx, &c_rx,
		                      &s_storage_dummy_rx,
		                      &spi_get_hw(BP_SPI_PORT)->dr,
		                      btx,
		                      false);

		dma_channel_config c_tx = dma_channel_get_default_config((uint)s_storage_dma_tx);
		channel_config_set_transfer_data_size(&c_tx, DMA_SIZE_8);
		channel_config_set_dreq(&c_tx, spi_get_dreq(BP_SPI_PORT, true));
		channel_config_set_read_increment(&c_tx, true);
		channel_config_set_write_increment(&c_tx, false);
		dma_channel_configure((uint)s_storage_dma_tx, &c_tx,
		                      &spi_get_hw(BP_SPI_PORT)->dr,
		                      buff,
		                      btx,
		                      false);

		dma_channel_start((uint)s_storage_dma_rx);
		dma_channel_start((uint)s_storage_dma_tx);
		dma_channel_wait_for_finish_blocking((uint)s_storage_dma_tx);
		dma_channel_wait_for_finish_blocking((uint)s_storage_dma_rx);
		return;
	}
#endif
	const uint8_t *b = (const uint8_t *) buff;
	spi_write_blocking(BP_SPI_PORT, b, btx);
}

/*-----------------------------------------------------------------------*/
/* Transmit a data packet to the MMC                                     */
/*-----------------------------------------------------------------------*/

static
int xmit_datablock (	/* 1:OK, 0:Error */
	const BYTE *buff, /* 512 byte data block to be transmitted */
	BYTE token /* Data/Stop token */
)
{
	BYTE resp;
	if (!wait_ready(500)) return 0;
	xchg_spi(token); /* Xmit data token */
	if (token != 0xFD) { /* Is data token */
		xmit_spi_multi(buff, 512); /* Xmit the data block to the MMC */
		xchg_spi(0xFF); /* CRC (Dummy) */
		xchg_spi(0xFF);
		resp = xchg_spi(0xFF); /* Reveive data response */
		if ((resp & 0x1F) != 0x05) /* If not accepted, return with error */
			return 0;
	}
	return 1;
}

/*-----------------------------------------------------------------------*/
/* Write sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT diskio_write (
	BYTE drv,			/* Physical drive number (0) */
	const BYTE *buff,	/* Ponter to the data to write */
	LBA_t sector,		/* Start sector number (LBA) */
	UINT count			/* Number of sectors to write (1..128) */
)
{
	if (drv || !buff || !count || count > 128) return RES_PARERR;
	if (s_tf_card_meta.valid) {
		if ((uint64_t)count > s_tf_card_meta.sector_count ||
		    (uint64_t)sector > s_tf_card_meta.sector_count - (uint64_t)count) {
			return RES_PARERR;
		}
	} else if (sector > UINT32_MAX - (count - 1)) {
		return RES_PARERR;
	}
	if (!(CardType & CT_BLOCK)) {
		if (count > (UINT32_MAX / 512) || sector > (UINT32_MAX / 512) - count) {
			return RES_PARERR;
		}
	}
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check drive status */
	if (Stat & STA_PROTECT) return RES_WRPRT;	/* Check write protect */

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* LBA ==> BA conversion (byte addressing cards) */

	spi_busy_wait(true);

	/* Modified by Codex: unwind SPI ownership when card selection fails. */
	if (!_select()) {
		deselect();
		spi_busy_wait(false);
		return RES_NOTRDY;
	}

	if (count == 1) {	/* Single sector write */
		if ((send_cmd(CMD24, sector) == 0)	/* WRITE_BLOCK */
			&& xmit_datablock(buff, 0xFE)) {
			count = 0;
		}
	}
	else {				/* Multiple sector write */
		if (CardType & CT_SDC) send_cmd(ACMD23, count);	/* Predefine number of sectors */
		if (send_cmd(CMD25, sector) == 0) {	/* WRITE_MULTIPLE_BLOCK */
			do {
				if (!xmit_datablock(buff, 0xFC)) break;
				buff += 512;
			} while (--count);
			if (!xmit_datablock(0, 0xFD)) count = 1;	/* STOP_TRAN token */
		}
	}
	deselect();

	spi_busy_wait(false);

	return count ? RES_ERROR : RES_OK;	/* Return result */
}
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous drive controls other than data read/write               */
/*-----------------------------------------------------------------------*/

DRESULT diskio_ioctl (
	BYTE drv,		/* Physical drive number (0) */
	BYTE cmd,		/* Control command code */
	void *buff		/* Pointer to the conrtol data */
)
{
	DRESULT res;
	BYTE n, csd[16];
	DWORD *dp, st, ed, csize;


	if (drv) return RES_PARERR;					/* Check parameter */
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check if drive is ready */

	res = RES_ERROR;

	spi_busy_wait(true);

	switch (cmd) {
	case CTRL_SYNC :		/* Wait for end of internal write process of the drive */
		if (_select()) res = RES_OK;
		break;
	
	case GET_SECTOR_SIZE:
		;
		WORD *sector_size_out = (WORD *)buff;
		*sector_size_out = BP_FLASH_DISK_BLOCK_SIZE;
		res = RES_OK;
		break;
	
	case GET_SECTOR_COUNT :	/* Get drive capacity in unit of sector (DWORD) */
		if (s_tf_card_meta.valid) {
			*(DWORD*)buff = (s_tf_card_meta.sector_count > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (DWORD)s_tf_card_meta.sector_count;
			res = RES_OK;
		} else if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
			if (tf_card_parse_csd(csd, &s_tf_card_meta)) {
				s_media_generation++;
				s_tf_card_meta.media_generation = s_media_generation;
				*(DWORD*)buff = (s_tf_card_meta.sector_count > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (DWORD)s_tf_card_meta.sector_count;
				res = RES_OK;
			}
		}
		break;

	case GET_BLOCK_SIZE :	/* Get erase block size in unit of sector (DWORD) */
		if (CardType & CT_SD2) {	/* SDC ver 2.00 */
			if (send_cmd(ACMD13, 0) == 0) {	/* Read SD status */
				xchg_spi(0xFF);
				if (rcvr_datablock(csd, 16)) {				/* Read partial block */
					for (n = 64 - 16; n; n--) xchg_spi(0xFF);	/* Purge trailing data */
					*(DWORD*)buff = 16UL << (csd[10] >> 4);
					res = RES_OK;
				}
			}
		} else {					/* SDC ver 1.XX or MMC */
			if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {	/* Read CSD */
				if (CardType & CT_SD1) {	/* SDC ver 1.XX */
					*(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
				} else {					/* MMC */
					*(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
				}
				res = RES_OK;
			}
		}
		break;

	case CTRL_TRIM :	/* Erase a block of sectors (used when _USE_ERASE == 1) */
		if (!(CardType & CT_SDC)) break;				/* Check if the card is SDC */
		if (disk_ioctl(drv, MMC_GET_CSD, csd)) break;	/* Get CSD */
		if (!(csd[0] >> 6) && !(csd[10] & 0x40)) break;	/* Check if sector erase can be applied to the card */
		dp = buff; st = dp[0]; ed = dp[1];				/* Load sector block */
		if (!(CardType & CT_BLOCK)) {
			st *= 512; ed *= 512;
		}
		if (send_cmd(CMD32, st) == 0 && send_cmd(CMD33, ed) == 0 && send_cmd(CMD38, 0) == 0 && wait_ready(30000)) {	/* Erase sector block */
			res = RES_OK;	/* FatFs does not check result of this command */
		}
		break;

	default:
		res = RES_PARERR;
	}

	deselect();

	spi_busy_wait(false);

	return res;
}