#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kssplay.h"

#define HLPMSG                                                                                                         \
  "Usage: kss2vgm [Options] kssfile \n"                                                                                \
  "Options: \n"                                                                                                        \
  "  -p<play_time>  Specify song length in second to convert.\n"                                                       \
  "  -s<song_num>   Internal song number to play.\n"                                                                   \
  "  -o<file>       Specify the output filename.\n"

#define MAX_PATH 512

static uint32_t opll_adr = 0, psg_adr = 0, opl_adr = 0;
static uint32_t total_samples = 0;
static uint32_t data_size = 0;
static uint32_t last_write_clock = 0;
static int use_sng = 0, use_opll = 0, use_psg = 0, use_scc = 0, use_scc_plus = 0, use_opl = 0;

static void WORD(uint8_t *buf, uint32_t data) {
  buf[0] = data & 0xff;
  buf[1] = (data & 0xff00) >> 8;
}

static void DWORD(uint8_t *buf, uint32_t data) {
  buf[0] = data & 0xff;
  buf[1] = (data & 0xff00) >> 8;
  buf[2] = (data & 0xff0000) >> 16;
  buf[3] = (data & 0xff000000) >> 24;
}

static void create_vgm_header(uint8_t *buf, uint32_t header_size, uint32_t data_size, uint32_t total_samples) {

  uint8_t opl_data_offset = 0x00;
  memset(buf, 0, header_size);
  memcpy(buf, "Vgm ", 4);

  DWORD(buf + 0x04, header_size + data_size - 4);
  DWORD(buf + 0x08, 0x00000170);
  DWORD(buf + 0x0C, use_sng ? 3579545 : 0);  // SN76489
  DWORD(buf + 0x10, use_opll ? 3579545 : 0); // YM2413
  DWORD(buf + 0x14, 0);                      // GD3 offset
  DWORD(buf + 0x18, total_samples);
  DWORD(buf + 0x1C, 0);  // loop_offset
  DWORD(buf + 0x20, 0);  // loop_samples
  DWORD(buf + 0x24, 60); // PAL or NTSC

  WORD(buf + 0x28, 0x0009); // SN76489 feedback
  WORD(buf + 0x2A, 16);     // SN76489 shift register width
  WORD(buf + 0x2B, 0);      // SN76489 flags

  if (use_opl && is_adpcm_used()) opl_data_offset = 0x0f;

  DWORD(buf + 0x34, header_size - opl_data_offset - 0x34);    // VGM data offset
  DWORD(buf + 0x58, use_opl ? 3579545 : 0); // Y8950
  DWORD(buf + 0x74, use_psg ? 1789773 : 0); // AY8910
  buf[0x78] = 0x00;                         // AY8910 chiptype
  buf[0x79] = 0x00;
  DWORD(buf + 0x9C, use_scc_plus ? (0x80000000|1789773) : (use_scc ? 1789773 : 0) ); // SCC

  DWORD(buf + 0x100, 0x08886667);
  DWORD(buf + 0x104, 0x00000000);
  DWORD(buf + 0x108, 0x80);
  DWORD(buf + 0x109, 0x00000000);
  DWORD(buf + 0x10d, 0x0000);
}

typedef struct {
  int song_num;
  int play_time;
  int loop_num;
  char input[MAX_PATH + 4];
  char output[MAX_PATH + 4];
  int help;
  int error;
} Options;

static Options parse_options(int argc, char **argv) {

  Options options;
  int i, j;

  options.song_num = 0;
  options.play_time = 60;
  options.loop_num = 1;
  options.input[0] = '\0';
  options.output[0] = '\0';
  options.help = 0;
  options.error = 0;

  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1]) {
      case 'p':
        options.play_time = atoi(argv[i] + 2);
        break;
      case 's':
        options.song_num = atoi(argv[i] + 2);
        break;
      case 'l':
        options.loop_num = atoi(argv[i] + 2);
        if (options.loop_num == 0) {
          options.loop_num = 256;
        }
        break;
      case 'o':
        for (j = 0; argv[i][j + 2]; j++) {
          options.output[j] = argv[i][j + 2];
        }
        options.output[j] = '\0';
        break;
      default:
        options.error = 1;
        break;
      }
    } else {
      strncpy(options.input, argv[i], MAX_PATH);
    }
  }

  if (options.output[0] == '\0') {
    for (i = 0; options.input[i] != '\0'; i++) {
      if (options.input[i] == '.') {
        break;
      }
      options.output[i] = options.input[i];
    }
    options.output[i] = '\0';
    strcat(options.output, ".vgm");
  }

  return options;
}

static void write_command(FILE *fp, uint8_t *buf, uint32_t len) {

    uint32_t d = (total_samples - last_write_clock);
	if(0 < d) {
    	fputc(0x61, fp);
        fputc((d & 0xff), fp);
        fputc((d >> 8) & 0xff, fp);
        last_write_clock = total_samples;
        data_size += 3;
    }
  	fwrite(buf, len, 1, fp);
  	data_size += len;
}

static uint8_t cmd_buf[8];

static void write_eos_command(FILE *fp) {
  cmd_buf[0] = 0x66;
  write_command(fp, cmd_buf, 1);
}

static void iowrite_handler(void *context, uint32_t a, uint32_t d) {

  FILE *fp = (FILE *)context;

  if (a == 0x7c || a == 0xf0) { // YM2413(A)
    use_opll = 1;
    opll_adr = d;
  } else if (a == 0x7d || a == 0xf1) { // YM2413(D)
    cmd_buf[0] = 0x51;
    cmd_buf[1] = opll_adr;
    cmd_buf[2] = d;
    write_command(fp, cmd_buf, 3);
  } else if (a == 0xC0) {
    use_opl = 1;
    opl_adr = d;
  } else if (a == 0xC1) {
    cmd_buf[0] = 0x5C;
    cmd_buf[1] = opl_adr;
    cmd_buf[2] = d;
    write_command(fp, cmd_buf, 3);
  } else if (a == 0xa0) { // PSG(A)
    use_psg = 1;
    psg_adr = d;
  } else if (a == 0xa1) { // PSG(D)
    cmd_buf[0] = 0xA0;
    cmd_buf[1] = psg_adr;
    cmd_buf[2] = d;
    write_command(fp, cmd_buf, 3);
  } else if (a == 0x7E || a == 0x7F) { // SN76489
    use_sng = 1;
    cmd_buf[0] = 0x50;
    cmd_buf[1] = d;
    write_command(fp, cmd_buf, 2);
  } else if (a == 0x06) { // GG Stereo
    cmd_buf[0] = 0x4F;
    cmd_buf[1] = d;
    write_command(fp, cmd_buf, 2);
  }
}

static void scc_handler(FILE *fp, uint32_t a, uint32_t d) {

  int port = 0, offset = 0;
  a = a & 0xFF;
  if (a <= 0x7F) {
    port = 0; // wave
    offset = a;
  } else if (a <= 0x89) {
    port = 1; // freq
    offset = a - 0x80;
  } else if (a <= 0x8E) {
    port = 2; // volume
    offset = a - 0x8A;
  } else if (a == 0x8F) {
    port = 3; // enable
    offset = 0;
  } else if (a == 0x90 && a <= 0x99) {
    port = 1; // freq
    offset = a - 0x90;
  } else if (a == 0x9A && a <= 0x9E) {
    port = 2; // volume
    offset = a - 0x9A;
  } else if (a == 0x9F) {
    port = 3; // enable
    offset = 0;
  } else if (a <= 0xDF) {
    port = -1;
    offset = 0;
  } else if (a <= 0xFF) {
    port = 5;
    offset = a - 0xe0;
  }

  if (0 <= port) {
    use_scc = 1;
    cmd_buf[0] = 0xD2;
    cmd_buf[1] = port;
    cmd_buf[2] = offset;
    cmd_buf[3] = d;
    write_command(fp, cmd_buf, 4);
  }
}

static void scc_plus_handler(FILE *fp, uint32_t a, uint32_t d) {
  int port = 0, offset = 0;
  a = a & 0xFF;
  if (a <= 0x7F) {
    port = 0; // wave[0-4]
    offset = a;
  } else if (a <= 0x9F) {
    port = 4; // wave[5]
    offset = a;
  } else if (a <= 0xA9) {
    port = 1; // freq
    offset = a - 0xA0;
  } else if (0xAA <= a && a <= 0xAE) {
    port = 2; // volume
    offset = a - 0xAA;
  } else if (a == 0xAF) {
    port = 3; // enable
    offset = 0;
  } else if (a == 0xB0 && a <= 0xB9) {
    port = 1; // freq
    offset = a - 0xB0;
  } else if (a == 0xBA && a <= 0xBE) {
    port = 2; // volume
    offset = a - 0xBA;
  } else if (a == 0xBF) {
    port = 3; // enable
    offset = 0;
  } else if (a <= 0xDF) {
    port = 5;
    offset = a - 0xC0;
  } else {
    port = -1;
    offset = 0;
  }

  if (0 <= port) {
    use_scc_plus = 1;
    cmd_buf[0] = 0xD2;
    cmd_buf[1] = port;
    cmd_buf[2] = offset;
    cmd_buf[3] = d;
    write_command(fp, cmd_buf, 4);
  }
}

static void memwrite_handler(void *context, uint32_t a, uint32_t d) {
  if (0x9800 <= a && a <= 0x98FF) {
    scc_handler((FILE *)context, a, d);
  } else if (0xB800 <= a && a <= 0xB8FF) {
    scc_plus_handler((FILE *)context, a, d);
  }
}

int main(int argc, char **argv) {

  int i, t;
  uint8_t header[0x10f];
  FILE *fp;

  if (argc < 2) {
    fprintf(stderr, HLPMSG);
    exit(0);
  }

  Options opt = parse_options(argc, argv);

  if (opt.error) {
    fprintf(stderr, HLPMSG);
    exit(1);
  }

  KSS *kss = KSS_load_file(opt.input);

  if ((kss = KSS_load_file(opt.input)) == NULL) {
    fprintf(stderr, "FILE READ ERROR!\n");
    exit(1);
  }

  /* Open output VGM file */
  if ((fp = fopen(opt.output, "wb")) == NULL) {
    fprintf(stderr, "Can't open %s\n", opt.output);
    exit(1);
  }

  fseek(fp, sizeof(header), SEEK_SET); /* SKIP HEADER SIZE */

  KSSPLAY *kssplay = KSSPLAY_new(44100, 1, 16);
  KSSPLAY_set_iowrite_handler(kssplay, fp, iowrite_handler);
  KSSPLAY_set_memwrite_handler(kssplay, fp, memwrite_handler);
  KSSPLAY_set_data(kssplay, kss);
  KSSPLAY_reset(kssplay, opt.song_num, 0);

  for (t = 0; t < opt.play_time; t++) {
    for (i = 0; i < 44100; i++) {
      KSSPLAY_calc_silent(kssplay,1);
      total_samples++;
    }
  }

  KSSPLAY_delete(kssplay);
  KSS_delete(kss);
  write_eos_command(fp);

  create_vgm_header(header, sizeof(header), data_size, total_samples);
  fseek(fp, 0, SEEK_SET);
  fwrite(header, sizeof(header), 1, fp);
  fclose(fp);

  return 0;
}
