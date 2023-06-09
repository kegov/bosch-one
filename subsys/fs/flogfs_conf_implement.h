/*
Copyright (c) 2013, Ben Nahill <bnahill@gmail.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of the FLogFS Project.
*/

/*!
 * @file flogfs_conf_implement.h
 * @author Ben Nahill <bnahill@gmail.com>
 * @ingroup FLogFS
 *
 * @brief Platform-specific interface details
 */
#include <stdlib.h>

#include "flogfs.h"

/*for Zephyr specific operations*/
#include <zephyr/kernel.h> 
#include <drivers/flash_nand.h>

flash_nand_config_t *nand_cfg=NULL;

typedef uint8_t flash_spare_t[59];

static flash_spare_t flog_spare_buffer;
static uint16_t flash_block;
static uint16_t flash_page;
static uint8_t flash_sector = 0;
static uint8_t have_metadata;
static uint8_t page_open;


/* this mutex is used in fs_lock and fs_unlock functions prior 
   to each file in FLogFS code	operation.*/

typedef struct k_mutex fs_lock_t;

#define nullptr NULL

static inline void fs_lock_initialize(fs_lock_t * lock){
	k_mutex_init(lock);
}

static inline void fs_lock(fs_lock_t * lock){
	k_mutex_lock(lock, K_FOREVER);
}

static inline void fs_unlock(fs_lock_t * lock){
	k_mutex_unlock(lock);
}

/*fs_lock and flash_lock seem to be used as parallel mechanisms in the flogfs code
It seems that only one needs to be implemented, as implementing iwould be an overkill
TODO: to be verified*/
static inline void flash_lock(void){
}

static inline void flash_unlock(void){
}

static inline flog_result_t flash_initialize(void){
	if( nand_cfg == NULL || nand_cfg->ext_api.init()!= 0){
		return FLOG_FAILURE;
	}
	return FLOG_SUCCESS;
}


static inline flog_result_t flash_open_page(uint16_t block, uint16_t page){
	flash_block = block;
	flash_page = page;
	have_metadata = 0;
	return FLOG_SUCCESS;
}

static inline void flash_close_page(void){
	page_open = 0;
}

static inline flog_result_t flash_erase_block(uint16_t block){
	page_open = 0;
	if( nand_cfg == NULL || 
		nand_cfg->ext_api.erase_block((block*FS_BLOCK_SIZE)+1,
										FS_BLOCK_SIZE)!= 0){
		return FLOG_FAILURE;
	}
	return FLOG_SUCCESS;
}

static inline flog_result_t flash_get_spares(void){
	return FLOG_SUCCESS;
}

static inline uint8_t * flash_spare(uint8_t sector){
	return &flog_spare_buffer[sector * 16 + 4];
}

static inline flog_result_t flash_block_is_bad(void){
	return FLOG_RESULT(0);
}

static inline void flash_set_bad_block(void){
}

/*!
 @brief Commit the changes to the active page
 */
static inline flog_result_t flash_commit(void){
	page_open = 0;
	if( nand_cfg == NULL || 
		nand_cfg->ext_api.write_sector_with_spare(
				(flash_block*FS_PAGES_PER_BLOCK)+flash_page,flash_sector)!=0){
		return FLOG_FAILURE;
	}
	return FLOG_SUCCESS;
}

/*!
 @brief Read data from the flash cache (current page only)
 @param dst The destination buffer to fill
 @param chunk_in_page The chunk index within the current page
 @param offset The offset data to retrieve
 @param n The number of bytes to transfer
 @return The success or failure of the operation
 */
static inline flog_result_t flash_read_sector(uint8_t * dst, uint8_t sector, uint16_t offset, uint16_t n){
	sector = sector%FS_SECTORS_PER_PAGE;
	uint32_t read_loc =(flash_block*FS_SECTOR_SIZE*FS_PAGES_PER_BLOCK*FS_SECTORS_PER_PAGE)+
	( flash_page*FS_SECTORS_PER_PAGE*FS_SECTOR_SIZE )+( FS_SECTOR_SIZE * sector) + offset;
	if( nand_cfg == NULL || 
		nand_cfg->ext_api.read(dst,n,read_loc)!=0){
		return FLOG_FAILURE;
	}
	return FLOG_SUCCESS;
}

static inline flog_result_t flash_read_spare(uint8_t * dst, uint8_t sector){
	sector = sector%FS_SECTORS_PER_PAGE;
	if( nand_cfg == NULL || 
		nand_cfg->ext_api.read_spare(dst,4,flash_page+(flash_block*64), 0x804+sector * 0x10)!=0){
		return FLOG_FAILURE;
	}
	return FLOG_SUCCESS;
}

/*!
 @brief Write chunk data to the flash cache
 @param src A pointer to the data to transfer
 @param chunk_in_page The chunk index within the current page
 @param offset The offset to write the data
 @param n The number of bytes to write
 */
static inline flog_result_t flash_write_sector(uint8_t const * src, uint8_t sector, uint16_t offset, uint16_t n){
	 flash_sector = sector%FS_SECTORS_PER_PAGE;
    uint32_t write_loc =(flash_block*FS_SECTOR_SIZE*FS_PAGES_PER_BLOCK*FS_SECTORS_PER_PAGE)
	+( flash_page*FS_SECTORS_PER_PAGE*FS_SECTOR_SIZE )+( FS_SECTOR_SIZE * flash_sector) + offset;
	if( nand_cfg == NULL || 
		nand_cfg->ext_api.load_sector(src,n, write_loc)!=0){
		return FLOG_FAILURE;
	}
	return FLOG_SUCCESS;
}


/*!
 @brief Write the spare data for a sector
 @param chunk_in_page The chunk index within the current page

 @note This doesn't commit the transaction
 */
static inline flog_result_t flash_write_spare(uint8_t const * src, uint8_t sector){

    flash_sector = sector%FS_SECTORS_PER_PAGE;
	if( nand_cfg == NULL || 
		nand_cfg->ext_api.load_sector_spare(src,4,(flash_block*FS_PAGES_PER_BLOCK)+flash_page)!=0){
		return FLOG_FAILURE;
	}
	return FLOG_SUCCESS;
}

void flash_debug_warn(char const *f, ...){
}

void flash_debug_error(char const *f, ...){
}

void flash_debug_panic(void) {
}

void flash_high_level(int hle) {
}

uint32_t flash_random(uint32_t max) {
    return rand()%1024;
}

