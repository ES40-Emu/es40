/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) 2025 by Kisara Development LLC.
 * All rights reserved.
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Kisara Development LLC contribution notice
 * Copyright (C) 2025 Kisara Development LLC.
 *
 * This contribution is being open sourced from our commercial Intel Itanium 2
 * emulation platform. Kisara Development LLC has removed the former
 * proprietary license notice and all associated proprietary restrictions. It
 * is now distributed under the BSD-1-Clause license above.
 *
 * No warranty, maintenance, support, update, or other service obligation is
 * provided. The code is supplied "AS IS" and remains subject to the warranty
 * disclaimer and limitation of liability stated above.
 */

/**
 * \file
 * Contains the code for the emulated LSI Logic 53C1020 controller.
 */
#include "StdAfx.h"
#include "LSI53C1020.h"
#include "system/System.h"
#include "Disk.h"
#include "SCSIBus.h"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#elif !defined(__VMS)
#include <unistd.h>
#endif

#define LSI_PCI_VENDOR_ID           0x1000U
#define LSI_PCI_DEVICE_ID           0x0030U
#define LSI_PCI_CLASS_CODE          0x010000U
#define LSI_PCI_REVISION_ID         0x08U
#define LSI_PCI_SUBSYSTEM_VENDOR_ID 0x103CU
#define LSI_PCI_SUBSYSTEM_DEVICE_ID 0x1290U

#define LSI_PCI_VENDOR_DEVICE  ((LSI_PCI_DEVICE_ID << 16) | LSI_PCI_VENDOR_ID)
#define LSI_PCI_CLASS_REVISION ((LSI_PCI_CLASS_CODE << 8) | LSI_PCI_REVISION_ID)
#define LSI_PCI_SUBSYSTEM                                                                  \
	((LSI_PCI_SUBSYSTEM_DEVICE_ID << 16) | LSI_PCI_SUBSYSTEM_VENDOR_ID)

#define R_DOORBELL        0x00U
#define R_WRITE_SEQUENCE  0x04U
#define R_HOST_DIAG       0x08U
#define R_TEST_BASE       0x0CU
#define R_DIAG_RW_DATA    0x10U
#define R_DIAG_RW_ADDR    0x14U
#define R_HOST_INT_STATUS 0x30U
#define R_HOST_INT_MASK   0x34U
#define R_REQUEST_QUEUE   0x40U
#define R_REPLY_QUEUE     0x44U

#define LSI_IOC_STATE_READY       0x1U
#define LSI_IOC_STATE_OPERATIONAL 0x2U
#define LSI_IOC_STATE_FAULT       0x4U
#define LSI_DOORBELL_USED         0x08000000U

#define LSI_DOORBELL_MESSAGE_UNIT_RESET 0x40U
#define LSI_DOORBELL_IO_UNIT_RESET      0x41U
#define LSI_DOORBELL_HANDSHAKE          0x42U
#define LSI_DOORBELL_REPLY_FRAME_REMOVAL 0x43U

#define LSI_HIS_DOORBELL_INTERRUPT 0x00000001U
#define LSI_HIS_REPLY_INTERRUPT    0x00000008U
#define LSI_HIM_WRITABLE_MASK      0x00000309U
#define LSI_HIM_RESET_VALUE        0x00000009U

#define LSI_HOSTDIAG_DIAG_WRITE_ENABLE 0x80U
#define LSI_HOSTDIAG_RESET_HISTORY     0x20U
#define LSI_HOSTDIAG_DIAG_RW_ENABLE    0x10U
#define LSI_HOSTDIAG_RESET_ADAPTER     0x04U
#define LSI_HOSTDIAG_MEMORY_ENABLE     0x01U
#define LSI_HOSTDIAG_STORED_MASK       0x7BU

#define LSI_FUNCTION_SCSI_IO_REQUEST    0x00U
#define LSI_FUNCTION_SCSI_TASK_MGMT     0x01U
#define LSI_FUNCTION_IOC_INIT           0x02U
#define LSI_FUNCTION_IOC_FACTS          0x03U
#define LSI_FUNCTION_CONFIG             0x04U
#define LSI_FUNCTION_PORT_FACTS         0x05U
#define LSI_FUNCTION_PORT_ENABLE        0x06U
#define LSI_FUNCTION_EVENT_NOTIFICATION 0x07U
#define LSI_FUNCTION_EVENT_ACK          0x08U
#define LSI_FUNCTION_FW_DOWNLOAD        0x09U
#define LSI_FUNCTION_FW_UPLOAD          0x12U

#define LSI_EVENT_IOC_BUS_RESET         0x00000004U
#define LSI_EVENT_EVENT_CHANGE          0x0000000AU
#define LSI_MSGFLAGS_CONTINUATION_REPLY 0x80U

#define LSI_IOCSTATUS_SUCCESS               0x0000U
#define LSI_IOCSTATUS_INVALID_FUNCTION      0x0001U
#define LSI_IOCSTATUS_INTERNAL_ERROR        0x0004U
#define LSI_IOCSTATUS_INVALID_FIELD         0x0007U
#define LSI_IOCSTATUS_INVALID_STATE         0x0008U
#define LSI_IOCSTATUS_CONFIG_INVALID_ACTION 0x0020U
#define LSI_IOCSTATUS_CONFIG_INVALID_PAGE   0x0022U
#define LSI_IOCSTATUS_SCSI_DEVICE_NOT_THERE 0x0043U
#define LSI_IOCSTATUS_SCSI_DATA_OVERRUN     0x0044U
#define LSI_IOCSTATUS_SCSI_DATA_UNDERRUN    0x0045U

#define LSI_FAULT_REPLY_FREE_EMPTY 0xE001U
#define LSI_FAULT_NO_DMA           0xE002U
#define LSI_FAULT_XFER_TOO_LARGE   0xE003U
#define LSI_FAULT_REPLY_POST_FULL  0xE004U

#define LSI_MPI_VERSION          0x0102U
#define LSI_MPI_HEADER_VERSION   0x0000U
#define LSI_REPLY_QUEUE_DEPTH    128U
#define LSI_GLOBAL_CREDITS       128U
#define LSI_REQUEST_FRAME_DWORDS 32U
#define LSI_MAX_CHAIN_DEPTH      128U
#define LSI_MAX_BUSES            1U
#define LSI_NUMBER_OF_PORTS      1U
#define LSI_PORT_SCSI_ID         7U
#define LSI_FW_VERSION           0x01000000U
#define LSI_REPLY_FRAME_BYTES    128U
#define LSI_IOC_BLOCK_DWORDS     16U

#define LSI_PORTFACTS_PORTTYPE_SCSI      0x01U
#define LSI_PORTFACTS_PROTOCOL_INITIATOR 0x0008U

#define LSI_CFG_ACTION_PAGE_HEADER   0x00U
#define LSI_CFG_ACTION_READ_CURRENT  0x01U
#define LSI_CFG_ACTION_WRITE_CURRENT 0x02U
#define LSI_CFG_ACTION_PAGE_DEFAULT  0x03U
#define LSI_CFG_ACTION_WRITE_NVRAM   0x04U
#define LSI_CFG_ACTION_READ_DEFAULT  0x05U
#define LSI_CFG_ACTION_READ_NVRAM    0x06U

#define LSI_CFG_PAGETYPE_IO_UNIT       0x00U
#define LSI_CFG_PAGETYPE_IOC           0x01U
#define LSI_CFG_PAGETYPE_BIOS          0x02U
#define LSI_CFG_PAGETYPE_SCSI_PORT     0x03U
#define LSI_CFG_PAGETYPE_SCSI_DEVICE   0x04U
#define LSI_CFG_PAGETYPE_MANUFACTURING 0x09U
#define LSI_CFG_PAGEATTR_READ_ONLY     0x00U
#define LSI_CFG_PAGEATTR_CHANGEABLE    0x10U
#define LSI_CFG_PAGEATTR_PERSISTENT    0x20U

#define LSI_U320_PORT_CAPABILITIES 0xA03F0807U
#define LSI_U320_DEV1_PARAMS       0x203F0807U
#define LSI_PHYS_INTERFACE_LVD     0x00000003U
#define LSI_PORT1_CONFIGURATION    ((1UL << (16U + LSI_PORT_SCSI_ID)) | LSI_PORT_SCSI_ID)

#define LSI_SGE_LENGTH_MASK   0x00FFFFFFU
#define LSI_SGE_FLAG_64BIT    (0x02U << 24)
#define LSI_SGEF_LAST_ELEMENT 0x80U
#define LSI_SGEF_TYPE_MASK    0x30U
#define LSI_SGEF_TYPE_SIMPLE  0x10U
#define LSI_SGEF_TYPE_CHAIN   0x30U
#define LSI_SGEF_ADDR64       0x02U
#define LSI_SGEF_END_OF_LIST  0x01U

#define LSI_SCSIIO_CONTROL_DIR_MASK     0x03000000U
#define LSI_SCSIIO_CONTROL_DIR_WRITE    0x01000000U
#define LSI_SCSIIO_CONTROL_DIR_READ     0x02000000U
#define LSI_SCSI_STATE_AUTOSENSE_VALID  0x01U
#define LSI_SCSI_STATE_NO_SCSI_STATUS   0x04U
#define LSI_SCSI_STATUS_GOOD            0x00U
#define LSI_SCSI_STATUS_CHECK_CONDITION 0x02U

#define LSI_MAX_TRANSFER_BYTES    (16U * 1024U * 1024U)
#define LSI_REPLY_MFA_ADDRESS_BIT 0x80000000U
#define LSI_REPLY_REMOVAL_BYTES   (sizeof(u32) * (LSI_REPLY_FIFO_DEPTH + 1U))
#define LSI_FRAME_MAX             128U
/* Manufacturing page 1 is a four-byte header followed by 256 bytes of VPD. */
#define LSI_CFG_PAGE_MAX          260U
#define LSI_SGL_MAX_ENTRIES       256U
#define LSI_SGL_SEG_MAX           512U
#define LSI_SGL_MAX_ELEMENTS      1024U

#define LSI_FLASH_SIZE      (512U * 1024U)
#define LSI_FLASH_SECTOR_SIZE (64U * 1024U)
#define LSI_FLASH_MAX_SIZE  (1024U * 1024U)
#define LSI_FW_PRODUCT_1020_C0         0x0208U
#define LSI_FW_PRODUCT_1030_B0_COMPAT  0x0202U

#define LSI_FLASH_ADDRESS_BASE       0x3E000000U
#define LSI_FLASH_CSR_ADDRESS        0x3F000000U
#define LSI_FLASH_CSR_WRITE_ENABLE   0x00000800U
#define LSI_FLASH_MANUFACTURER_ID    0x01U
#define LSI_FLASH_DEVICE_ID          0x4FU
#define LSI_FLASH_UNLOCK_ADDRESS1    0x555U
#define LSI_FLASH_UNLOCK_ADDRESS2    0x2AAU
#define LSI_FLASH_DOWNLOAD_LAST      0x01U
#define LSI_FLASH_SAVE_DELAY         2.0

#define LSI_FLASH_MODE_READ           0U
#define LSI_FLASH_MODE_UNLOCK1        1U
#define LSI_FLASH_MODE_UNLOCK2        2U
#define LSI_FLASH_MODE_PROGRAM        3U
#define LSI_FLASH_MODE_ERASE_UNLOCK1  4U
#define LSI_FLASH_MODE_ERASE_UNLOCK2  5U
#define LSI_FLASH_MODE_ERASE_SELECT   6U
#define LSI_FLASH_MODE_AUTOSELECT     7U
#define LSI_FLASH_MODE_CFI            8U
#define LSI_FLASH_MODE_BYPASS         9U
#define LSI_FLASH_MODE_BYPASS_PROGRAM 10U
#define LSI_FLASH_MODE_BYPASS_EXIT    11U

#define LSI_FW_DOWNLOAD_TYPE_FW   0x01U
#define LSI_FW_DOWNLOAD_TYPE_BIOS 0x02U
#define LSI_FW_UPLOAD_TYPE_IOC    0x00U
#define LSI_FW_UPLOAD_TYPE_FW     0x01U
#define LSI_FW_UPLOAD_TYPE_BIOS   0x02U
#define LSI_FW_UPLOAD_TYPE_ALL    0x0AU

#define MPI_REPLY_MSG_LENGTH 0x02U
#define MPI_MSG_FUNCTION     0x03U
#define MPI_MSG_CONTEXT      0x08U
#define MPI_REPLY_IOC_STATUS 0x0EU

#define MPI_SCSI_TARGET_ID       0x00U
#define MPI_SCSI_BUS             0x01U
#define MPI_SCSI_CHAIN_OFFSET    0x02U
#define MPI_SCSI_CDB_LENGTH      0x04U
#define MPI_SCSI_SENSE_LENGTH    0x05U
#define MPI_SCSI_LUN_BYTE        0x0DU
#define MPI_SCSI_CONTROL         0x14U
#define MPI_SCSI_CDB             0x18U
#define MPI_SCSI_DATA_LENGTH     0x28U
#define MPI_SCSI_SENSE_ADDRESS   0x2CU
#define MPI_SCSI_SGL             0x30U
#define MPI_SCSI_REPLY_STATUS    0x0CU
#define MPI_SCSI_REPLY_STATE     0x0DU
#define MPI_SCSI_REPLY_TRANSFER_COUNT 0x14U
#define MPI_SCSI_REPLY_SENSE_COUNT    0x18U

static const u8 lsi_write_sequence_keys[5] =
{
	0x04U, 0x0BU, 0x02U, 0x07U, 0x0DU
};

/**
 * PCI Configuration Data Block
 */
static u32 olsi_cfg_data[64] =
{
	/*00*/ LSI_PCI_VENDOR_DEVICE,
	/*04*/ 0x02200000U,              // Medium DEVSEL, 66 MHz capable
	/*08*/ LSI_PCI_CLASS_REVISION,
	/*0c*/ 0x00000000U,
	/*10*/ 0x00000001U,              // BAR0: 256-byte I/O window
	/*14*/ 0x00000004U,              // BAR1/2: 64-bit memory window
	/*18*/ 0x00000000U,
	/*1c*/ 0x00000004U,              // BAR3/4: 64-bit diagnostic memory window
	/*20*/ 0x00000000U,
	/*24*/ 0x00000000U,
	/*28*/ 0x00000000U,
	/*2c*/ LSI_PCI_SUBSYSTEM,
	/*30*/ 0x00000000U,
	/*34*/ 0x00000000U,
	/*38*/ 0x00000000U,
	/*3c*/ 0x06100100U,              // INTA#, Min_Gnt 0x10, Max_Lat 0x06
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/**
 * PCI Configuration Mask Block
 */
static u32 olsi_cfg_mask[64] =
{
	/*00*/ 0x00000000U,
	/*04*/ 0x00000157U,
	/*08*/ 0x00000000U,
	/*0c*/ 0x0000F0F8U,
	/*10*/ 0xFFFFFF00U,
	/*14*/ 0xFFFFFC00U,
	/*18*/ 0xFFFFFFFFU,
	/*1c*/ 0xFFFF0000U,
	/*20*/ 0xFFFFFFFFU,
	/*24*/ 0x00000000U,
	/*28*/ 0x00000000U,
	/*2c*/ 0x00000000U,
	/*30*/ 0x00000000U,
	/*34*/ 0x00000000U,
	/*38*/ 0x00000000U,
	/*3c*/ 0x000000FFU,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Validate an x86 PCI option ROM and return its size. */
static bool valid_option_rom(const u8* image, size_t available, u32* image_bytes)
{
	if (available < 0x34U || image[0] != 0x55U || image[1] != 0xAAU || image[2] == 0)
		return false;

	const u32 bytes = (u32)image[2] * 512U;
	const u32 pcir = get_le16(&image[0x18]);
	if (bytes > available || pcir > bytes - 0x18U ||
	    memcmp(&image[pcir], "PCIR", 4U) != 0 ||
	    get_le16(&image[pcir + 4U]) != LSI_PCI_VENDOR_ID ||
	    get_le16(&image[pcir + 6U]) != LSI_PCI_DEVICE_ID ||
	    get_le16(&image[pcir + 0x0AU]) < 0x18U ||
	    (get_le32(&image[pcir + 0x0CU]) >> 8) != LSI_PCI_CLASS_CODE ||
	    (u32)get_le16(&image[pcir + 0x10U]) * 512U != bytes ||
	    image[pcir + 0x14U] != 0)
		return false;

	u8 checksum = 0;
	for (u32 i = 0; i < bytes; i++)
		checksum = (u8)(checksum + image[i]);
	if (checksum != 0)
		return false;

	if (image_bytes)
		*image_bytes = bytes;
	return true;
}

/*
 * The supplied Fujitsu image is tagged for the 1030 B0 firmware family.  The
 * tag is accepted for flash seeding only; the emulated PCI device remains a
 * 53C1020 (device 0030).
 */
static bool valid_ioc_firmware(const u8* image, size_t available, u32* image_bytes)
{
	const u16 product_id = available >= 0x24U ? get_le16(&image[0x22]) : 0U;
	if (available < 0x34U || get_le32(&image[0x04]) != 0x5AEAA55AU ||
	    get_le32(&image[0x08]) != 0xA55AEAA5U ||
	    get_le32(&image[0x0C]) != 0x5AA55AEAU ||
	    get_le16(&image[0x20]) != LSI_PCI_VENDOR_ID ||
	    (product_id != LSI_FW_PRODUCT_1020_C0 &&
	     product_id != LSI_FW_PRODUCT_1030_B0_COMPAT))
		return false;

	const u32 bytes = get_le32(&image[0x2C]);
	if (bytes < 0x34U || bytes > available || (bytes & 3U) != 0)
		return false;

	u32 checksum = 0;
	for (u32 i = 0; i < bytes; i += 4U)
		checksum += get_le32(&image[i]);
	if (checksum != 0)
		return false;

	if (image_bytes)
		*image_bytes = bytes;
	return true;
}

CLSI53C1020::CLSI53C1020(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev)
	: CPCIDevice(cfg, c, pcibus, pcidev),
	CDiskController(1, LSI_MAX_TARGETS),
	myRegLock(nullptr),
	trace_enabled(false),
	trace_last_his(0),
	trace_last_doorbell(0),
	trace_his_valid(false),
	trace_doorbell_valid(false),
	option_rom_offset(0),
	option_rom_size(0),
	ioc_firmware_size(0),
	firmware_product_id(LSI_FW_PRODUCT_1020_C0),
	firmware_version(LSI_FW_VERSION),
	download_image_type(0),
	download_active(false),
	flash_dirty(false),
	flash_views_dirty(false),
	flash_last_dirty(0)
{
	memset(&state, 0, sizeof(state));
	CSCSIBus* bus = new CSCSIBus(cfg, c);
	scsi_register(0, bus, LSI_PORT_SCSI_ID);
}

void CLSI53C1020::init()
{
	u32 cfg_mask[64];
	memcpy(cfg_mask, olsi_cfg_mask, sizeof(cfg_mask));
	load_flash();
	if (!flash.empty())
	{
		const u32 rom_size = (u32)flash.size();
		cfg_mask[0x30 / 4] = (~(rom_size - 1U) & PCI_ROM_ADDRESS_MASK) |
			PCI_ROM_ADDRESS_ENABLE;
	}

	add_function(0, olsi_cfg_data, cfg_mask);
	CPCIDevice::ResetPCI();

	memset(&state, 0, sizeof(state));
	const char* trace_env = getenv("ES40_LSI53C1020_TRACE");
	trace_enabled = trace_env && trace_env[0] == '1';
	myRegLock = new CMutex("lsi1020-reg");
	chip_reset();
	printf("%s: LSI53C1020 Fusion-MPT SCSI controller.\n", devid_string);
}

/**
 * Restore the card flash or initialize it from optional seed images.
 *
 * Seed images are used only when the backing file does not exist.
 */
void CLSI53C1020::load_flash()
{
	flash.clear();
	download_image.clear();
	flash_path.clear();
	option_rom_offset = 0;
	option_rom_size = 0;
	ioc_firmware_size = 0;
	firmware_product_id = LSI_FW_PRODUCT_1020_C0;
	firmware_version = LSI_FW_VERSION;
	download_image_type = 0;
	download_active = false;
	flash_dirty = false;
	flash_views_dirty = false;
	flash_last_dirty = 0;

	const char* backing = myCfg->get_text_value("flash");
	if (backing && backing[0])
	{
		flash_path = backing;
		if (load_flash_image(flash_path))
		{
			rebuild_flash_views();
			return;
		}
	}

	/* An erased flash remains available when no backing or seed image is present. */
	flash.assign(LSI_FLASH_SIZE, 0xFFU);
	if (!flash_path.empty())
		mark_flash_dirty();

	const char* firmware = myCfg->get_text_value("firmware");
	if (firmware && firmware[0])
		load_ioc_firmware(firmware);

	const char* rom = myCfg->get_text_value("rom");
	if (rom && rom[0])
		load_option_rom(rom);

	rebuild_flash_views();
	flush_flash();
}

bool CLSI53C1020::load_flash_image(const std::string& path)
{
	FILE* file = fopen(path.c_str(), "rb");
	if (!file)
	{
		if (errno == ENOENT)
		{
			printf("%%LSI-I-NEWFLASH: %s: card flash %s does not exist; "
				"it will be initialized from any available seed images.\n",
				devid_string, path.c_str());
		}
		else
		{
			printf("%%LSI-W-NOFLASH: %s: card flash %s could not be opened; "
				"continuing with volatile flash.\n", devid_string, path.c_str());
			flash_path.clear();
		}
		return false;
	}

	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		printf("%%LSI-W-BADFLASH: %s: card flash %s could not be sized; "
			"continuing with volatile flash.\n", devid_string, path.c_str());
		flash_path.clear();
		return false;
	}
	const long length = ftell(file);
	if (length != LSI_FLASH_SIZE || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		printf("%%LSI-W-BADFLASH: %s: card flash %s is %ld bytes; expected "
			"%u.  The file will not be overwritten.\n", devid_string,
			path.c_str(), length, (unsigned)LSI_FLASH_SIZE);
		flash_path.clear();
		return false;
	}

	flash.resize(LSI_FLASH_SIZE);
	const size_t count = fread(flash.data(), 1, flash.size(), file);
	const int close_result = fclose(file);
	if (count != flash.size() || close_result != 0)
	{
		flash.clear();
		printf("%%LSI-W-BADFLASH: %s: card flash %s could not be read; "
			"continuing with volatile flash.\n", devid_string, path.c_str());
		flash_path.clear();
		return false;
	}

	printf("%s: restored 512 KiB card flash from %s.\n", devid_string,
		path.c_str());
	return true;
}

bool CLSI53C1020::load_option_rom(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		printf("%%LSI-W-NOROM: %s: optional PCI BIOS %s was not found; "
			"continuing without it.\n", devid_string, path.c_str());
		return false;
	}

	file.seekg(0, std::ios::end);
	const std::streamoff length = file.tellg();
	if (length <= 0 || length > LSI_FLASH_MAX_SIZE)
	{
		printf("%%LSI-W-BADROM: %s: optional PCI BIOS %s has an invalid "
			"size; continuing without it.\n", devid_string, path.c_str());
		return false;
	}
	file.seekg(0, std::ios::beg);
	std::vector<u8> image((size_t)length);
	if (!file.read((char*)image.data(), (std::streamsize)image.size()))
	{
		printf("%%LSI-W-BADROM: %s: optional PCI BIOS %s could not be "
			"read; continuing without it.\n", devid_string, path.c_str());
		return false;
	}

	u32 image_size = 0;
	if (!valid_option_rom(image.data(), image.size(), &image_size) ||
	    image_size != image.size())
	{
		printf("%%LSI-W-BADROM: %s: optional PCI BIOS %s is not a valid "
			"LSI 53C1020 x86 ROM; continuing without it.\n",
			devid_string, path.c_str());
		return false;
	}

	if (flash.empty())
		flash.assign(LSI_FLASH_SIZE, 0xFFU);
	if (image_size > flash.size())
	{
		printf("%%LSI-W-BADROM: %s: optional PCI BIOS %s does not fit in "
			"the 512 KiB card flash; continuing without it.\n",
			devid_string, path.c_str());
		return false;
	}

	/* FLASH1030 places the BIOS at the top of the part.  BAR6 remaps that
	 * physical tail region to logical option-ROM address zero. */
	const u32 offset = (u32)flash.size() - image_size;
	std::copy(image.begin(), image.end(), flash.begin() + offset);
	mark_flash_dirty();
	printf("%s: seeded PCI BIOS %s into card flash (%u bytes).\n",
		devid_string, path.c_str(), image_size);
	return true;
}

/* Load the ARM IOC image into flash.  The controller model does not execute it. */
bool CLSI53C1020::load_ioc_firmware(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		printf("%%LSI-W-NOFW: %s: optional IOC firmware %s was not found; "
			"continuing without an IOC image.\n", devid_string,
			path.c_str());
		return false;
	}

	file.seekg(0, std::ios::end);
	const std::streamoff length = file.tellg();
	if (length <= 0 || length > LSI_FLASH_MAX_SIZE)
	{
		printf("%%LSI-W-BADFW: %s: optional IOC firmware %s has an invalid "
			"size; continuing without an IOC image.\n",
			devid_string, path.c_str());
		return false;
	}
	file.seekg(0, std::ios::beg);
	std::vector<u8> image((size_t)length);
	if (!file.read((char*)image.data(), (std::streamsize)image.size()))
	{
		printf("%%LSI-W-BADFW: %s: optional IOC firmware %s could not be "
			"read; continuing without an IOC image.\n",
			devid_string, path.c_str());
		return false;
	}

	u32 image_size = 0;
	if (!valid_ioc_firmware(image.data(), image.size(), &image_size) ||
	    image_size != image.size())
	{
		printf("%%LSI-W-BADFW: %s: optional IOC firmware %s is not valid "
			"for this card; continuing without an IOC image.\n",
			devid_string, path.c_str());
		return false;
	}

	if (flash.empty())
		flash.assign(LSI_FLASH_SIZE, 0xFFU);
	if (image_size > flash.size())
	{
		printf("%%LSI-W-BADFW: %s: optional IOC firmware %s does not fit "
			"in the 512 KiB card flash; continuing without it.\n",
			devid_string, path.c_str());
		return false;
	}

	std::copy(image.begin(), image.end(), flash.begin());
	mark_flash_dirty();
	const u32 version = get_le32(&image[0x24]);
	if (get_le16(&image[0x22]) == LSI_FW_PRODUCT_1030_B0_COMPAT)
	{
		printf("%s: IOC image is tagged for the 1030 B0 family; it is "
			"retained in flash while the emulated IOC remains a 53C1020 C0.\n",
			devid_string);
	}
	printf("%s: seeded IOC firmware %s into card flash "
		"(%u.%02u.%02u.%02u).\n",
		devid_string, path.c_str(), (version >> 24) & 0xFFU,
		(version >> 16) & 0xFFU, (version >> 8) & 0xFFU, version & 0xFFU);
	return true;
}

void CLSI53C1020::rebuild_flash_views()
{
	option_rom_offset = 0;
	option_rom_size = 0;
	ioc_firmware_size = 0;
	firmware_product_id = LSI_FW_PRODUCT_1020_C0;
	firmware_version = LSI_FW_VERSION;

	if (flash.empty())
	{
		flash_views_dirty = false;
		return;
	}

	if (valid_ioc_firmware(flash.data(), flash.size(), &ioc_firmware_size))
	{
		firmware_version = get_le32(&flash[0x24]);
	}

	/* A production BIOS is top-aligned; use the highest valid image. */
	for (u32 offset = (u32)flash.size() - 512U;; offset -= 512U)
	{
		u32 bytes = 0;
		if (valid_option_rom(&flash[offset], flash.size() - offset, &bytes))
		{
			option_rom_offset = offset;
			option_rom_size = bytes;
			break;
		}
		if (offset == 0U)
			break;
	}

	/* The Expansion ROM aperture is a logical BIOS view.  The IOC firmware
	 * remains at physical flash offset zero and is never exposed as x86 code. */
	flash_views_dirty = false;
}

void CLSI53C1020::mark_flash_dirty()
{
	flash_views_dirty = true;
	if (!flash_path.empty())
	{
		flash_dirty = true;
		flash_last_dirty = std::time(nullptr);
	}
}

/**
 * Save the raw card flash using a same-directory temporary file and replace.
 */
bool CLSI53C1020::save_flash()
{
	if (flash_path.empty() || flash.empty())
		return true;

	const std::string temporary = flash_path + ".tmp";
	FILE* file = fopen(temporary.c_str(), "wb");
	if (!file)
	{
		printf("%%LSI-W-FLASHSAVE: %s: card flash could not be saved to %s.\n",
			devid_string, flash_path.c_str());
		return false;
	}

	bool okay = fwrite(flash.data(), 1, flash.size(), file) == flash.size();
	if (okay)
		okay = fflush(file) == 0;
#if defined(_WIN32)
	if (okay)
		okay = _commit(_fileno(file)) == 0;
#elif !defined(__VMS)
	if (okay)
		okay = fsync(fileno(file)) == 0;
#endif
	if (fclose(file) != 0)
		okay = false;
	if (!okay)
	{
		remove(temporary.c_str());
		printf("%%LSI-W-FLASHSAVE: %s: short or failed write saving card "
			"flash to %s.\n", devid_string, flash_path.c_str());
		return false;
	}

#if defined(_WIN32)
	okay = MoveFileExA(temporary.c_str(), flash_path.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#elif defined(__VMS)
	remove(flash_path.c_str());
	okay = rename(temporary.c_str(), flash_path.c_str()) == 0;
#else
	okay = rename(temporary.c_str(), flash_path.c_str()) == 0;
#endif
	if (!okay)
	{
		remove(temporary.c_str());
		printf("%%LSI-W-FLASHSAVE: %s: card flash temporary file could not "
			"replace %s.\n", devid_string, flash_path.c_str());
		return false;
	}

	printf("%s: card flash saved to %s.\n", devid_string, flash_path.c_str());
	return true;
}

void CLSI53C1020::flush_flash()
{
	if (flash_views_dirty)
		rebuild_flash_views();
	if (flash_dirty)
	{
		if (save_flash())
			flash_dirty = false;
		else
			flash_last_dirty = std::time(nullptr);
	}
}

void CLSI53C1020::start_threads()
{
}

void CLSI53C1020::stop_threads()
{
}

CLSI53C1020::~CLSI53C1020()
{
	stop_threads();
	if (myRegLock)
	{
		CScopedLock<CMutex> regLock(myRegLock);
		flush_flash();
	}
	else
	{
		flush_flash();
	}
	delete myRegLock;
	myRegLock = nullptr;
	scsi_bus[0] = nullptr;
}

void CLSI53C1020::ResetPCI()
{
	if (!myRegLock)
	{
		flush_flash();
		CPCIDevice::ResetPCI();
		chip_reset();
		return;
	}

	CScopedLock<CMutex> regLock(myRegLock);
	flush_flash();
	CPCIDevice::ResetPCI();
	chip_reset();
}

/**
 * Reset the controller and its SCSI bus.
 *
 * Message-unit resets intentionally preserve configuration pages.  A full
 * chip reset restores those pages to their power-on values as well.
 */
void CLSI53C1020::chip_reset()
{
	flush_flash();
	download_image.clear();
	download_image_type = 0;
	download_active = false;
	clear_transport();
	clear_ioc_init_state();
	state.scsi_port1_cfg[0] = (u32)LSI_PORT1_CONFIGURATION;
	state.scsi_port1_cfg[1] = 0;
	for (u32 target = 0; target < LSI_MAX_TARGETS; target++)
	{
		state.scsi_dev1_params[target] = LSI_U320_DEV1_PARAMS;
		state.scsi_dev1_cfg[target] = 0;
	}
	reset_scsi_bus();
	state.host_interrupt_mask = LSI_HIM_RESET_VALUE;
	state.write_sequence_pos = 0;
	state.diag_write_enable = false;
	state.host_diag_bits = LSI_HOSTDIAG_RESET_HISTORY;
	state.test_base = 0;
	state.diag_rw_addr = 0;
	memset(state.shared_ram, 0, sizeof(state.shared_ram));
	memset(state.diag_buffer, 0, sizeof(state.diag_buffer));
	state.flash_csr = 0;
	state.flash_mode = LSI_FLASH_MODE_READ;
	state.ioc_state = LSI_IOC_STATE_READY;
	state.fault_code = 0;
	trace_his_valid = false;
	trace_doorbell_valid = false;
	do_pci_interrupt(0, false);
	state.irq_asserted = false;
}

void CLSI53C1020::register_disk(class CDisk* dsk, int bus, int dev)
{
	if (bus != 0)
		FAILURE(Configuration, "LSI53C1020 disk bus number out of range");
	if (dev < 0 || dev >= LSI_MAX_TARGETS)
		FAILURE(Configuration, "LSI53C1020 disk target number out of range");
	if (dev == LSI_PORT_SCSI_ID)
		FAILURE(Configuration, "LSI53C1020 target 7 is the SCSI initiator");

	CDiskController::register_disk(dsk, bus, dev);
	dsk->scsi_register(0, scsi_bus[0], dev);
	dsk->scsi_reset();
}

static u32 lsi_magic1 = 0x4C534931U;
static u32 lsi_magic2 = 0x3149534CU;

/**
 * Save controller state to a Virtual Machine State file.
 */
int CLSI53C1020::SaveState(FILE* f)
{
	long ss = sizeof(state);
	int res;
	const u32 flash_size = (u32)flash.size();

	if (flash_size != 0U && flash_size != LSI_FLASH_SIZE)
	{
		printf("%s: invalid card flash size!\n", devid_string);
		return -1;
	}

	flush_flash();

	if ((res = CPCIDevice::SaveState(f)))
		return res;

	if (fwrite(&lsi_magic1, sizeof(u32), 1, f) != 1 ||
	    fwrite(&ss, sizeof(long), 1, f) != 1 ||
	    fwrite(&state, sizeof(state), 1, f) != 1 ||
	    fwrite(&flash_size, sizeof(u32), 1, f) != 1 ||
	    (flash_size != 0U && fwrite(flash.data(), 1, flash_size, f) != flash_size) ||
	    fwrite(&lsi_magic2, sizeof(u32), 1, f) != 1)
	{
		printf("%s: failed to save controller state!\n", devid_string);
		return -1;
	}
	printf("%s: %d bytes saved.\n", devid_string,
		(int)(ss + sizeof(flash_size) + flash_size));
	return 0;
}

/**
 * Restore controller state from a Virtual Machine State file.
 */
int CLSI53C1020::RestoreState(FILE* f)
{
	long ss;
	u32 m1;
	u32 m2;
	u32 flash_size;
	int res;
	size_t r;
	SLSI_state saved_state;
	std::vector<u8> saved_flash;
	bool flash_in_state = false;

	if ((res = CPCIDevice::RestoreState(f)))
		return res;

	r = fread(&m1, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m1 != lsi_magic1)
	{
		printf("%s: MAGIC 1 does not match!\n", devid_string);
		return -1;
	}

	r = fread(&ss, sizeof(long), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (ss != sizeof(state))
	{
		printf("%s: STRUCT SIZE does not match!\n", devid_string);
		return -1;
	}

	r = fread(&saved_state, sizeof(saved_state), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	/* Older state files put the trailing magic directly after the controller
	 * structure.  New files store the complete raw card flash first. */
	r = fread(&flash_size, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}
	if (flash_size == lsi_magic2)
	{
		m2 = flash_size;
	}
	else
	{
		if (flash_size != 0U && flash_size != LSI_FLASH_SIZE)
		{
			printf("%s: card flash size does not match!\n", devid_string);
			return -1;
		}
		flash_in_state = true;
		saved_flash.resize(flash_size);
		if (flash_size != 0U &&
			fread(saved_flash.data(), 1, flash_size, f) != flash_size)
		{
			printf("%s: unexpected end of file!\n", devid_string);
			return -1;
		}
		r = fread(&m2, sizeof(u32), 1, f);
		if (r != 1)
		{
			printf("%s: unexpected end of file!\n", devid_string);
			return -1;
		}
	}

	if (m2 != lsi_magic2)
	{
		printf("%s: MAGIC 2 does not match!\n", devid_string);
		return -1;
	}

	state = saved_state;
	if (flash_in_state)
	{
		flash.swap(saved_flash);
		flash_dirty = false;
		flash_views_dirty = true;
		rebuild_flash_views();
		mark_flash_dirty();
		flush_flash();
	}
	else if (flash_views_dirty)
	{
		rebuild_flash_views();
	}
	download_image.clear();
	download_image_type = 0;
	download_active = false;

	register_bar(0, 0, endian_32(pci_state.config_data[0][4]),
		endian_32(pci_state.config_mask[0][4]));
	register_bar(0, 1, endian_32(pci_state.config_data[0][5]),
		endian_32(pci_state.config_mask[0][5]));
	register_bar(0, 3, endian_32(pci_state.config_data[0][7]),
		endian_32(pci_state.config_mask[0][7]));
	register_bar(0, 6, endian_32(pci_state.config_data[0][12]),
		endian_32(pci_state.config_mask[0][12]));

	trace_his_valid = false;
	trace_doorbell_valid = false;
	release_scsi_bus();
	const bool asserted = state.irq_asserted;
	state.irq_asserted = !asserted;
	eval_interrupts();
	printf("%s: %d bytes restored.\n", devid_string,
		(int)(ss + (flash_in_state ? sizeof(flash_size) + flash.size() : 0U)));
	return 0;
}

/**
 * Write data to one of the PCI BAR address ranges.
 */
void CLSI53C1020::WriteMem_Bar(int func, int bar, u32 address, int dsize, u32 data)
{
	if (func != 0)
		return;
	if (dsize != 8 && dsize != 16 && dsize != 32)
		return;
	if (bar == 6)
		return;
	if (bar == 3)
	{
		address &= 0xFFFFU;
		CScopedLock<CMutex> regLock(myRegLock);
		if ((state.host_diag_bits & LSI_HOSTDIAG_MEMORY_ENABLE) == 0U)
			return;

		const u32 base = state.test_base & 0xFFFF0000U;
		const u32 bytes = (u32)dsize / 8U;
		for (u32 i = 0; i < bytes; i++)
			write_b_diag(base | ((address + i) & 0xFFFFU),
				(u8)(data >> (i * 8U)));
		return;
	}
	if (bar != 0 && bar != 1)
		return;

	const bool io_space = bar == 0;
	// Memory[0] occupies the 1 KiB aperture described in section 2.3.1.3.
	address &= io_space ? 0xFFU : (LSI_MEMORY0_SIZE - 1U);
	CScopedLock<CMutex> regLock(myRegLock);

	const u8 bytes = (u8)(dsize / 8);
	u8 i = 0;
	while (i < bytes)
	{
		const u32 byte_offset = address + i;
		const u32 reg = byte_offset & ~3U;
		const u32 lane = byte_offset & 3U;
		u32 chunk = 4U - lane;
		if (chunk > (u32)(bytes - i))
			chunk = (u32)(bytes - i);

		u32 value;
		if (chunk == 4U)
		{
			value = (u32)(data >> (i * 8U));
		}
		else
		{
			value = peek_l_register(reg, io_space);
			for (u32 j = 0; j < chunk; j++)
			{
				const u32 shift = (lane + j) * 8U;
				value &= ~(0xFFU << shift);
				value |= (u32)((data >> ((i + j) * 8U)) & 0xFFU) << shift;
			}
		}
		write_l_register(reg, value, io_space);
		i = (u8)(i + chunk);
	}
}

/**
 * Read data from one of the PCI BAR address ranges.
 */
u32 CLSI53C1020::ReadMem_Bar(int func, int bar, u32 address, int dsize)
{
	if (func != 0)
		return 0xFFFFFFFFU;
	if (dsize != 8 && dsize != 16 && dsize != 32)
		return 0xFFFFFFFFU;
	if (bar == 6)
	{
		CScopedLock<CMutex> regLock(myRegLock);
		if (flash_views_dirty)
			rebuild_flash_views();
		u32 value = 0;
		const u32 bytes = (u32)dsize / 8U;
		for (u32 i = 0; i < bytes; i++)
		{
			const u32 offset = address + i;
			const u8 byte = offset < option_rom_size ?
				flash[option_rom_offset + offset] : 0xFFU;
			value |= (u32)byte << (i * 8U);
		}
		return value;
	}
	if (bar == 3)
	{
		address &= 0xFFFFU;
		CScopedLock<CMutex> regLock(myRegLock);
		if ((state.host_diag_bits & LSI_HOSTDIAG_MEMORY_ENABLE) == 0U)
			return 0xFFFFFFFFU;

		u32 value = 0;
		const u32 base = state.test_base & 0xFFFF0000U;
		const u32 bytes = (u32)dsize / 8U;
		for (u32 i = 0; i < bytes; i++)
			value |= (u32)read_b_diag(base | ((address + i) & 0xFFFFU)) <<
				(i * 8U);
		return value;
	}
	if (bar != 0 && bar != 1)
		return 0xFFFFFFFFU;

	const bool io_space = bar == 0;
	address &= io_space ? 0xFFU : (LSI_MEMORY0_SIZE - 1U);
	CScopedLock<CMutex> regLock(myRegLock);

	const u8 bytes = (u8)(dsize / 8);
	u32 value = 0;
	u32 cached_reg = 0xFFFFFFFFU;
	u32 cached_value = 0;
	for (u8 i = 0; i < bytes; i++)
	{
		const u32 byte_offset = address + i;
		const u32 reg = byte_offset & ~3U;
		if (reg != cached_reg)
		{
			cached_value = read_l_register(reg, io_space);
			cached_reg = reg;
		}
		value |= ((cached_value >> ((byte_offset & 3U) * 8U)) & 0xFFU) <<
			(i * 8U);
	}
	return value;
}

/*
 * Register access.
 */

u32 CLSI53C1020::read_l_doorbell() const
{
	u32 value = ((u32)state.ioc_state & 0x0FU) << 28;
	if (state.hs_receiving || state.hs_replying)
		value |= LSI_DOORBELL_USED;
	if (state.ioc_state == LSI_IOC_STATE_FAULT)
		value |= state.fault_code;
	if (state.hs_replying && state.hs_reply_pos < state.hs_reply_len_words)
	{
		value = (value & 0xFFFF0000U) | state.hs_reply_words[state.hs_reply_pos];
	}
	return value;
}

void CLSI53C1020::write_l_doorbell(u32 value)
{
	if (state.hs_receiving)
	{
		if (state.hs_received_dwords < LSI_HANDSHAKE_MAX_DWORDS)
			state.hs_buffer[state.hs_received_dwords] = value;
		state.hs_received_dwords++;
		if (state.hs_received_dwords >= state.hs_expected_dwords)
		{
			state.hs_receiving = false;
			execute_handshake();
		}
		return;
	}
	if (state.hs_replying)
		return;

	const u8 function = (u8)(value >> 24);
	switch (function)
	{
	case LSI_DOORBELL_IO_UNIT_RESET:
		reset_scsi_bus();
		message_unit_reset();
		break;

	case LSI_DOORBELL_MESSAGE_UNIT_RESET:
		message_unit_reset();
		break;

	case LSI_DOORBELL_HANDSHAKE:
		state.hs_expected_dwords = (u8)(value >> 16);
		state.hs_received_dwords = 0;
		memset(state.hs_buffer, 0, sizeof(state.hs_buffer));
		state.doorbell_interrupt = true;
		if (state.hs_expected_dwords == 0U)
			execute_handshake();
		else
			state.hs_receiving = true;
		break;

	case LSI_DOORBELL_REPLY_FRAME_REMOVAL:
	{
		u8 reply[LSI_REPLY_REMOVAL_BYTES];
		const u32 count = state.reply_free_count;

		memset(reply, 0, sizeof(reply));
		put_le32(reply, count);
		for (u32 i = 0; i < count; i++)
		{
			put_le32(&reply[sizeof(u32) * (i + 1U)],
				state.reply_free_fifo[state.reply_free_head]);
			state.reply_free_head =
				(state.reply_free_head + 1U) % LSI_REPLY_FIFO_DEPTH;
		}
		state.reply_free_count = 0;
		post_handshake_reply(reply, sizeof(u32) * (count + 1U));
		trace("reply-frame removal returned %u frame%s", (unsigned)count,
			count == 1U ? "" : "s");
		break;
	}

	default:
		break;
	}
}

/**
 * Assemble the Host Interrupt Status register from queue state.
 */
u32 CLSI53C1020::read_l_host_int_status() const
{
	u32 value = 0;
	if (state.doorbell_interrupt)
		value |= LSI_HIS_DOORBELL_INTERRUPT;
	/* bit3 is held sticky until the host reads the reply FIFO empty */
	if (state.reply_post_count != 0U || state.reply_int_sticky)
		value |= LSI_HIS_REPLY_INTERRUPT;
	return value;
}

/**
 * Clear the doorbell bit in the Host Interrupt Status register.
 */
void CLSI53C1020::write_l_host_int_status()
{
	state.doorbell_interrupt = false;
	if (state.hs_replying)
	{
		state.hs_int_cleared = true;
		advance_handshake_reply();
	}
}

/** Read one byte through the AMD 29LV040 flash command state machine. */
u8 CLSI53C1020::read_b_flash(u32 address) const
{
	if (flash.empty())
		return 0xFFU;
	address %= (u32)flash.size();

	if (state.flash_mode == LSI_FLASH_MODE_AUTOSELECT)
	{
		switch (address & (LSI_FLASH_SECTOR_SIZE - 1U))
		{
		case 0:
			return LSI_FLASH_MANUFACTURER_ID;
		case 1:
			return LSI_FLASH_DEVICE_ID;
		case 2:
			return 0; // sector protection is not fitted
		default:
			return 0;
		}
	}

	if (state.flash_mode == LSI_FLASH_MODE_CFI)
	{
		switch (address)
		{
		case 0x10:
			return 'Q';
		case 0x11:
			return 'R';
		case 0x12:
			return 'Y';
		case 0x13:
			return 0x02U; // AMD/Fujitsu command set
		case 0x27:
			return 19U; // 2^19 bytes
		case 0x2C:
			return 1U; // one erase-block region
		case 0x2D:
			return 7U; // eight sectors, encoded as count - 1
		case 0x2F:
			return 0U;
		case 0x30:
			return 1U; // 0x0100 * 256 = 64 KiB
		default:
			return 0;
		}
	}

	return flash[address];
}

/** Execute one byte-wide AMD 29LV040 command or programming operation. */
void CLSI53C1020::write_b_flash(u32 address, u8 value)
{
	if (flash.empty() || (state.flash_csr & LSI_FLASH_CSR_WRITE_ENABLE) == 0U)
		return;
	address %= (u32)flash.size();

	if (value == 0xF0U && state.flash_mode != LSI_FLASH_MODE_PROGRAM &&
	    state.flash_mode != LSI_FLASH_MODE_BYPASS_PROGRAM)
	{
		state.flash_mode = LSI_FLASH_MODE_READ;
		return;
	}

	switch (state.flash_mode)
	{
	case LSI_FLASH_MODE_READ:
		if (address == LSI_FLASH_UNLOCK_ADDRESS1 && value == 0xAAU)
			state.flash_mode = LSI_FLASH_MODE_UNLOCK1;
		else if (address == 0x55U && value == 0x98U)
			state.flash_mode = LSI_FLASH_MODE_CFI;
		break;

	case LSI_FLASH_MODE_UNLOCK1:
		state.flash_mode =
			address == LSI_FLASH_UNLOCK_ADDRESS2 && value == 0x55U ?
			LSI_FLASH_MODE_UNLOCK2 : LSI_FLASH_MODE_READ;
		break;

	case LSI_FLASH_MODE_UNLOCK2:
		if (address != LSI_FLASH_UNLOCK_ADDRESS1)
		{
			state.flash_mode = LSI_FLASH_MODE_READ;
			break;
		}
		switch (value)
		{
		case 0x90:
			state.flash_mode = LSI_FLASH_MODE_AUTOSELECT;
			break;
		case 0xA0:
			state.flash_mode = LSI_FLASH_MODE_PROGRAM;
			break;
		case 0x80:
			state.flash_mode = LSI_FLASH_MODE_ERASE_UNLOCK1;
			break;
		case 0x20:
			state.flash_mode = LSI_FLASH_MODE_BYPASS;
			break;
		default:
			state.flash_mode = LSI_FLASH_MODE_READ;
			break;
		}
		break;

	case LSI_FLASH_MODE_PROGRAM:
	{
		const u8 programmed = (u8)(flash[address] & value);
		if (programmed != flash[address])
		{
			flash[address] = programmed;
			mark_flash_dirty();
		}
		state.flash_mode = LSI_FLASH_MODE_READ;
		break;
	}

	case LSI_FLASH_MODE_ERASE_UNLOCK1:
		state.flash_mode =
			address == LSI_FLASH_UNLOCK_ADDRESS1 && value == 0xAAU ?
			LSI_FLASH_MODE_ERASE_UNLOCK2 : LSI_FLASH_MODE_READ;
		break;

	case LSI_FLASH_MODE_ERASE_UNLOCK2:
		state.flash_mode =
			address == LSI_FLASH_UNLOCK_ADDRESS2 && value == 0x55U ?
			LSI_FLASH_MODE_ERASE_SELECT : LSI_FLASH_MODE_READ;
		break;

	case LSI_FLASH_MODE_ERASE_SELECT:
		if (address == LSI_FLASH_UNLOCK_ADDRESS1 && value == 0x10U)
		{
			if (!std::all_of(flash.begin(), flash.end(), [](u8 byte) { return byte == 0xFFU; }))
			{
				std::fill(flash.begin(), flash.end(), 0xFFU);
				mark_flash_dirty();
			}
		}
		else if (value == 0x30U)
		{
			const u32 start = address & ~(LSI_FLASH_SECTOR_SIZE - 1U);
			const u32 end = std::min<u32>(start + LSI_FLASH_SECTOR_SIZE,
				(u32)flash.size());
			if (!std::all_of(flash.begin() + start, flash.begin() + end,
				[](u8 byte) { return byte == 0xFFU; }))
			{
				std::fill(flash.begin() + start, flash.begin() + end, 0xFFU);
				mark_flash_dirty();
			}
		}
		state.flash_mode = LSI_FLASH_MODE_READ;
		break;

	case LSI_FLASH_MODE_BYPASS:
		if (value == 0xA0U)
			state.flash_mode = LSI_FLASH_MODE_BYPASS_PROGRAM;
		else if (value == 0x90U)
			state.flash_mode = LSI_FLASH_MODE_BYPASS_EXIT;
		break;

	case LSI_FLASH_MODE_BYPASS_PROGRAM:
	{
		const u8 programmed = (u8)(flash[address] & value);
		if (programmed != flash[address])
		{
			flash[address] = programmed;
			mark_flash_dirty();
		}
		state.flash_mode = LSI_FLASH_MODE_BYPASS;
		break;
	}

	case LSI_FLASH_MODE_BYPASS_EXIT:
		state.flash_mode = value == 0 ? LSI_FLASH_MODE_READ : LSI_FLASH_MODE_BYPASS;
		break;

	case LSI_FLASH_MODE_AUTOSELECT:
	case LSI_FLASH_MODE_CFI:
	default:
		break;
	}
}

/** Read one byte from the IOC diagnostic address space. */
u8 CLSI53C1020::read_b_diag(u32 address) const
{
	if (address >= LSI_FLASH_ADDRESS_BASE &&
	    address - LSI_FLASH_ADDRESS_BASE < flash.size())
		return read_b_flash(address - LSI_FLASH_ADDRESS_BASE);
	if (address >= LSI_FLASH_CSR_ADDRESS && address < LSI_FLASH_CSR_ADDRESS + 4U)
		return (u8)(state.flash_csr >> ((address & 3U) * 8U));
	return state.diag_buffer[address & (LSI_DIAG_BUFFER_SIZE - 1U)];
}

/** Write one byte through the IOC diagnostic address space. */
void CLSI53C1020::write_b_diag(u32 address, u8 value)
{
	if (address >= LSI_FLASH_ADDRESS_BASE &&
	    address - LSI_FLASH_ADDRESS_BASE < flash.size())
	{
		write_b_flash(address - LSI_FLASH_ADDRESS_BASE, value);
		return;
	}
	if (address >= LSI_FLASH_CSR_ADDRESS && address < LSI_FLASH_CSR_ADDRESS + 4U)
	{
		const u32 shift = (address & 3U) * 8U;
		state.flash_csr = (state.flash_csr & ~(0xFFU << shift)) | ((u32)value << shift);
		return;
	}
	state.diag_buffer[address & (LSI_DIAG_BUFFER_SIZE - 1U)] = value;
}

/** Read a dword from diagnostic memory or the internal flash window. */
u32 CLSI53C1020::read_l_diag(u32 address) const
{
	return (u32)read_b_diag(address) |
	       ((u32)read_b_diag(address + 1U) << 8) |
	       ((u32)read_b_diag(address + 2U) << 16) |
	       ((u32)read_b_diag(address + 3U) << 24);
}

/** Write a dword to diagnostic memory or an encoded flash byte lane. */
void CLSI53C1020::write_l_diag(u32 address, u32 value)
{
	if (address >= LSI_FLASH_ADDRESS_BASE &&
	    address - LSI_FLASH_ADDRESS_BASE < flash.size())
	{
		const u32 lane = value & 3U;
		write_b_flash(address - LSI_FLASH_ADDRESS_BASE + lane, (u8)(value >> 24));
		return;
	}
	for (u32 i = 0; i < 4U; i++)
		write_b_diag(address + i, (u8)(value >> (i * 8U)));
}

/**
 * Read a register without consuming queue entries or advancing handshakes.
 *
 * Partial-width PCI writes use this path to merge untouched byte lanes.
 */
u32 CLSI53C1020::peek_l_register(u32 reg, bool io_space) const
{
	if (!io_space && reg >= LSI_SHARED_RAM_OFFSET && reg < LSI_MEMORY0_SIZE)
		return get_le32(&state.shared_ram[reg - LSI_SHARED_RAM_OFFSET]);

	switch (reg)
	{
	case R_DOORBELL:
		return read_l_doorbell();
	case R_WRITE_SEQUENCE:
		return 0x0000000BU;
	case R_HOST_DIAG:
		return state.host_diag_bits |
		       (state.diag_write_enable ? LSI_HOSTDIAG_DIAG_WRITE_ENABLE : 0U);
	case R_TEST_BASE:
		return state.test_base;
	case R_DIAG_RW_DATA:
		if (!io_space || (state.host_diag_bits & LSI_HOSTDIAG_DIAG_RW_ENABLE) == 0U)
			return 0xFFFFFFFFU;
		return read_l_diag(state.diag_rw_addr);
	case R_DIAG_RW_ADDR:
		return io_space ? state.diag_rw_addr : 0xFFFFFFFFU;
	case R_HOST_INT_STATUS:
		return read_l_host_int_status();
	case R_HOST_INT_MASK:
		return state.host_interrupt_mask;
	case R_REQUEST_QUEUE:
		return 0xFFFFFFFFU;
	case R_REPLY_QUEUE:
		return state.reply_post_count ? state.reply_post_fifo[state.reply_post_head] : 0xFFFFFFFFU;
	default:
		return 0U;
	}
}

/**
 * Read one aligned 32-bit register and apply its read side effects.
 */
u32 CLSI53C1020::read_l_register(u32 reg, bool io_space)
{
	u32 value;
	switch (reg)
	{
	case R_DIAG_RW_DATA:
		if (!io_space || (state.host_diag_bits & LSI_HOSTDIAG_DIAG_RW_ENABLE) == 0U)
			return 0xFFFFFFFFU;
		value = read_l_diag(state.diag_rw_addr);
		state.diag_rw_addr += 4U;
		return value;

	case R_REPLY_QUEUE:
		if (state.reply_post_count == 0U && state.bus_reset_event_pending && state.events_enabled)
		{
			state.bus_reset_event_pending = false;
			post_event(LSI_EVENT_IOC_BUS_RESET, 0U);
		}
		else if (state.reply_post_count == 0U && state.taskmgmt_mirror_pending)
		{
			state.taskmgmt_mirror_pending = false;
			state.hs_receiving = false;
			state.hs_expected_dwords = 0;
			state.hs_received_dwords = 0;
			state.hs_replying = false;
			state.hs_reply_len_words = 0;
			state.hs_reply_pos = 0;
			state.hs_word_consumed = false;
			state.hs_int_cleared = false;
			trace("task-management reply mirrored to reply-post FIFO");
			post_address_reply(state.taskmgmt_reply_frame, sizeof(state.taskmgmt_reply_frame));
			eval_interrupts();
		}
		if (state.reply_post_count != 0U)
		{
			value = state.reply_post_fifo[state.reply_post_head];
			state.reply_post_head = (state.reply_post_head + 1U) % LSI_REPLY_FIFO_DEPTH;
			state.reply_post_count--;
			/* bit3 stays asserted (sticky) until the empty read below */
			eval_interrupts();
			return value;
		}
		/* empty (p.4-42): this read clears the sticky reply interrupt */
		if (state.reply_int_sticky)
		{
			state.reply_int_sticky = false;
			eval_interrupts();
		}
		return 0xFFFFFFFFU;

	case R_HOST_INT_STATUS:
		value = read_l_host_int_status();
		if (trace_enabled && (!trace_his_valid || value != trace_last_his))
		{
			trace_last_his = value;
			trace_his_valid = true;
			trace("HIS read -> 0x%08x", value);
		}
		return value;

	case R_DOORBELL:
		value = read_l_doorbell();
		if (trace_enabled && (!trace_doorbell_valid || value != trace_last_doorbell))
		{
			trace_last_doorbell = value;
			trace_doorbell_valid = true;
			trace("doorbell read -> 0x%08x", value);
		}
		if (state.hs_replying && state.hs_reply_pos < state.hs_reply_len_words)
		{
			state.hs_word_consumed = true;
			advance_handshake_reply();
			eval_interrupts();
		}
		return value;

	default:
		return peek_l_register(reg, io_space);
	}
}

/**
 * Write one aligned 32-bit register and apply its side effects.
 */
void CLSI53C1020::write_l_register(u32 reg, u32 value, bool io_space)
{
	if (!io_space && reg >= LSI_SHARED_RAM_OFFSET && reg < LSI_MEMORY0_SIZE)
	{
		put_le32(&state.shared_ram[reg - LSI_SHARED_RAM_OFFSET], value);
		return;
	}

	switch (reg)
	{
	case R_DOORBELL:
		write_l_doorbell(value);
		break;

	case R_WRITE_SEQUENCE:
	{
		const u8 key = (u8)(value & 0x0FU);
		if (state.write_sequence_pos < 5U &&
		    key == lsi_write_sequence_keys[state.write_sequence_pos])
		{
			state.write_sequence_pos++;
			if (state.write_sequence_pos == 5U)
			{
				state.diag_write_enable = true;
				state.write_sequence_pos = 0;
			}
		}
		else
		{
			state.diag_write_enable = false;
			state.write_sequence_pos = key == lsi_write_sequence_keys[0] ? 1U : 0U;
		}
		break;
	}

	case R_HOST_DIAG:
		if (!state.diag_write_enable)
			break;
		if ((value & LSI_HOSTDIAG_RESET_ADAPTER) != 0U)
		{
			trace("HostDiag RESET_ADAPTER (hard reset)");
			chip_reset();
			break;
		}
		state.host_diag_bits = (u8)(value & LSI_HOSTDIAG_STORED_MASK);
		break;

	case R_TEST_BASE:
		state.test_base = value & 0xFFFF0000U;
		break;

	case R_DIAG_RW_DATA:
		if (io_space && state.diag_write_enable &&
		    (state.host_diag_bits & LSI_HOSTDIAG_DIAG_RW_ENABLE) != 0U)
		{
			write_l_diag(state.diag_rw_addr, value);
			state.diag_rw_addr += 4U;
		}
		break;

	case R_DIAG_RW_ADDR:
		if (io_space && state.diag_write_enable &&
		    (state.host_diag_bits & LSI_HOSTDIAG_DIAG_RW_ENABLE) != 0U)
			state.diag_rw_addr = value;
		break;

	case R_HOST_INT_STATUS:
		write_l_host_int_status();
		break;

	case R_HOST_INT_MASK:
		state.host_interrupt_mask = value & LSI_HIM_WRITABLE_MASK;
		break;

	case R_REQUEST_QUEUE:
		execute_request(value);
		break;

	case R_REPLY_QUEUE:
		if (state.reply_free_count < LSI_REPLY_FIFO_DEPTH)
		{
			const u32 tail =
			    (state.reply_free_head + state.reply_free_count) % LSI_REPLY_FIFO_DEPTH;
			state.reply_free_fifo[tail] = value;
			state.reply_free_count++;
		}
		break;

	default:
		break;
	}
	eval_interrupts();
}

/**
 * Check for deferred controller work.
 *
 * Flash metadata is rebuilt immediately.  Backing-file writes are delayed so
 * a sequence of byte-program operations is saved as one transaction.
 */
void CLSI53C1020::check_state()
{
	if (!myRegLock)
		return;

	CScopedLock<CMutex> regLock(myRegLock);
	if (flash_views_dirty)
		rebuild_flash_views();
	if (!flash_dirty)
		return;

	const std::time_t now = std::time(nullptr);
	if (flash_last_dirty == 0 ||
		std::difftime(now, flash_last_dirty) >= LSI_FLASH_SAVE_DELAY)
		flush_flash();
}

void CLSI53C1020::trace(const char* fmt, ...) const
{
	if (!trace_enabled)
		return;

	fprintf(stderr, "[lsi53c1020] ");
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/*
 * Fusion-MPT transport and reset handling.
 */

void CLSI53C1020::clear_transport()
{
	state.hs_receiving = false;
	state.hs_expected_dwords = 0;
	state.hs_received_dwords = 0;
	memset(state.hs_buffer, 0, sizeof(state.hs_buffer));
	state.hs_replying = false;
	state.hs_reply_len_words = 0;
	state.hs_reply_pos = 0;
	state.hs_word_consumed = false;
	state.hs_int_cleared = false;
	memset(state.hs_reply_words, 0, sizeof(state.hs_reply_words));
	state.doorbell_interrupt = false;
	state.reply_post_head = 0;
	state.reply_post_count = 0;
	memset(state.reply_post_fifo, 0, sizeof(state.reply_post_fifo));
	state.reply_int_sticky = false;
	state.reply_free_head = 0;
	state.reply_free_count = 0;
	memset(state.reply_free_fifo, 0, sizeof(state.reply_free_fifo));
}

void CLSI53C1020::clear_ioc_init_state()
{
	state.who_init = 0;
	state.reply_frame_size = LSI_REPLY_FRAME_BYTES;
	state.host_mfa_high_addr = 0;
	state.sense_buffer_high_addr = 0;
	state.port_enabled = false;
	state.events_enabled = false;
	state.event_msg_context = 0;
	state.event_context = 0;
	state.bus_reset_event_pending = false;
	state.taskmgmt_mirror_pending = false;
	memset(state.taskmgmt_reply_frame, 0, sizeof(state.taskmgmt_reply_frame));
}

void CLSI53C1020::release_scsi_bus()
{
	if (!scsi_bus[0])
		return;
	scsi_bus[0]->state.phase = SCSI_PHASE_FREE;
	scsi_bus[0]->state.target = -1;
	scsi_bus[0]->state.initiator = -1;
}

void CLSI53C1020::reset_scsi_target(u8 target)
{
	release_scsi_bus();
	CDisk* disk = get_disk(0, target);
	if (disk)
		disk->scsi_reset();
}

void CLSI53C1020::reset_scsi_bus()
{
	release_scsi_bus();
	for (u8 target = 0; target < LSI_MAX_TARGETS; target++)
	{
		CDisk* disk = get_disk(0, target);
		if (disk)
			disk->scsi_reset();
	}
}

void CLSI53C1020::message_unit_reset()
{
	clear_transport();
	clear_ioc_init_state();
	state.host_interrupt_mask = LSI_HIM_RESET_VALUE;
	state.ioc_state = LSI_IOC_STATE_READY;
	state.fault_code = 0;
	eval_interrupts();
}

void CLSI53C1020::enter_fault(u16 fault_code)
{
	state.ioc_state = LSI_IOC_STATE_FAULT;
	state.fault_code = fault_code;
	trace("FAULT entered, code=0x%04x", (unsigned)fault_code);
}

void CLSI53C1020::post_handshake_reply(const u8* frame, u32 frame_bytes)
{
	u32 words = (frame_bytes + 1U) / 2U;
	if (words > LSI_REPLY_MAX_WORDS)
		words = LSI_REPLY_MAX_WORDS;

	for (u32 i = 0; i < words; i++)
	{
		const u16 lo = frame[i * 2U];
		const u16 hi = (i * 2U + 1U < frame_bytes) ? frame[i * 2U + 1U] : 0U;
		state.hs_reply_words[i] = (u16)(lo | (hi << 8));
	}
	state.hs_reply_len_words = (u16)words;
	state.hs_reply_pos = 0;
	state.hs_word_consumed = false;
	state.hs_int_cleared = false;
	state.hs_replying = true;
	state.doorbell_interrupt = true;
}

void CLSI53C1020::advance_handshake_reply()
{
	if (!state.hs_replying || !state.hs_word_consumed || !state.hs_int_cleared)
		return;

	state.hs_word_consumed = false;
	state.hs_int_cleared = false;
	state.hs_reply_pos++;
	if (state.hs_reply_pos >= state.hs_reply_len_words)
	{
		state.hs_replying = false;
		state.taskmgmt_mirror_pending = false;
	}
	state.doorbell_interrupt = true;
}

bool CLSI53C1020::dma_read(u64 address, u8* data, u32 bytes)
{
	if (bytes == 0)
		return true;
	if ((address >> 32) != 0 || address > 0xFFFFFFFFULL - (u64)(bytes - 1U))
	{
		memset(data, 0, bytes);
		trace("unsupported 64-bit DMA read at 0x%016" PRIx64, address);
		enter_fault(LSI_FAULT_NO_DMA);
		return false;
	}
	do_pci_read((u32)address, data, 1, bytes);
	return true;
}

bool CLSI53C1020::dma_write(u64 address, const u8* data, u32 bytes)
{
	if (bytes == 0)
		return true;
	if ((address >> 32) != 0 || address > 0xFFFFFFFFULL - (u64)(bytes - 1U))
	{
		trace("unsupported 64-bit DMA write at 0x%016" PRIx64, address);
		enter_fault(LSI_FAULT_NO_DMA);
		return false;
	}
	// CPCIDevice predates const-correct write buffers; it does not modify data.
	do_pci_write((u32)address, const_cast<u8*>(data), 1, bytes);
	return true;
}

/**
 * DMA an MPI reply into a reply-free frame and post its descriptor.
 */
void CLSI53C1020::post_address_reply(const u8* frame, u32 frame_bytes)
{
	if (state.reply_post_count >= LSI_REPLY_FIFO_DEPTH)
	{
		enter_fault(LSI_FAULT_REPLY_POST_FULL);
		return;
	}
	if (state.reply_free_count == 0)
	{
		enter_fault(LSI_FAULT_REPLY_FREE_EMPTY);
		return;
	}

	const u32 mfa = state.reply_free_fifo[state.reply_free_head];
	u32 limit = state.reply_frame_size;
	if (limit > LSI_FRAME_MAX)
		limit = LSI_FRAME_MAX;
	if (frame_bytes > limit)
		frame_bytes = limit;
	const u64 mfa_address = ((u64)state.host_mfa_high_addr << 32) | (u64)(mfa & ~3U);
	if (!dma_write(mfa_address, frame, frame_bytes))
		return;

	state.reply_free_head = (state.reply_free_head + 1U) % LSI_REPLY_FIFO_DEPTH;
	state.reply_free_count--;
	const u32 reply = (mfa >> 1) | LSI_REPLY_MFA_ADDRESS_BIT;
	const u32 tail = (state.reply_post_head + state.reply_post_count) % LSI_REPLY_FIFO_DEPTH;
	state.reply_post_fifo[tail] = reply;
	state.reply_post_count++;
	state.reply_int_sticky = true;
	trace("reply-post push 0x%08x count=%u", reply, (unsigned)state.reply_post_count);
	eval_interrupts();
}

void CLSI53C1020::post_event(u32 event, u32 event_data0)
{
	if (!state.events_enabled)
		return;

	u8 reply[LSI_FRAME_MAX];
	memset(reply, 0, sizeof(reply));
	put_le16(&reply[0x00], 1U);
	reply[MPI_REPLY_MSG_LENGTH] = 8U;
	reply[MPI_MSG_FUNCTION] = LSI_FUNCTION_EVENT_NOTIFICATION;
	put_le32(&reply[MPI_MSG_CONTEXT], state.event_msg_context);
	put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_SUCCESS);
	put_le32(&reply[0x14], event);
	state.event_context++;
	put_le32(&reply[0x18], state.event_context);
	put_le32(&reply[0x1C], event_data0);
	post_address_reply(reply, 0x20U);
}

u32 CLSI53C1020::build_default_reply(const u8* request, u8* reply, u16 iocstatus)
{
	reply[MPI_REPLY_MSG_LENGTH] = 5U;
	reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
	memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);
	put_le16(&reply[MPI_REPLY_IOC_STATUS], iocstatus);
	return 20U;
}

u32 CLSI53C1020::execute_ioc_facts(const u8* request, u8* reply)
{
	put_le16(&reply[0x00], LSI_MPI_VERSION);
	reply[MPI_REPLY_MSG_LENGTH] = 0x0FU;
	reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
	put_le16(&reply[0x04], LSI_MPI_HEADER_VERSION);
	reply[0x06] = 0;
	memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);
	reply[0x14] = (u8)LSI_MAX_CHAIN_DEPTH;
	reply[0x15] = state.who_init;
	/* Request descriptors count 16-dword (64-byte) IOC blocks. */
	reply[0x16] = LSI_IOC_BLOCK_DWORDS;
	put_le16(&reply[0x18], LSI_REPLY_QUEUE_DEPTH);
	put_le16(&reply[0x1A], LSI_REQUEST_FRAME_DWORDS);
	put_le16(&reply[0x1E], firmware_product_id);
	put_le32(&reply[0x20], state.host_mfa_high_addr);
	put_le16(&reply[0x24], LSI_GLOBAL_CREDITS);
	reply[0x26] = LSI_NUMBER_OF_PORTS;
	reply[0x27] = state.events_enabled ? 1U : 0U;
	put_le32(&reply[0x28], state.sense_buffer_high_addr);
	put_le16(&reply[0x2C], state.reply_frame_size);
	reply[0x2E] = LSI_MAX_TARGETS;
	reply[0x2F] = LSI_MAX_BUSES;
	put_le32(&reply[0x30], ioc_firmware_size);
	put_le32(&reply[0x38], firmware_version);
	return 0x3CU;
}

u32 CLSI53C1020::execute_port_facts(const u8* request, u8* reply)
{
	const u8 port = request[0x06];
	reply[MPI_REPLY_MSG_LENGTH] = 0x0AU;
	reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
	reply[0x06] = port;
	memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);
	if (port != 0)
	{
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_INVALID_FIELD);
		return 0x28U;
	}
	reply[0x15] = LSI_PORTFACTS_PORTTYPE_SCSI;
	put_le16(&reply[0x16], LSI_MAX_TARGETS);
	put_le16(&reply[0x18], LSI_PORT_SCSI_ID);
	put_le16(&reply[0x1A], LSI_PORTFACTS_PROTOCOL_INITIATOR);
	return 0x28U;
}

u32 CLSI53C1020::execute_ioc_init(const u8* request, u8* reply)
{
	u16 iocstatus = LSI_IOCSTATUS_SUCCESS;
	if (state.ioc_state != LSI_IOC_STATE_READY)
	{
		iocstatus = LSI_IOCSTATUS_INVALID_STATE;
	}
	else
	{
		u16 reply_frame_size = get_le16(&request[0x0C]);
		state.who_init = request[0x00];
		if (reply_frame_size == 0 || reply_frame_size > LSI_REPLY_FRAME_BYTES)
			reply_frame_size = LSI_REPLY_FRAME_BYTES;
		state.reply_frame_size = reply_frame_size;
		state.host_mfa_high_addr = get_le32(&request[0x10]);
		state.sense_buffer_high_addr = get_le32(&request[0x14]);
		state.ioc_state = LSI_IOC_STATE_OPERATIONAL;
	}

	reply[0x00] = request[0x00];
	reply[MPI_REPLY_MSG_LENGTH] = 5U;
	reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
	reply[0x04] = request[0x04];
	reply[0x05] = request[0x05];
	reply[0x06] = request[0x06];
	memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);
	put_le16(&reply[MPI_REPLY_IOC_STATUS], iocstatus);
	return 20U;
}

u32 CLSI53C1020::execute_port_enable(const u8* request, u8* reply)
{
	u16 iocstatus = LSI_IOCSTATUS_SUCCESS;
	const u8 port = request[0x06];
	if (state.ioc_state != LSI_IOC_STATE_OPERATIONAL)
		iocstatus = LSI_IOCSTATUS_INVALID_STATE;
	else if (port != 0)
		iocstatus = LSI_IOCSTATUS_INVALID_FIELD;
	else
		state.port_enabled = true;

	reply[MPI_REPLY_MSG_LENGTH] = 5U;
	reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
	reply[0x06] = port;
	memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);
	put_le16(&reply[MPI_REPLY_IOC_STATUS], iocstatus);
	return 20U;
}

u32 CLSI53C1020::execute_event_notification(const u8* request, u8* reply)
{
	state.events_enabled = request[0x00] != 0;
	if (state.events_enabled)
		state.event_msg_context = get_le32(&request[MPI_MSG_CONTEXT]);
	put_le16(&reply[0x00], 1U);
	reply[MPI_REPLY_MSG_LENGTH] = 8U;
	reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
	reply[0x06] = 0;
	reply[0x07] = LSI_MSGFLAGS_CONTINUATION_REPLY;
	memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);
	put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_SUCCESS);
	put_le32(&reply[0x14], LSI_EVENT_EVENT_CHANGE);
	state.event_context++;
	put_le32(&reply[0x18], state.event_context);
	put_le32(&reply[0x1C], state.events_enabled ? 1U : 0U);
	return 0x20U;
}

/**
 * Download one firmware or BIOS segment and commit a complete image to flash.
 */
u32 CLSI53C1020::execute_fw_download(const u8* request, u8* reply)
{
	const u8 image_type = request[0x00];
	const u32 image_offset = get_le32(&request[0x14]);
	const u32 image_size = get_le32(&request[0x18]);
	u16 iocstatus = LSI_IOCSTATUS_SUCCESS;

	reply[0x00] = image_type;
	reply[MPI_REPLY_MSG_LENGTH] = 5U;
	reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
	reply[0x07] = request[0x07];
	memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);
	if (flash_views_dirty)
		rebuild_flash_views();

	if ((image_type != LSI_FW_DOWNLOAD_TYPE_FW &&
	     image_type != LSI_FW_DOWNLOAD_TYPE_BIOS) ||
	    request[0x0D] != 0U || request[0x0E] != 12U ||
	    (request[0x0F] & LSI_SGEF_TYPE_MASK) != 0U || image_size == 0U ||
	    image_offset > LSI_FLASH_SIZE ||
	    image_size > LSI_FLASH_SIZE - image_offset)
	{
		iocstatus = LSI_IOCSTATUS_INVALID_FIELD;
	}
	else
	{
		SLSI_sge sgl[LSI_SGL_MAX_ENTRIES];
		const int sge_count = collect_sgl(request, 0x1CU, request[0x02], sgl,
			LSI_SGL_MAX_ENTRIES);
		u32 capacity = 0;
		for (int i = 0; i < sge_count && capacity < image_size; i++)
			capacity += std::min<u32>(sgl[i].len, image_size - capacity);

		if (sge_count < 0 || capacity != image_size ||
		    (image_offset != 0U &&
		     (!download_active || download_image_type != image_type ||
		      image_offset != download_image.size())))
		{
			iocstatus = LSI_IOCSTATUS_INVALID_FIELD;
		}
		else
		{
			if (image_offset == 0U)
			{
				download_image.clear();
				download_image_type = image_type;
				download_active = true;
			}
			download_image.resize(image_offset + image_size);
			u32 copied = 0;
			for (int i = 0; i < sge_count && copied < image_size; i++)
			{
				const u32 bytes = std::min<u32>(sgl[i].len, image_size - copied);
				if (!dma_read(sgl[i].addr,
				              &download_image[image_offset + copied], bytes))
					return 0;
				copied += bytes;
			}
		}
	}

	bool complete = (request[0x07] & LSI_FLASH_DOWNLOAD_LAST) != 0U;
	if (iocstatus == LSI_IOCSTATUS_SUCCESS && download_active && !complete)
	{
		u32 bytes = 0;
		if (image_type == LSI_FW_DOWNLOAD_TYPE_FW &&
		    valid_ioc_firmware(download_image.data(), download_image.size(), &bytes) &&
		    (bytes == download_image.size() || download_image.size() == LSI_FLASH_SIZE))
			complete = true;
		else if (image_type == LSI_FW_DOWNLOAD_TYPE_BIOS &&
		         valid_option_rom(download_image.data(), download_image.size(), &bytes) &&
		         bytes == download_image.size())
			complete = true;
	}

	if (iocstatus == LSI_IOCSTATUS_SUCCESS && complete)
	{
		u32 validated_size = 0;
		download_active = false;
		if (flash.empty())
			flash.assign(LSI_FLASH_SIZE, 0xFFU);

		if (image_type == LSI_FW_DOWNLOAD_TYPE_FW &&
		    download_image.size() == flash.size() &&
		    valid_ioc_firmware(download_image.data(), download_image.size(),
		                       &validated_size))
		{
			flash = download_image;
		}
		else if (image_type == LSI_FW_DOWNLOAD_TYPE_FW &&
		         valid_ioc_firmware(download_image.data(), download_image.size(),
		                            &validated_size) &&
		         validated_size == download_image.size())
		{
			const u32 occupied = std::max<u32>(ioc_firmware_size, validated_size);
			const u32 erase_bytes = std::min<u32>((occupied + LSI_FLASH_SECTOR_SIZE - 1U) &
				~(LSI_FLASH_SECTOR_SIZE - 1U), (u32)flash.size());
			if (option_rom_size && erase_bytes > option_rom_offset)
			{
				iocstatus = LSI_IOCSTATUS_INVALID_FIELD;
			}
			else
			{
				std::fill(flash.begin(), flash.begin() + erase_bytes, 0xFFU);
				std::copy(download_image.begin(), download_image.end(), flash.begin());
			}
		}
		else if (image_type == LSI_FW_DOWNLOAD_TYPE_BIOS &&
		         valid_option_rom(download_image.data(), download_image.size(),
		                          &validated_size) &&
		         validated_size == download_image.size())
		{
			const u32 offset = (u32)flash.size() - validated_size;
			const u32 new_erase_offset =
				offset & ~(LSI_FLASH_SECTOR_SIZE - 1U);
			const u32 old_erase_offset = option_rom_size ?
				option_rom_offset & ~(LSI_FLASH_SECTOR_SIZE - 1U) :
				(u32)flash.size();
			const u32 erase_offset = std::min<u32>(new_erase_offset, old_erase_offset);
			if (erase_offset < ioc_firmware_size)
			{
				iocstatus = LSI_IOCSTATUS_INVALID_FIELD;
			}
			else
			{
				std::fill(flash.begin() + erase_offset, flash.end(), 0xFFU);
				std::copy(download_image.begin(), download_image.end(),
				          flash.begin() + offset);
			}
		}
		else
		{
			iocstatus = LSI_IOCSTATUS_INVALID_FIELD;
		}

		if (iocstatus == LSI_IOCSTATUS_SUCCESS)
		{
			mark_flash_dirty();
			flush_flash();
			if (flash_dirty)
				iocstatus = LSI_IOCSTATUS_INTERNAL_ERROR;
		}
		download_image.clear();
	}
	else if (iocstatus != LSI_IOCSTATUS_SUCCESS)
	{
		download_active = false;
		download_image.clear();
	}

	put_le16(&reply[MPI_REPLY_IOC_STATUS], iocstatus);
	return 20U;
}

/**
 * Upload an IOC firmware, BIOS, or complete raw-flash region to the guest.
 */
u32 CLSI53C1020::execute_fw_upload(const u8* request, u8* reply)
{
	const u8 image_type = request[0x00];
	const u32 image_offset = get_le32(&request[0x14]);
	const u32 image_size = get_le32(&request[0x18]);
	const u8* image = nullptr;
	u32 actual_size = 0;
	u16 iocstatus = LSI_IOCSTATUS_SUCCESS;

	reply[0x00] = image_type;
	reply[MPI_REPLY_MSG_LENGTH] = 6U;
	reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
	reply[0x07] = request[0x07];
	memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);

	if (flash_views_dirty)
		rebuild_flash_views();
	if (request[0x0D] != 0U || request[0x0E] != 12U ||
	    (request[0x0F] & LSI_SGEF_TYPE_MASK) != 0U || flash.empty())
	{
		iocstatus = LSI_IOCSTATUS_INVALID_FIELD;
	}
	else
	{
		switch (image_type)
		{
		case LSI_FW_UPLOAD_TYPE_IOC:
		case LSI_FW_UPLOAD_TYPE_FW:
			if (ioc_firmware_size)
			{
				image = flash.data();
				actual_size = ioc_firmware_size;
			}
			break;

		case LSI_FW_UPLOAD_TYPE_BIOS:
			if (option_rom_size)
			{
				image = &flash[option_rom_offset];
				actual_size = option_rom_size;
			}
			break;

		case LSI_FW_UPLOAD_TYPE_ALL:
			image = flash.data();
			actual_size = (u32)flash.size();
			break;

		default:
			break;
		}
		if (!image || image_offset > actual_size ||
		    image_size > actual_size - image_offset)
			iocstatus = LSI_IOCSTATUS_INVALID_FIELD;
	}

	if (iocstatus == LSI_IOCSTATUS_SUCCESS)
	{
		const u32 transfer = image_size;
		if (transfer)
		{
			SLSI_sge sgl[LSI_SGL_MAX_ENTRIES];
			const int sge_count = collect_sgl(request, 0x1CU, request[0x02], sgl,
				LSI_SGL_MAX_ENTRIES);
			u32 capacity = 0;
			for (int i = 0; i < sge_count && capacity < transfer; i++)
				capacity += std::min<u32>(sgl[i].len, transfer - capacity);
			if (sge_count < 0 || capacity != transfer)
			{
				iocstatus = LSI_IOCSTATUS_INVALID_FIELD;
			}
			else
			{
				u32 copied = 0;
				for (int i = 0; i < sge_count && copied < transfer; i++)
				{
					const u32 bytes = std::min<u32>(sgl[i].len, transfer - copied);
					if (!dma_write(sgl[i].addr, image + image_offset + copied, bytes))
						return 0;
					copied += bytes;
				}
			}
		}
	}

	put_le16(&reply[MPI_REPLY_IOC_STATUS], iocstatus);
	put_le32(&reply[0x14], actual_size);
	return 24U;
}

/*
 * SCSI request execution.
 */

/**
 * Parse the simple and chained scatter/gather elements in a request frame.
 */
int CLSI53C1020::collect_sgl(const u8* frame, u32 sge_offset,
	u8 chain_offset_dwords, SLSI_sge* out, u32 max_entries)
{
	u8 segment[LSI_SGL_SEG_MAX];
	const u8* current = frame;
	u32 segment_len = LSI_FRAME_MAX;
	u32 pos = sge_offset;
	u32 chain_pos = (u32)chain_offset_dwords * 4U;
	u32 elements = 0;
	int count = 0;

	for (;;)
	{
		if (++elements > LSI_SGL_MAX_ELEMENTS || pos + 8U > segment_len)
			return -1;

		const u32 flags_len = get_le32(&current[pos]);
		const u8 flags = (u8)(flags_len >> 24);
		const u8 type = (u8)(flags & LSI_SGEF_TYPE_MASK);
		u64 address;

		if (type == LSI_SGEF_TYPE_CHAIN)
		{
			const u32 chain_len = flags_len & 0xFFFFU;
			const u32 next_offset = (flags_len >> 16) & 0xFFU;
			if (flags & LSI_SGEF_ADDR64)
			{
				if (pos + 12U > segment_len)
					return -1;
				address =
				    (u64)get_le32(&current[pos + 4U]) | ((u64)get_le32(&current[pos + 8U]) << 32);
			}
			else
			{
				address = get_le32(&current[pos + 4U]);
			}
			if (chain_len < 8U || chain_len > sizeof(segment))
				return -1;
			if (!dma_read(address, segment, chain_len))
				return -1;
			current = segment;
			segment_len = chain_len;
			pos = 0;
			chain_pos = next_offset * 4U;
			continue;
		}

		if (type != LSI_SGEF_TYPE_SIMPLE)
			return -1;

		if (flags & LSI_SGEF_ADDR64)
		{
			if (pos + 12U > segment_len)
				return -1;
			address =
			    (u64)get_le32(&current[pos + 4U]) | ((u64)get_le32(&current[pos + 8U]) << 32);
			pos += 12U;
		}
		else
		{
			address = get_le32(&current[pos + 4U]);
			pos += 8U;
		}

		const u32 length = flags_len & LSI_SGE_LENGTH_MASK;
		if (length)
		{
			if ((u32)count >= max_entries)
				return -1;
			out[count].addr = address;
			out[count].len = length;
			count++;
		}

		if (flags & LSI_SGEF_END_OF_LIST)
			break;
		if (flags & LSI_SGEF_LAST_ELEMENT)
		{
			if (chain_pos == 0)
				break;
			pos = chain_pos;
		}
	}
	return count;
}

void CLSI53C1020::init_scsi_io_reply(const u8* request, u8* reply)
{
	reply[MPI_SCSI_TARGET_ID] = request[MPI_SCSI_TARGET_ID];
	reply[MPI_SCSI_BUS] = request[MPI_SCSI_BUS];
	reply[MPI_REPLY_MSG_LENGTH] = 9U;
	reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
	reply[MPI_SCSI_CDB_LENGTH] = request[MPI_SCSI_CDB_LENGTH];
	reply[MPI_SCSI_SENSE_LENGTH] = request[MPI_SCSI_SENSE_LENGTH];
	reply[0x07] = request[0x07];
	memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);
}

void CLSI53C1020::release_scsi_target(u8 target)
{
	if (!scsi_bus[0] || scsi_bus[0]->state.phase == SCSI_PHASE_FREE)
		return;
	if (scsi_bus[0]->state.phase == SCSI_PHASE_ARBITRATION)
		scsi_free(0);
	else
		scsi_bus[0]->free_bus(target);
}

/**
 * Run one command through the ES40 SCSI phase engine.
 */
bool CLSI53C1020::execute_scsi_command(u8 target, const u8* cdb, u8 cdb_len, const u8* data_out,
	u32 data_out_len, u8* data_in, u32 data_in_cap, SLSI_scsi_result& result)
{
	result.data_in_len = 0;
	result.data_out_used = 0;
	result.status = LSI_SCSI_STATUS_GOOD;
	result.data_out_short = false;
	if (!cdb || cdb_len == 0 || target >= LSI_MAX_TARGETS || !get_disk(0, target))
		return false;

	if (!scsi_arbitrate(0))
		return false;
	if (!scsi_select(0, target))
	{
		scsi_free(0);
		return false;
	}

	if (scsi_get_phase(0) == SCSI_PHASE_MSG_OUT)
	{
		u8* identify = (u8*)scsi_xfer_ptr(0, 1);
		identify[0] = 0x80U;
		scsi_xfer_done(0);
	}

	while (scsi_get_phase(0) == SCSI_PHASE_MSG_IN)
	{
		const size_t bytes = scsi_expected_xfer(0);
		if (!bytes)
			break;
		(void)scsi_xfer_ptr(0, bytes);
		scsi_xfer_done(0);
	}

	if (scsi_get_phase(0) != SCSI_PHASE_COMMAND)
	{
		release_scsi_target(target);
		return false;
	}

	u8* command = (u8*)scsi_xfer_ptr(0, cdb_len);
	memcpy(command, cdb, cdb_len);
	scsi_xfer_done(0);

	if (scsi_get_phase(0) == SCSI_PHASE_DATA_OUT)
	{
		const size_t expected = scsi_expected_xfer(0);
		if (!data_out || data_out_len < expected)
		{
			result.data_out_short = true;
			release_scsi_target(target);
			return true;
		}
		u8* target_data = (u8*)scsi_xfer_ptr(0, expected);
		memcpy(target_data, data_out, expected);
		result.data_out_used = (u32)expected;
		scsi_xfer_done(0);
	}

	if (scsi_get_phase(0) == SCSI_PHASE_DATA_IN)
	{
		const size_t expected = scsi_expected_xfer(0);
		u8* target_data = (u8*)scsi_xfer_ptr(0, expected);
		const size_t copy = std::min<size_t>(expected, data_in_cap);
		if (copy && data_in)
			memcpy(data_in, target_data, copy);
		result.data_in_len =
			(expected > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (u32)expected;
		scsi_xfer_done(0);
	}

	if (scsi_get_phase(0) != SCSI_PHASE_STATUS)
	{
		release_scsi_target(target);
		return false;
	}

	const size_t status_bytes = scsi_expected_xfer(0);
	if (!status_bytes)
	{
		release_scsi_target(target);
		return false;
	}
	u8* status = (u8*)scsi_xfer_ptr(0, status_bytes);
	result.status = status[0];
	scsi_xfer_done(0);

	while (scsi_get_phase(0) == SCSI_PHASE_MSG_IN)
	{
		const size_t bytes = scsi_expected_xfer(0);
		if (!bytes)
			break;
		(void)scsi_xfer_ptr(0, bytes);
		scsi_xfer_done(0);
	}

	if (scsi_get_phase(0) != SCSI_PHASE_FREE)
		release_scsi_target(target);
	return true;
}

u32 CLSI53C1020::request_sense(u8 target, u8* sense, u32 sense_cap)
{
	u8 cdb[6] = {0x03U, 0, 0, 0, 0, 0};
	cdb[4] = (u8)std::min<u32>(sense_cap, 0xFFU);
	SLSI_scsi_result result;
	if (!execute_scsi_command(target, cdb, sizeof(cdb), nullptr, 0, sense,
			sense_cap, result) || result.status != LSI_SCSI_STATUS_GOOD)
		return 0;
	return std::min<u32>(result.data_in_len, sense_cap);
}

u32 CLSI53C1020::execute_scsi_io(const u8* request, u8* reply)
{
	SLSI_sge sgl[LSI_SGL_MAX_ENTRIES];
	const u8 target = request[MPI_SCSI_TARGET_ID];
	const u8 bus = request[MPI_SCSI_BUS];
	u8 cdb_len = request[MPI_SCSI_CDB_LENGTH];
	const u8 sense_buf_len = request[MPI_SCSI_SENSE_LENGTH];
	const u8 lun = request[MPI_SCSI_LUN_BYTE];
	const u32 control = get_le32(&request[MPI_SCSI_CONTROL]);
	const u32 data_length = get_le32(&request[MPI_SCSI_DATA_LENGTH]);
	const u32 sense_low = get_le32(&request[MPI_SCSI_SENSE_ADDRESS]);
	const u64 sense_addr = ((u64)state.sense_buffer_high_addr << 32) | sense_low;
	const u32 direction = control & LSI_SCSIIO_CONTROL_DIR_MASK;

	if (state.ioc_state != LSI_IOC_STATE_OPERATIONAL || !state.port_enabled)
	{
		init_scsi_io_reply(request, reply);
		reply[MPI_SCSI_REPLY_STATE] = LSI_SCSI_STATE_NO_SCSI_STATUS;
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_INVALID_STATE);
		return 0x24U;
	}
	if (bus != 0 || target >= LSI_MAX_TARGETS || !get_disk(0, target))
	{
		init_scsi_io_reply(request, reply);
		reply[MPI_SCSI_REPLY_STATE] = LSI_SCSI_STATE_NO_SCSI_STATUS;
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_SCSI_DEVICE_NOT_THERE);
		return 0x24U;
	}

	if (lun != 0 || get_le32(&request[0x0C]) != 0 || get_le32(&request[0x10]) != 0)
	{
		u8 sense[18];
		memset(sense, 0, sizeof(sense));
		sense[0] = 0x70U;
		sense[2] = 0x05U;
		sense[7] = 10U;
		sense[12] = 0x25U;
		const u32 sense_n = std::min<u32>(sizeof(sense), sense_buf_len);
		if (sense_n && !dma_write(sense_addr, sense, sense_n))
			return 0;
		init_scsi_io_reply(request, reply);
		reply[MPI_SCSI_REPLY_STATUS] = LSI_SCSI_STATUS_CHECK_CONDITION;
		reply[MPI_SCSI_REPLY_STATE] = sense_n ? LSI_SCSI_STATE_AUTOSENSE_VALID : 0;
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_SUCCESS);
		put_le32(&reply[MPI_SCSI_REPLY_SENSE_COUNT], sense_n);
		return 0x24U;
	}

	if (cdb_len > 16U)
		cdb_len = 16U;
	if (data_length > LSI_MAX_TRANSFER_BYTES)
	{
		enter_fault(LSI_FAULT_XFER_TOO_LARGE);
		return 0;
	}

	int sge_count = 0;
	if (data_length && direction)
	{
		sge_count = collect_sgl(request, MPI_SCSI_SGL,
			request[MPI_SCSI_CHAIN_OFFSET], sgl,
			LSI_SGL_MAX_ENTRIES);
		if (sge_count < 0)
		{
			if (state.ioc_state == LSI_IOC_STATE_FAULT)
				return 0;
			init_scsi_io_reply(request, reply);
			reply[MPI_SCSI_REPLY_STATE] = LSI_SCSI_STATE_NO_SCSI_STATUS;
			put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_INTERNAL_ERROR);
			return 0x24U;
		}
	}

	std::vector<u8> buffer(data_length, 0);
	u32 gathered = 0;
	if (direction == LSI_SCSIIO_CONTROL_DIR_WRITE)
	{
		for (int i = 0; i < sge_count && gathered < data_length; i++)
		{
			const u32 chunk = std::min<u32>(sgl[i].len, data_length - gathered);
			if (!dma_read(sgl[i].addr, &buffer[gathered], chunk))
				return 0;
			gathered += chunk;
		}
	}

	SLSI_scsi_result result;
	if (!execute_scsi_command(target, &request[MPI_SCSI_CDB], cdb_len,
			buffer.empty() ? nullptr : buffer.data(), gathered,
			buffer.empty() ? nullptr : buffer.data(), data_length, result))
	{
		init_scsi_io_reply(request, reply);
		reply[MPI_SCSI_REPLY_STATE] = LSI_SCSI_STATE_NO_SCSI_STATUS;
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_INTERNAL_ERROR);
		return 0x24U;
	}

	const u32 wanted_in = std::min<u32>(result.data_in_len, data_length);
	u32 moved_in = 0;
	if (direction == LSI_SCSIIO_CONTROL_DIR_READ && wanted_in)
	{
		for (int i = 0; i < sge_count && moved_in < wanted_in; i++)
		{
			const u32 chunk = std::min<u32>(sgl[i].len, wanted_in - moved_in);
			if (!dma_write(sgl[i].addr, &buffer[moved_in], chunk))
				return 0;
			moved_in += chunk;
		}
	}

	const u32 transferred = (direction == LSI_SCSIIO_CONTROL_DIR_READ) ?
		moved_in : result.data_out_used;

	if (result.data_out_short)
	{
		init_scsi_io_reply(request, reply);
		reply[MPI_SCSI_REPLY_STATE] = LSI_SCSI_STATE_NO_SCSI_STATUS;
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_SCSI_DATA_UNDERRUN);
		put_le32(&reply[MPI_SCSI_REPLY_TRANSFER_COUNT], transferred);
		return 0x24U;
	}

	if (result.status == LSI_SCSI_STATUS_GOOD && result.data_in_len > data_length)
	{
		init_scsi_io_reply(request, reply);
		reply[MPI_SCSI_REPLY_STATUS] = LSI_SCSI_STATUS_GOOD;
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_SCSI_DATA_OVERRUN);
		put_le32(&reply[MPI_SCSI_REPLY_TRANSFER_COUNT], transferred);
		return 0x24U;
	}

	if (result.status == LSI_SCSI_STATUS_GOOD && transferred == data_length)
	{
		init_scsi_io_reply(request, reply);
		reply[MPI_SCSI_REPLY_STATUS] = LSI_SCSI_STATUS_GOOD;
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_SUCCESS);
		put_le32(&reply[MPI_SCSI_REPLY_TRANSFER_COUNT], transferred);
		return 0x24U;
	}

	init_scsi_io_reply(request, reply);
	reply[MPI_SCSI_REPLY_STATUS] = result.status;
	put_le32(&reply[MPI_SCSI_REPLY_TRANSFER_COUNT], transferred);
	if (result.status == LSI_SCSI_STATUS_CHECK_CONDITION)
	{
		u8 sense[18];
		const u32 sense_n =
		    request_sense(target, sense, std::min<u32>(sizeof(sense), sense_buf_len));
		if (sense_n && !dma_write(sense_addr, sense, sense_n))
			return 0;
		reply[MPI_SCSI_REPLY_STATE] = sense_n ? LSI_SCSI_STATE_AUTOSENSE_VALID : 0;
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_SUCCESS);
		put_le32(&reply[MPI_SCSI_REPLY_SENSE_COUNT], sense_n);
	}
	else
	{
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_SCSI_DATA_UNDERRUN);
	}
	return 0x24U;
}

/*
 * MPI configuration pages.
 */

u32 CLSI53C1020::build_config_page(u8 type, u8 number, u32 page_address, bool factory_default,
	u8* data)
{
	u32 bytes = 0;

	memset(data, 0, LSI_CFG_PAGE_MAX);
	switch (type)
	{
	case LSI_CFG_PAGETYPE_MANUFACTURING:
		if (number == 0U)
		{
			bytes = 76U;
			data[0] = 0x00U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_MANUFACTURING | LSI_CFG_PAGEATTR_READ_ONLY;
			memcpy(&data[0x04], "53C1020", 7U);
			memcpy(&data[0x14], "A0", 2U);
			memcpy(&data[0x1C], "LSI53C1020", 10U);
			memcpy(&data[0x2C], "LSI53C1020", 10U);
		}
		else if (number == 1U)
		{
			/* Manufacturing page 1 contains a 256-byte PCI VPD image. */
			static const char identification[] = "LSI53C1020";
			static const char part_number[] = "LSI53C1020";
			static const char serial_number[] = "00000000";
			u32 vpd = 0x04U;

			bytes = 0x104U;
			data[0] = 0x00U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_MANUFACTURING | LSI_CFG_PAGEATTR_READ_ONLY;

			data[vpd++] = 0x82U;
			data[vpd++] = (u8)(sizeof(identification) - 1U);
			data[vpd++] = 0x00U;
			memcpy(&data[vpd], identification, sizeof(identification) - 1U);
			vpd += (u32)(sizeof(identification) - 1U);

			data[vpd++] = 0x90U;
			const u32 read_only_length = vpd;
			vpd += 2U;
			data[vpd++] = 'P';
			data[vpd++] = 'N';
			data[vpd++] = (u8)(sizeof(part_number) - 1U);
			memcpy(&data[vpd], part_number, sizeof(part_number) - 1U);
			vpd += (u32)(sizeof(part_number) - 1U);
			data[vpd++] = 'S';
			data[vpd++] = 'N';
			data[vpd++] = (u8)(sizeof(serial_number) - 1U);
			memcpy(&data[vpd], serial_number, sizeof(serial_number) - 1U);
			vpd += (u32)(sizeof(serial_number) - 1U);
			data[vpd++] = 'R';
			data[vpd++] = 'V';
			data[vpd++] = 0x01U;
			const u32 checksum_position = vpd++;
			data[read_only_length] = (u8)(vpd - read_only_length - 2U);
			data[read_only_length + 1U] = 0x00U;

			u8 checksum = 0;
			for (u32 i = 0x04U; i < checksum_position; i++)
				checksum = (u8)(checksum + data[i]);
			data[checksum_position] = (u8)(0U - checksum);
			data[vpd] = 0x78U;
		}
		break;

	case LSI_CFG_PAGETYPE_IOC:
		if (number == 0U)
		{
			bytes = 0x1CU;
			data[0] = 0x01U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_IOC | LSI_CFG_PAGEATTR_READ_ONLY;
			put_le16(&data[0x0C], LSI_PCI_VENDOR_ID);
			put_le16(&data[0x0E], LSI_PCI_DEVICE_ID);
			data[0x10] = LSI_PCI_REVISION_ID;
			put_le32(&data[0x14], LSI_PCI_CLASS_CODE);
			put_le16(&data[0x18], LSI_PCI_SUBSYSTEM_VENDOR_ID);
			put_le16(&data[0x1A], LSI_PCI_SUBSYSTEM_DEVICE_ID);
		}
		else if (number == 1U)
		{
			/* Reply coalescing is disabled; only the PCI slot is reported. */
			bytes = 0x10U;
			data[0] = 0x01U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_IOC | LSI_CFG_PAGEATTR_CHANGEABLE;
			data[0x0D] = (u8)pci_dev();
		}
		else if (number == 2U)
		{
			bytes = 0x0CU;
			data[0] = 0x01U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_IOC | LSI_CFG_PAGEATTR_READ_ONLY;
		}
		else if (number == 3U)
		{
			/* No RAID physical disks. */
			bytes = 0x08U;
			data[0] = 0x01U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_IOC | LSI_CFG_PAGEATTR_READ_ONLY;
		}
		else if (number == 5U)
		{
			/* No RAID hot spares. */
			bytes = 0x0CU;
			data[0] = 0x00U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_IOC | LSI_CFG_PAGEATTR_READ_ONLY;
		}
		break;

	case LSI_CFG_PAGETYPE_BIOS:
		if (number == 3U)
		{
			/* Present but empty for driver startup probes. */
			bytes = 0x08U;
			data[0] = 0x00U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_BIOS | LSI_CFG_PAGEATTR_READ_ONLY;
		}
		break;

	case LSI_CFG_PAGETYPE_IO_UNIT:
		if (number == 0U)
		{
			bytes = 0x0CU;
			data[0] = 0x00U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_IO_UNIT | LSI_CFG_PAGEATTR_READ_ONLY;
			put_le32(&data[0x04], 0x53C10200U);
			put_le32(&data[0x08], 0x0000103CU);
		}
		else if (number == 1U)
		{
			bytes = 0x08U;
			data[0] = 0x01U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_IO_UNIT | LSI_CFG_PAGEATTR_PERSISTENT;
			put_le32(&data[0x04], 0x00000001U);
		}
		else if (number == 2U)
		{
			bytes = 0x1CU;
			data[0] = 0x00U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_IO_UNIT | LSI_CFG_PAGEATTR_PERSISTENT;
		}
		break;

	case LSI_CFG_PAGETYPE_SCSI_PORT:
		if ((page_address & 0xFFU) != 0U)
			break;

		if (number == 0U)
		{
			bytes = 0x0CU;
			data[0] = 0x01U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_SCSI_PORT | LSI_CFG_PAGEATTR_READ_ONLY;
			put_le32(&data[0x04], LSI_U320_PORT_CAPABILITIES);
			put_le32(&data[0x08], LSI_PHYS_INTERFACE_LVD);
		}
		else if (number == 1U)
		{
			bytes = 0x0CU;
			data[0] = 0x03U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_SCSI_PORT | LSI_CFG_PAGEATTR_CHANGEABLE;
			if (factory_default)
			{
				put_le32(&data[0x04], LSI_PORT1_CONFIGURATION);
				put_le32(&data[0x08], 0U);
			}
			else
			{
				put_le32(&data[0x04], state.scsi_port1_cfg[0]);
				put_le32(&data[0x08], state.scsi_port1_cfg[1]);
			}
		}
		else if (number == 2U)
		{
			bytes = 76U;
			data[0] = 0x01U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_SCSI_PORT | LSI_CFG_PAGEATTR_PERSISTENT;
			put_le32(&data[0x08], LSI_PORT_SCSI_ID);
			for (u32 target = 0; target < LSI_MAX_TARGETS; target++)
			{
				data[0x0C + target * 4U] = 10U;
				data[0x0D + target * 4U] = 0x08U;
			}
		}
		break;

	case LSI_CFG_PAGETYPE_SCSI_DEVICE:
	{
		const u32 target = page_address & 0xFFU;
		const u32 bus = (page_address >> 8) & 0xFFU;
		if ((page_address >> 28) != 0U || bus != 0U || target >= LSI_MAX_TARGETS)
			break;

		if (number == 0U)
		{
			bytes = 0x0CU;
			data[0] = 0x02U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_SCSI_DEVICE | LSI_CFG_PAGEATTR_READ_ONLY;
			put_le32(&data[0x04],
			           factory_default ? LSI_U320_DEV1_PARAMS : state.scsi_dev1_params[target]);
			if (get_disk(0, (int)target))
				put_le32(&data[0x08], 0x00000001U);
		}
		else if (number == 1U)
		{
			bytes = 0x10U;
			data[0] = 0x03U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_SCSI_DEVICE | LSI_CFG_PAGEATTR_CHANGEABLE;
			if (factory_default)
			{
				put_le32(&data[0x04], LSI_U320_DEV1_PARAMS);
				put_le32(&data[0x0C], 0U);
			}
			else
			{
				put_le32(&data[0x04], state.scsi_dev1_params[target]);
				put_le32(&data[0x0C], state.scsi_dev1_cfg[target]);
			}
		}
		else if (number == 9U)
		{
			/* Present but empty for driver startup probes. */
			bytes = 0x08U;
			data[0] = 0x00U;
			data[1] = (u8)(bytes / 4U);
			data[2] = number;
			data[3] = LSI_CFG_PAGETYPE_SCSI_DEVICE | LSI_CFG_PAGEATTR_READ_ONLY;
		}
		break;
	}

	default:
		break;
	}
	return bytes;
}

void CLSI53C1020::store_config_page(u8 type, u8 number, u32 page_address, const u8* data, u32 bytes)
{
	if (type == LSI_CFG_PAGETYPE_SCSI_PORT && number == 1U)
	{
		if (bytes >= 0x08U)
			state.scsi_port1_cfg[0] = get_le32(&data[0x04]);
		if (bytes >= 0x0CU)
			state.scsi_port1_cfg[1] = get_le32(&data[0x08]);
	}
	else if (type == LSI_CFG_PAGETYPE_SCSI_DEVICE && number == 1U)
	{
		const u32 target = page_address & 0xFFU;
		if (target < LSI_MAX_TARGETS)
		{
			if (bytes >= 0x08U)
				state.scsi_dev1_params[target] = get_le32(&data[0x04]);
			if (bytes >= 0x10U)
				state.scsi_dev1_cfg[target] = get_le32(&data[0x0C]);
		}
	}
}

u32 CLSI53C1020::execute_config(const u8* request, u8* reply)
{
	const u8 action = request[0x00];
	const u8 number = request[0x16];
	const u8 type = request[0x17] & 0x0FU;
	const u32 page_address = get_le32(&request[0x18]);
	const u32 sge_flags_len = get_le32(&request[0x1C]);
	const u32 sge_len = sge_flags_len & LSI_SGE_LENGTH_MASK;
	u64 sge_address = get_le32(&request[0x20]);
	u8 page[LSI_CFG_PAGE_MAX];
	u16 iocstatus = LSI_IOCSTATUS_SUCCESS;

	if ((sge_flags_len & LSI_SGE_FLAG_64BIT) != 0U)
		sge_address |= (u64)get_le32(&request[0x24]) << 32;

	reply[0x00] = action;
	reply[MPI_REPLY_MSG_LENGTH] = 6U;
	reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
	memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);
	memcpy(&reply[0x14], &request[0x14], 4U);

	const u32 page_bytes =
	    build_config_page(type, number, page_address, action == LSI_CFG_ACTION_READ_DEFAULT, page);
	if (page_bytes == 0U)
	{
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_CONFIG_INVALID_PAGE);
		return 0x18U;
	}
	memcpy(&reply[0x14], page, 4U);

	switch (action)
	{
	case LSI_CFG_ACTION_PAGE_HEADER:
		break;

	case LSI_CFG_ACTION_READ_CURRENT:
	case LSI_CFG_ACTION_READ_DEFAULT:
	case LSI_CFG_ACTION_READ_NVRAM:
		if (sge_len != 0U && !dma_write(sge_address, page, std::min<u32>(sge_len, page_bytes)))
			return 0;
		break;

	case LSI_CFG_ACTION_WRITE_CURRENT:
	case LSI_CFG_ACTION_WRITE_NVRAM:
	{
		u8 data[LSI_CFG_PAGE_MAX];
		const u32 copy = std::min<u32>(sge_len, page_bytes);
		if (copy != 0U)
		{
			memset(data, 0, sizeof(data));
			if (!dma_read(sge_address, data, copy))
				return 0;
			store_config_page(type, number, page_address, data, copy);
		}
		break;
	}

	case LSI_CFG_ACTION_PAGE_DEFAULT:
		if (type == LSI_CFG_PAGETYPE_SCSI_PORT && number == 1U)
		{
			state.scsi_port1_cfg[0] = LSI_PORT1_CONFIGURATION;
			state.scsi_port1_cfg[1] = 0U;
		}
		else if (type == LSI_CFG_PAGETYPE_SCSI_DEVICE && number == 1U)
		{
			const u32 target = page_address & 0xFFU;
			if (target < LSI_MAX_TARGETS)
			{
				state.scsi_dev1_params[target] = LSI_U320_DEV1_PARAMS;
				state.scsi_dev1_cfg[target] = 0U;
			}
		}
		break;

	default:
		iocstatus = LSI_IOCSTATUS_CONFIG_INVALID_ACTION;
		break;
	}

	put_le16(&reply[MPI_REPLY_IOC_STATUS], iocstatus);
	return 0x18U;
}

/*
 * MPI message dispatch.
 */

void CLSI53C1020::trace_message(const char* via, const u8* request, const u8* reply,
	u32 reply_bytes) const
{
	if (!trace_enabled)
		return;

	const u16 status = reply_bytes ? get_le16(&reply[MPI_REPLY_IOC_STATUS]) : 0U;
	if (request[MPI_MSG_FUNCTION] == LSI_FUNCTION_SCSI_IO_REQUEST)
	{
		char cdb[3U * 16U + 1U];
		u32 cdb_len = std::min<u32>(request[MPI_SCSI_CDB_LENGTH], 16U);
		cdb[0] = '\0';
		for (u32 i = 0; i < cdb_len; i++)
			(void)snprintf(&cdb[i * 3U], 4U, "%02x ", request[MPI_SCSI_CDB + i]);
		trace("%s SCSIIO target=%u lun=%u cdb=[%s] length=%u -> "
		      "status=0x%04x xfer=%u",
		      via, request[MPI_SCSI_TARGET_ID], request[MPI_SCSI_LUN_BYTE], cdb,
		      (unsigned)get_le32(&request[MPI_SCSI_DATA_LENGTH]), status,
		      reply_bytes ? (unsigned)get_le32(&reply[MPI_SCSI_REPLY_TRANSFER_COUNT]) : 0U);
	}
	else if (request[MPI_MSG_FUNCTION] == LSI_FUNCTION_CONFIG)
	{
		trace("%s CONFIG action=%u type=0x%02x page=%u address=0x%08x "
		      "-> status=0x%04x",
		      via, request[0], request[0x17], request[0x16], (unsigned)get_le32(&request[0x18]),
		      status);
	}
	else
	{
		trace("%s function=0x%02x context=0x%08x -> status=0x%04x", via,
		      request[MPI_MSG_FUNCTION],
		      (unsigned)get_le32(&request[MPI_MSG_CONTEXT]), status);
	}
}

u32 CLSI53C1020::execute(const u8* request, u8* reply)
{
	memset(reply, 0, LSI_FRAME_MAX);

	switch (request[MPI_MSG_FUNCTION])
	{
	case LSI_FUNCTION_IOC_FACTS:
		return execute_ioc_facts(request, reply);

	case LSI_FUNCTION_PORT_FACTS:
		return execute_port_facts(request, reply);

	case LSI_FUNCTION_IOC_INIT:
		return execute_ioc_init(request, reply);

	case LSI_FUNCTION_SCSI_TASK_MGMT:
	{
		const u8 task_type = request[0x05];
		const u8 target = request[0x00];
		if (task_type == 0x03U && target < LSI_MAX_TARGETS && get_disk(0, target))
		{
			reset_scsi_target(target);
		}
		else if (task_type == 0x04U)
		{
			reset_scsi_bus();
			/* FIFO-capable hosts expect the reset event before task completion. */
			if (state.events_enabled && state.reply_free_count != 0U)
				post_event(LSI_EVENT_IOC_BUS_RESET, 0U);
			else
				state.bus_reset_event_pending = true;
		}

		reply[0x00] = request[0x00];
		reply[0x01] = request[0x01];
		reply[MPI_REPLY_MSG_LENGTH] = 6U;
		reply[MPI_MSG_FUNCTION] = request[MPI_MSG_FUNCTION];
		reply[0x04] = 0U;
		reply[0x05] = task_type;
		memcpy(&reply[MPI_MSG_CONTEXT], &request[MPI_MSG_CONTEXT], 4U);
		put_le16(&reply[MPI_REPLY_IOC_STATUS], LSI_IOCSTATUS_SUCCESS);
		return 0x18U;
	}

	case LSI_FUNCTION_PORT_ENABLE:
		return execute_port_enable(request, reply);

	case LSI_FUNCTION_EVENT_NOTIFICATION:
		return execute_event_notification(request, reply);

	case LSI_FUNCTION_EVENT_ACK:
		return build_default_reply(request, reply, LSI_IOCSTATUS_SUCCESS);

	case LSI_FUNCTION_FW_DOWNLOAD:
		return execute_fw_download(request, reply);

	case LSI_FUNCTION_FW_UPLOAD:
		return execute_fw_upload(request, reply);

	case LSI_FUNCTION_CONFIG:
		return execute_config(request, reply);

	case LSI_FUNCTION_SCSI_IO_REQUEST:
		return execute_scsi_io(request, reply);

	default:
		return build_default_reply(request, reply, LSI_IOCSTATUS_INVALID_FUNCTION);
	}
}

void CLSI53C1020::execute_handshake()
{
	u8 request[LSI_FRAME_MAX];
	u8 reply[LSI_FRAME_MAX];
	const u32 dwords = std::min<u32>(state.hs_received_dwords, LSI_HANDSHAKE_MAX_DWORDS);

	memset(request, 0, sizeof(request));
	for (u32 i = 0; i < dwords; i++)
		put_le32(&request[i * 4U], state.hs_buffer[i]);

	const u32 reply_bytes = execute(request, reply);
	trace_message("handshake", request, reply, reply_bytes);
	if (request[MPI_MSG_FUNCTION] == LSI_FUNCTION_SCSI_TASK_MGMT &&
	    reply_bytes == sizeof(state.taskmgmt_reply_frame))
	{
		if (state.reply_free_count != 0U)
		{
			/* Deliver TaskMgmt through the stocked reply FIFO. */
			post_address_reply(reply, reply_bytes);
			state.hs_receiving = false;
			state.hs_replying = false;
			state.doorbell_interrupt = true;
			eval_interrupts();
			return;
		}
		memcpy(state.taskmgmt_reply_frame, reply, sizeof(state.taskmgmt_reply_frame));
		state.taskmgmt_mirror_pending = true;
	}
	if (reply_bytes != 0U)
		post_handshake_reply(reply, reply_bytes);
}

void CLSI53C1020::execute_request(u32 mfa)
{
	if (state.ioc_state != LSI_IOC_STATE_READY && state.ioc_state != LSI_IOC_STATE_OPERATIONAL)
		return;

	u8 request[LSI_FRAME_MAX];
	u8 reply[LSI_FRAME_MAX];
	const u64 mfa_address = ((u64)state.host_mfa_high_addr << 32) | (u64)(mfa & ~3U);
	if (!dma_read(mfa_address, request, sizeof(request)))
		return;
	const u32 reply_bytes = execute(request, reply);
	trace_message("fifo", request, reply, reply_bytes);
	if (reply_bytes != 0U)
		post_address_reply(reply, reply_bytes);
}

/*
 * Interrupt handling.
 */

void CLSI53C1020::eval_interrupts()
{
	const u32 pending = read_l_host_int_status() & ~state.host_interrupt_mask &
	                    (LSI_HIS_DOORBELL_INTERRUPT | LSI_HIS_REPLY_INTERRUPT);
	const bool asserted = pending != 0U;
	if (asserted != state.irq_asserted)
	{
		do_pci_interrupt(0, asserted);
		state.irq_asserted = asserted;
	}
}
