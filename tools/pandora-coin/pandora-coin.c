/*
 * pandora-coin: repassa as moedas do moedeiro da Pandora Box para o RetroArch.
 *
 * O emulotar (programa do fabricante) le o moedeiro por I2C e incrementa um
 * contador ASCII no primeiro byte de /tmp/pipe1. A cada incremento, este
 * programa "aperta" o Select do jogador 1 pelo Remote RetroPad do RetroArch
 * (UDP em 127.0.0.1:55400), que o FBNeo trata como moeda.
 *
 * Uso: pandora-coin [-f arquivo] [-p porta] [-w pid]
 *   -w pid  termina quando o processo pid terminar (o RetroArch)
 */
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Mesmo layout de struct remote_message (input/input_driver.h do RetroArch). */
struct remote_message
{
   int port;
   int device;
   int index;
   int id;
   uint16_t state;
};

#define RETRO_DEVICE_JOYPAD           1
#define RETRO_DEVICE_ID_JOYPAD_SELECT 2

#define POLL_MS  20
#define PRESS_MS 120

static volatile sig_atomic_t running = 1;

static void on_signal(int sig)
{
   (void)sig;
   running = 0;
}

static void sleep_ms(long ms)
{
   struct timespec ts;
   ts.tv_sec  = ms / 1000;
   ts.tv_nsec = (ms % 1000) * 1000000L;
   while (nanosleep(&ts, &ts) < 0 && errno == EINTR && running)
      ;
}

/* Le o primeiro byte do arquivo; -1 se nao existir ou estiver vazio. */
static int read_counter(const char *path)
{
   unsigned char c;
   FILE *f = fopen(path, "rb");
   int ret = -1;

   if (!f)
      return -1;
   if (fread(&c, 1, 1, f) == 1)
      ret = c;
   fclose(f);
   return ret;
}

static void send_select(int fd, const struct sockaddr_in *addr, int pressed)
{
   struct remote_message msg;

   memset(&msg, 0, sizeof(msg));
   msg.port   = 0;
   msg.device = RETRO_DEVICE_JOYPAD;
   msg.index  = 0;
   msg.id     = RETRO_DEVICE_ID_JOYPAD_SELECT;
   msg.state  = pressed ? 1 : 0;

   if (sendto(fd, &msg, sizeof(msg), 0,
            (const struct sockaddr*)addr, sizeof(*addr)) != sizeof(msg))
      perror("pandora-coin: sendto");
}

/* Vivo = existe em /proc e nao e zumbi (kill(pid, 0) aceita zumbis). */
static int process_alive(pid_t pid)
{
   char path[64], buf[256], *p;
   size_t n;
   FILE *f;

   snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
   f = fopen(path, "r");
   if (!f)
      return 0;
   n = fread(buf, 1, sizeof(buf) - 1, f);
   fclose(f);
   buf[n] = '\0';

   /* Formato: "pid (comando) estado ..."; o comando pode ter espacos. */
   p = strrchr(buf, ')');
   return !(p && p[1] == ' ' && p[2] == 'Z');
}

/* Quantas moedas entraram entre duas leituras do contador. */
static int coins_between(int before, int after)
{
   int delta = (after - before + 256) % 256;

   /* O contador so deve subir de pouco em pouco; qualquer outra
    * mudanca (reinicio do contador etc.) conta como uma moeda. */
   if (delta >= 1 && delta <= 9)
      return delta;
   return 1;
}

int main(int argc, char **argv)
{
   const char *path = "/tmp/pipe1";
   int port         = 55400;
   pid_t watch      = 0;
   int opt, fd, last;
   struct sockaddr_in addr;

   while ((opt = getopt(argc, argv, "f:p:w:")) != -1)
   {
      switch (opt)
      {
         case 'f': path  = optarg; break;
         case 'p': port  = atoi(optarg); break;
         case 'w': watch = (pid_t)atoi(optarg); break;
         default:
            fprintf(stderr, "uso: %s [-f arquivo] [-p porta] [-w pid]\n", argv[0]);
            return 2;
      }
   }

   signal(SIGINT, on_signal);
   signal(SIGTERM, on_signal);

   fd = socket(AF_INET, SOCK_DGRAM, 0);
   if (fd < 0)
   {
      perror("pandora-coin: socket");
      return 1;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sin_family      = AF_INET;
   addr.sin_port        = htons((uint16_t)port);
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

   last = read_counter(path);
   fprintf(stderr, "pandora-coin: observando %s (contador inicial %d), porta %d\n",
         path, last, port);

   while (running)
   {
      int now;

      if (watch > 0 && !process_alive(watch))
         break;

      now = read_counter(path);
      if (now >= 0 && last >= 0 && now != last)
      {
         int n = coins_between(last, now);
         fprintf(stderr, "pandora-coin: contador %d -> %d, %d moeda(s)\n", last, now, n);
         while (n-- > 0 && running)
         {
            send_select(fd, &addr, 1);
            sleep_ms(PRESS_MS);
            send_select(fd, &addr, 0);
            sleep_ms(PRESS_MS);
         }
      }
      if (now >= 0)
         last = now;

      sleep_ms(POLL_MS);
   }

   /* Nunca deixa o Select preso. */
   send_select(fd, &addr, 0);
   close(fd);
   return 0;
}
