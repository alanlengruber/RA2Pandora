/*
 * ra_trampolim: core libretro que troca o RetroArch de fabrica pelo novo.
 *
 * O Pandory sempre lanca /usr/bin/retroarch (1.7.4 do fabricante), mas deixa
 * escolher o core de cada ROM no pandory.xml. Este "core" nao emula nada: ao
 * receber a ROM, substitui o processo (execv) pelo pandora-ra/run.sh, que abre
 * a mesma ROM no RetroArch novo com o FBNeo atual. Quando o jogo termina, o
 * processo acaba e o Pandory volta ao menu normalmente.
 *
 * O RetroArch carrega o conteudo antes de iniciar video e audio, entao nada
 * esta aberto quando fazemos o execv; mesmo assim fechamos todos os
 * descritores herdados, para o RetroArch novo encontrar tudo livre.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libretro.h"

/* Relativo a este .so (pandory/cores/): ../../pandora-ra/ */
#define RA_DIR_RELATIVE "/../../pandora-ra"

static retro_environment_t environ_cb;

/* Pasta pandora-ra, a partir do caminho deste .so. */
static bool find_ra_dir(char *out, size_t size)
{
   Dl_info info;
   char self[PATH_MAX], *slash;

   if (!dladdr((void*)find_ra_dir, &info) || !info.dli_fname)
      return false;
   snprintf(self, sizeof(self), "%s", info.dli_fname);
   slash = strrchr(self, '/');
   if (!slash)
      return false;
   *slash = '\0';
   snprintf(out, size, "%s%s", self, RA_DIR_RELATIVE);
   return true;
}

/* Le uma string terminada em NUL no offset dado de um arquivo. */
static bool read_string_at(const char *path, long offset, char *buf, size_t size)
{
   FILE *f = fopen(path, "rb");
   size_t n;

   buf[0] = '\0';
   if (!f)
      return false;
   if (fseek(f, offset, SEEK_SET) != 0)
   {
      fclose(f);
      return false;
   }
   n = fread(buf, 1, size - 1, f);
   fclose(f);
   buf[n] = '\0';
   buf[strcspn(buf, "\n\r")] = '\0';
   return buf[0] != '\0';
}

/* So abre jogos listados em pandora-ra/jogos.txt (um nome de arquivo por
 * linha). Os nomes temporarios do Pandory (7a0d.zip...) nunca estao na lista. */
static bool rom_allowed(const char *ra_dir, const char *rom)
{
   char path[PATH_MAX], line[256];
   const char *base = strrchr(rom, '/');
   bool ok = false;
   FILE *f;

   base = base ? base + 1 : rom;
   snprintf(path, sizeof(path), "%s/jogos.txt", ra_dir);
   f = fopen(path, "r");
   if (!f)
      return false;
   while (!ok && fgets(line, sizeof(line), f))
   {
      line[strcspn(line, "\n\r")] = '\0';
      ok = line[0] && strcmp(line, base) == 0;
   }
   fclose(f);
   return ok;
}

/* O RetroArch de fabrica recebe "hhhh..." no lugar da ROM; o caminho real
 * esta em /tmp/retro_tmp (offset 0x11C), preenchido pelo gamemenu/Pandory. */
#define RETRO_TMP_ROM_OFFSET 0x11C

/* Atalhos da pasta roms_pandory/conquistas: arquivos pequenos com
 * "retroachievements:<romset>.zip", que apontam para <pendrive>/roms/. */
#define ATALHO_PREFIXO "retroachievements:"

static bool follow_shortcut(const char *ra_dir, const char *path, char *out, size_t size)
{
   char buf[256];
   FILE *f = fopen(path, "rb");
   size_t n;

   if (!f)
      return false;
   n = fread(buf, 1, sizeof(buf) - 1, f);
   fclose(f);
   buf[n] = '\0';
   buf[strcspn(buf, "\n\r")] = '\0';

   if (strncmp(buf, ATALHO_PREFIXO, strlen(ATALHO_PREFIXO)) != 0)
      return false;
   /* So um nome de arquivo, nunca um caminho. */
   if (strchr(buf + strlen(ATALHO_PREFIXO), '/'))
      return false;
   snprintf(out, size, "%s/../roms/%s", ra_dir, buf + strlen(ATALHO_PREFIXO));
   return true;
}

/* Primeiro candidato que existe e esta na lista de jogos. */
static bool resolve_rom(const char *ra_dir, const char *given, char *out, size_t size)
{
   char cand[3][PATH_MAX], target[PATH_MAX];
   const char *path;
   int i;

   snprintf(cand[0], PATH_MAX, "%s", given ? given : "");
   read_string_at("/tmp/retro_tmp", RETRO_TMP_ROM_OFFSET, cand[1], PATH_MAX);
   read_string_at("/tmp/pandory_rom", 0, cand[2], PATH_MAX);

   for (i = 0; i < 3; i++)
   {
      path = cand[i];
      if (path[0] && follow_shortcut(ra_dir, path, target, sizeof(target)))
         path = target;
      if (!path[0] || !realpath(path, out) || access(out, R_OK) != 0)
         continue;
      if (rom_allowed(ra_dir, out))
         return true;
   }
   (void)size;
   return false;
}

/* Log proprio, fora de pandora-ra/logs (que os scripts de teste limpam). */
static void log_msg(const char *ra_dir, const char *fmt, ...)
{
   char path[PATH_MAX];
   va_list ap;
   FILE *f;

   snprintf(path, sizeof(path), "%s/trampolim.log", ra_dir);
   f = fopen(path, "a");
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
   if (f)
   {
      va_start(ap, fmt);
      vfprintf(f, fmt, ap);
      va_end(ap);
      fclose(f);
   }
}

static void close_inherited_fds(void)
{
   long fd, max = sysconf(_SC_OPEN_MAX);

   if (max < 0 || max > 4096)
      max = 4096;
   for (fd = 3; fd < max; fd++)
      close((int)fd);
}

bool retro_load_game(const struct retro_game_info *game)
{
   char ra_dir[PATH_MAX], run[PATH_MAX], core[PATH_MAX], rom[PATH_MAX];
   char retro_tmp[PATH_MAX];
   char *argv[5];

   if (!find_ra_dir(ra_dir, sizeof(ra_dir)))
   {
      fprintf(stderr, "[ra_trampolim] nao achei a pasta pandora-ra\n");
      return false;
   }

   read_string_at("/tmp/retro_tmp", RETRO_TMP_ROM_OFFSET, retro_tmp, sizeof(retro_tmp));
   log_msg(ra_dir, "[ra_trampolim] recebido=\"%s\" retro_tmp=\"%s\"\n",
         game && game->path ? game->path : "(nulo)", retro_tmp);

   if (!resolve_rom(ra_dir, game ? game->path : NULL, rom, sizeof(rom)))
   {
      log_msg(ra_dir, "[ra_trampolim] nenhuma ROM de jogos.txt; nada a fazer\n");
      return false;
   }

   snprintf(run, sizeof(run), "%s/run.sh", ra_dir);
   snprintf(core, sizeof(core), "%s/cores/fbneo_libretro.so", ra_dir);
   log_msg(ra_dir, "[ra_trampolim] abrindo %s\n", rom);
   fflush(stderr);

   argv[0] = "/bin/bash";
   argv[1] = run;
   argv[2] = core;
   argv[3] = rom;
   argv[4] = NULL;

   close_inherited_fds();
   execv(argv[0], argv);

   /* So chega aqui se o execv falhou. */
   perror("[ra_trampolim] execv");
   return false;
}

/* O resto da API libretro: o minimo para o RetroArch aceitar o core. */

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_environment(retro_environment_t cb)
{
   bool no_game = false;
   environ_cb = cb;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "RetroArch novo (trampolim)";
   info->library_version  = "1.0";
   info->valid_extensions = "zip|7z";
   /* So precisamos do caminho; o RetroArch nao deve ler o arquivo. */
   info->need_fullpath    = true;
   info->block_extract    = true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->geometry.base_width  = 320;
   info->geometry.base_height = 240;
   info->geometry.max_width   = 320;
   info->geometry.max_height  = 240;
   info->timing.fps           = 60.0;
   info->timing.sample_rate   = 44100.0;
}

void retro_init(void) {}
void retro_deinit(void) {}
void retro_set_video_refresh(retro_video_refresh_t cb) { (void)cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { (void)cb; }
void retro_set_input_poll(retro_input_poll_t cb) { (void)cb; }
void retro_set_input_state(retro_input_state_t cb) { (void)cb; }
void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
void retro_reset(void) {}
void retro_run(void) {}
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{ (void)type; (void)info; (void)num; return false; }
void retro_unload_game(void) {}
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
