#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "errors.h"
#include "hardware.h"
#include "fdd.h"
#include "user_io.h"
#include "config.h"
#include "boot.h"
#include "osd.h"
#include "fpga.h"
#include "tos.h"
#include "mist_cfg.h"
#include "settings.h"
#include "usb/joymapping.h"

#include "drivers/fpga.h"
#include "drivers/bitfile.h"
#include "drivers/bitstore.h"
#include "drivers/pins.h"
// #define DEBUG
#include "drivers/debug.h"

#if 0
#define BUFFER_SIZE     16 // PACKETS
#define BUFFER_SIZE_MASK  0xf
#define NR_BLOCKS(a) (((a)+511)>>9)

//#define BUFFER_FPGA

// #undef BUFFER_FPGA

typedef struct {
  FIL file;
  uint32_t size;
  uint8_t error;
#ifdef BUFFER_FPGA
  uint8_t buff[BUFFER_SIZE][512];
  uint8_t l, r, c;
//   void (*closing_fn)(void *);
//   void *closing_user;
#endif
} configFpga;

static uint8_t read_next_block(void *ud, uint8_t *data) {
  UINT br;
  configFpga *cf = (configFpga *)ud;

#ifdef BUFFER_FPGA
  debug(("read_next_block cf->c = %d cf->size = %d\n", cf->c, cf->size));
#endif
  if (f_read(&cf->file, data, 512, &br) != FR_OK) {
    cf->error = 1;
    return 0;
  }

  if (br > cf->size) {
    cf->error = 0;
    f_close(&cf->file);
    return 0;
  }
  cf->size -= br;
  return 1;
}

// #ifdef BUFFER_FPGA

// typedef struct {
//   read_lba_t rl;
//   uint8_t buff[BUFFER_SIZE][512];
//   uint8_t l, r, c;
// } buffered_read_lba_t;

#ifdef BUFFER_FPGA
#define brl_inc(a) (((a) + 1) & BUFFER_SIZE_MASK)

static void read_next_block_buffered_fill(configFpga *brl) {
  uint8_t result;

  if (!brl->size) return; // no more blocks don't try to read more.
  if (brl->error) return; // error encountered
  
  while (brl->c < BUFFER_SIZE) {
     result = read_next_block(brl, brl->buff[brl->r]);
     if (result) {
       brl->r = brl_inc(brl->r);
       brl->c ++;
     } else {
#if 0
       if (brl->closing_fn) {
         brl->closing_fn(brl->closing_user);
         brl->closing_fn = NULL;
       }
#endif
       break;
    }
  }
}

uint8_t read_next_block_buffered(void *user_data, uint8_t *block) {
  uint8_t result = 0;
  
  configFpga *brl = (configFpga *)user_data;
  debug(("read_next_block_buffered cf->c = %d\n", brl->c));
  if (brl->c) {
    memcpy(block, brl->buff[brl->l], 512);
    brl->c --;
    brl->l = brl_inc(brl->l);
    result = 1;
  }
  read_next_block_buffered_fill(brl);
  return result;
}
#endif

//MJ TODO remove
int ResetFPGA() {
  /* initialise fpga */
#ifdef ZXUNO
  return 0;
#else
  fpga_initialise();
  fpga_claim(true);

  /* now configure */
  return fpga_reset();
#endif
}

#ifdef BOOT_FLASH_ON_ERROR
void BootFromFlash() {
  debug(("BootFromFlash!\n"));
  fpga_initialise();
  fpga_claim(false);
  int r = fpga_reset();
  debug(("fpga_reset returns %d\n", r));
}
#endif

//MJ TODO remove
// static configFpga cf;
unsigned char ConfigureFpgaEx(const char *bitfile, uint8_t fatal, uint8_t reset) {
  // configFpga cf;
  uint32_t fileSize;
  configFpga *cf;

  debug(("ConfigureFpgaEx: %s\n", bitfile ? bitfile : "null"));

#ifdef XILINX // go to flash boot
  if (bitfile && !strcmp(bitfile, "ZXTRES.BIT")) {
    fpga_initialise();
    fpga_claim(false);
    fpga_reset();
    return 1;
  }
#endif

  /* now configure */
  if (reset) {
    int r = ResetFPGA();
    if (r) {
      debug(("Failed: FPGA reset returns %d\n", r));
      return 0;
    }
  }

  cf = (configFpga *)malloc(sizeof (configFpga));

#ifdef XILINX
  if (f_open(&cf->file, bitfile ? bitfile : "CORE.BIT", FA_READ) != FR_OK) {
#else
  if (f_open(&cf->file, bitfile ? bitfile : "CORE.RBF", FA_READ) != FR_OK) {
#endif
    iprintf("No FPGA configuration file found %s!\r", bitfile);
    free(cf);
#ifdef BOOT_FLASH_ON_ERROR
    printf("!!! booting from flash!!!\n");
    BootFromFlash();
    return 1;
#else
    if (fatal) FatalError(4);
    else return 0;
#endif
  }

  fileSize = cf->size = f_size(&cf->file);
  cf->error = 0;
  debug(("cf.size = %ld\n", cf->size));
  iprintf("FPGA bitstream file %s opened, file size = %ld\r", bitfile, cf->size);

  /* initialise fpga */
#ifdef ZXUNO
#else
  fpga_initialise();
//   fpga_claim(true);
#endif

#ifdef BUFFER_FPGA
#if 0
  brl->closing_fn = NULL;
  brl->closing_user = NULL;
#endif  
  cf->l = cf->r = cf->c = 0;
  read_next_block_buffered_fill(cf);

#ifdef XILINX
  // try to figure out actual bitstream size
  uint32_t bslen = bitfile_get_length(cf->buff[0], 0);
  if (bslen && bslen != 0xffffffff) {
    cf->size = bslen - (BUFFER_SIZE * 512);
    debug(("ConfigureFpga: corrected bitlen to %d\n", cf->size));
  }
#endif
#endif

  /* now configure */
  if (!reset) {
    int r = ResetFPGA();
    if (r) {
      debug(("Failed: FPGA reset returns %d\n", r));
      f_close(&cf->file);
      free(cf);
      return 0;
    }
  }



#ifdef ZXUNO
  cf->error = 0;
#else
#ifdef BUFFER_FPGA
  fpga_configure(cf, read_next_block_buffered, fileSize);
#else
  fpga_configure(cf, read_next_block, fileSize);
#endif
  fpga_claim(false);
#endif
// #endif
//   sleep_ms(2000);

//   kickSPI();
//   kickSPI();
//   kickSPI();
//   kickSPI();

  int result = !cf->error;
  free(cf);

  // returns 1 if success / 0 on fail
  return result;
}
#endif

typedef struct {
  FIL file;
  uint32_t size;
  uint32_t block_nr;
  uint32_t n_blocks;
  uint8_t error;
} configFpga;


static uint8_t read_next_block(void *ud, uint8_t *data) {
  UINT br;
  configFpga *cf = (configFpga *)ud;

  if (f_read(&cf->file, data, 512, &br) != FR_OK) {
    cf->error = 1;
    return 0;
  }

  if (br > cf->size) {
    cf->error = 0; 
    f_close(&cf->file);
    return 0;
  }
  cf->size -= br;
  // printf("cf->size = %d br = %d\n", cf->size, br);
  return br > 0;
}

uint8_t read_next_block_stdin(void *user_data, uint8_t *data) {
  configFpga *b = (configFpga *)user_data;
  if (b->block_nr >= b->n_blocks) return false;

  for (int i=0; i<512; i++) {
    data[i] = getchar();
  }

  b->block_nr ++;
  return true;
}

static int bitstore_GetBlockJTAG(void *user_data, uint8_t *blk) {
  return bitstore_GetBlock(blk) ? false : true;
}

uint32_t fpga_image_size;

static bool test_fpga_get_next_block(void *user_data, uint8_t *data) {
  int j;
  int thislen;
  int o = 0;
  configFpga *b = (configFpga *)user_data;
  

  if ((b->block_nr * 512) > fpga_image_size) {
    return false;
  }

  uint8_t *bits = (uint8_t *)(FPGA_IMAGE_POS + b->block_nr * 512);

  memcpy(data, bits, 512);
  b->block_nr ++;
  return true;
}

void ConfigureFPGAFlash()
{
  configFpga cf;
  uint32_t size, offset;
  int result = jtag_get_length((uint8_t *)FPGA_IMAGE_POS, 512, &size, &offset);

  printf("result %d size %d offset %d\n", result, size, offset);
  //result 1 size 340699 offset 95

  if (result) {
    fpga_image_size = size;
    memset(&cf, 0x00, sizeof cf);
    jtag_init();
    printf("fpga_program returns %d\n", jtag_configure(&cf, test_fpga_get_next_block, size));
  }
}

void ConfigureFPGAStdin() {
  configFpga cf;

  while (getchar_timeout_us(2) >= 0)
    ;

  printf("Size of block = ");
  int len;
  scanf("%d", &len);
  printf("%d\n", len);

  memset(&cf, 0x00, sizeof cf);
  cf.n_blocks = (len + 511) / 512;

  jtag_init();
  printf("fpga_program returns %d\n", jtag_configure(&cf, read_next_block_stdin, len));
}

unsigned char ConfigureFpga(const char *bitfile) {
  configFpga cf;
  uint32_t size;

  if (f_open(&cf.file, bitfile ? bitfile : "CORE.BIT", FA_READ) != FR_OK) {
    FatalError(4);
  }

  size = cf.size = f_size(&cf.file);
  cf.error = 0;

  /* initialise fpga */
  bitstore_InitRetrieve();
  int chunks = bitstore_Store(&cf, read_next_block);

  /* initialise jtag */
  jtag_init();

#if PICO_NO_FLASH
  ConfigureFPGAStdin();
#else
  bitstore_InitRetrieve();
  jtag_configure(NULL, bitstore_GetBlockJTAG, bitstore_Size());
#endif
  return 1;
}

int ResetFPGA() {
  return 0;
}