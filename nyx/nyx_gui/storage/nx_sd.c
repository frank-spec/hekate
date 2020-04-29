/*
 * Copyright (c) 2018 naehrwert
 * Copyright (c) 2018-2019 CTCaer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "nx_sd.h"
#include "sdmmc.h"
#include "sdmmc_driver.h"
#include "../gfx/gfx.h"
#include "../libs/fatfs/ff.h"
#include "../mem/heap.h"

static bool sd_mounted = false;
static bool sd_init_done = false;

bool sd_get_card_removed()
{
	if (sd_init_done && !sdmmc_get_sd_inserted())
		return true;

	return false;
}

u32 sd_mode_get()
{
	return sd_mode;
}

int sd_init_retry(bool power_cycle)
{
	u32 bus_width = SDMMC_BUS_WIDTH_4;
	u32 type = SDHCI_TIMING_UHS_SDR104;

	// Power cycle SD card.
	if (power_cycle)
	{
		sd_mode--;
		sdmmc_storage_end(&sd_storage);
	}

	// Get init parameters.
	switch (sd_mode)
	{
	case SD_INIT_FAIL: // Reset to max.
		return 0;
	case SD_1BIT_HS25:
		bus_width = SDMMC_BUS_WIDTH_1;
		type = SDHCI_TIMING_SD_HS25;
		break;
	case SD_4BIT_HS25:
		type = SDHCI_TIMING_SD_HS25;
		break;
	case SD_UHS_SDR82:
		type = SDHCI_TIMING_UHS_SDR82;
		break;
	case SD_UHS_SDR104:
		type = SDHCI_TIMING_UHS_SDR104;
		break;
	default:
		sd_mode = SD_UHS_SDR104;
	}

	return sdmmc_storage_init_sd(&sd_storage, &sd_sdmmc, bus_width, type);
}

bool sd_initialize(bool power_cycle)
{
	if (power_cycle)
		sdmmc_storage_end(&sd_storage);

	int res = !sd_init_retry(false);

	while (true)
	{
		if (!res)
			return true;
		else if (!sdmmc_get_sd_inserted()) // SD Card is not inserted.
		{
			sd_mode = SD_UHS_SDR104;
			break;
		}
		else
		{
			if (sd_mode == SD_INIT_FAIL)
				break;
			else
				res = !sd_init_retry(true);
		}
	}

	sdmmc_storage_end(&sd_storage);

	return false;
}

bool sd_mount()
{
	if (sd_mounted)
		return true;

	int res = 0;

	if (!sd_init_done)
		res = !sd_initialize(false);

	if (res)
	{
		EPRINTF("Failed to init SD card.");
		if (!sdmmc_get_sd_inserted())
			EPRINTF("Make sure that it is inserted.");
		else
			EPRINTF("SD Card Reader is not properly seated!");
	}
	else
	{
		sd_init_done = true;
		res = f_mount(&sd_fs, "", 1);
		if (res == FR_OK)
		{
			sd_mounted = true;
			return true;
		}
		else
		{
			EPRINTFARGS("Failed to mount SD card (FatFS Error %d).\nMake sure that a FAT partition exists..", res);
		}
	}

	return false;
}

void sd_unmount(bool deinit)
{
	if (sd_mode == SD_INIT_FAIL)
		sd_mode = SD_UHS_SDR104;

	if (sd_init_done && sd_mounted)
	{
		f_mount(NULL, "", 1);
		sd_mounted = false;
	}
	if (sd_init_done && deinit)
	{
		sdmmc_storage_end(&sd_storage);
		sd_init_done = false;
	}
}

void *sd_file_read(const char *path, u32 *fsize)
{
	FIL fp;
	if (f_open(&fp, path, FA_READ) != FR_OK)
		return NULL;

	u32 size = f_size(&fp);
	if (fsize)
		*fsize = size;

	void *buf = malloc(size);

	if (f_read(&fp, buf, size, NULL) != FR_OK)
	{
		free(buf);
		f_close(&fp);

		return NULL;
	}

	f_close(&fp);

	return buf;
}

int sd_save_to_file(void *buf, u32 size, const char *filename)
{
	FIL fp;
	u32 res = 0;
	res = f_open(&fp, filename, FA_CREATE_ALWAYS | FA_WRITE);
	if (res)
	{
		EPRINTFARGS("Error (%d) creating file\n%s.\n", res, filename);
		return res;
	}

	f_write(&fp, buf, size, NULL);
	f_close(&fp);

	return 0;
}