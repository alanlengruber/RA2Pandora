/*
 * pandora-relay: lado da placa do relay de conquistas (ver PROTOCOLO.md).
 *
 * Escuta HTTP em 127.0.0.1:<porta> e repassa cada requisicao do RetroArch,
 * inteira, por um cabo serial ate o Raspberry Pi, que faz a chamada real ao
 * RetroAchievements e devolve a resposta.
 *
 * A placa nao tem driver para adaptadores seriais USB, entao o CP2102 e
 * controlado direto pelo usbfs (/dev/bus/usb). Para testes, -t usa um tty.
 *
 * Uso: pandora-relay [-t /dev/ttyX | -u] [-b baud] [-p porta] [--ping]
 *   -u        CP2102 pelo usbfs (padrao)
 *   -t tty    dispositivo serial comum (testes no PC)
 *   --ping    so testa o cabo: manda PING, espera PONG e sai (0 = ok)
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define FRAME_PEDIDO   1
#define FRAME_RESPOSTA 2
#define FRAME_PING     3
#define FRAME_PONG     4
#define FRAME_PEDIDO_PEDACOS 5
#define FRAME_TAMANHO  6
#define FRAME_LER      7
#define FRAME_PEDACO   8

#define HEADER_SIZE    10
#define MAX_PAYLOAD    (4u * 1024 * 1024)
#define MAX_REQUEST    (1u * 1024 * 1024)
#define RESPONSE_WAIT_MS 30000
/* Sem resposta nesse prazo, o pedido e reenviado (quadro corrompido no cabo). */
#define RETRY_MS         8000
/* Respostas grandes vem em pedacos, um por vez, pedidos pela placa. */
#define PEDACO_MAX       1024
#define PEDACO_MIN       128
#define PEDACO_WAIT_MS   400
/* Pedaco que comecou a chegar e ficou esse tempo sem bytes novos: perdido. */
#define PEDACO_GAP_MS    60
/* Sem nenhum pedaco novo nesse prazo, desiste da resposta. */
#define STALL_MS         15000
#define OFFSET_DESCONHECIDO 0xFFFFFFFFu

#define CP2102_VID     0x10c4
#define CP2102_PID     0xea60

static void logmsg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#include <stdarg.h>
static void logmsg(const char *fmt, ...)
{
   va_list ap;
   fprintf(stderr, "pandora-relay: ");
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
   fputc('\n', stderr);
}

static long long now_ms(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* --- CRC-32 (o mesmo do zlib) ---------------------------------------- */

static uint32_t crc_table[256];

static void crc_init(void)
{
   uint32_t i, j, c;
   for (i = 0; i < 256; i++)
   {
      c = i;
      for (j = 0; j < 8; j++)
         c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      crc_table[i] = c;
   }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
   crc = ~crc;
   while (n--)
      crc = crc_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
   return ~crc;
}

/* --- Enlace serial: tty ou CP2102 pelo usbfs ------------------------- */

/* Leituras USB sempre na fila: o CP2102 so guarda 576 bytes recebidos e, a
 * 921600 baud, enche em ~6 ms. Com uma leitura sincrona por vez, o intervalo
 * entre elas perde dados em respostas grandes (os 21 KB de conquistas de um
 * jogo, por exemplo); com varias leituras enfileiradas, nunca ha intervalo.
 * O CP2102 devolve o que tiver a cada consulta, entao uma leitura pode vir
 * com poucos bytes: a fila e longa para aguentar o relay ficar sem CPU. */
#define IN_URBS     32
#define IN_URB_SIZE 512

/* Contadores do pedido em andamento, para o log. */
static struct
{
   unsigned long leituras, bytes_lidos, lixo;
   int tentativas, pedacos, pedacos_repetidos;
} st;

struct link
{
   int fd;
   int is_usb;
   unsigned char ep_in, ep_out;
   struct usbdevfs_urb urbs[IN_URBS];
   uint8_t in_buf[IN_URBS][IN_URB_SIZE];
};

static speed_t baud_constant(int baud)
{
   switch (baud)
   {
      case 115200: return B115200;
      case 230400: return B230400;
      case 460800: return B460800;
      case 921600: return B921600;
      default:     return 0;
   }
}

static int link_open_tty(struct link *l, const char *path, int baud)
{
   struct termios t;
   speed_t sp = baud_constant(baud);

   if (!sp)
   {
      logmsg("baud nao suportado no tty: %d", baud);
      return -1;
   }
   l->fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
   if (l->fd < 0)
   {
      logmsg("nao abri %s: %s", path, strerror(errno));
      return -1;
   }
   tcgetattr(l->fd, &t);
   cfmakeraw(&t);
   cfsetispeed(&t, sp);
   cfsetospeed(&t, sp);
   t.c_cflag |= CLOCAL | CREAD;
   t.c_cflag &= ~CRTSCTS;
   tcsetattr(l->fd, TCSANOW, &t);
   tcflush(l->fd, TCIOFLUSH);
   l->is_usb = 0;
   return 0;
}

/* Acha o CP2102 em /sys/bus/usb/devices e devolve /dev/bus/usb/BBB/DDD. */
static int find_cp2102(char *out, size_t size)
{
   DIR *d = opendir("/sys/bus/usb/devices");
   struct dirent *e;
   int found = 0;

   if (!d)
      return 0;
   while (!found && (e = readdir(d)))
   {
      char p[512];
      unsigned vid = 0, pid = 0, bus = 0, dev = 0;
      FILE *f;

      if (e->d_name[0] == '.')
         continue;
#define READ_HEX(name, var) \
      snprintf(p, sizeof(p), "/sys/bus/usb/devices/%s/" name, e->d_name); \
      if ((f = fopen(p, "r"))) { if (fscanf(f, "%x", &var) != 1) var = 0; fclose(f); }
#define READ_DEC(name, var) \
      snprintf(p, sizeof(p), "/sys/bus/usb/devices/%s/" name, e->d_name); \
      if ((f = fopen(p, "r"))) { if (fscanf(f, "%u", &var) != 1) var = 0; fclose(f); }
      READ_HEX("idVendor", vid);
      READ_HEX("idProduct", pid);
      if (vid != CP2102_VID || pid != CP2102_PID)
         continue;
      READ_DEC("busnum", bus);
      READ_DEC("devnum", dev);
      snprintf(out, size, "/dev/bus/usb/%03u/%03u", bus, dev);
      found = 1;
   }
   closedir(d);
   return found;
}

static int cp210x_ctrl(struct link *l, uint8_t req, uint16_t value, void *data, uint16_t len)
{
   struct usbdevfs_ctrltransfer c;

   memset(&c, 0, sizeof(c));
   c.bRequestType = 0x41; /* vendor, host -> interface */
   c.bRequest     = req;
   c.wValue       = value;
   c.wIndex       = 0;
   c.wLength      = len;
   c.timeout      = 1000;
   c.data         = data;
   return ioctl(l->fd, USBDEVFS_CONTROL, &c);
}

static int link_open_cp2102(struct link *l, int baud)
{
   char path[64];
   uint8_t desc[1024];
   ssize_t n, i;
   /* i tambem conta as leituras enfileiradas, mais abaixo. */
   uint32_t b = (uint32_t)baud;
   unsigned int iface = 0;

   if (!find_cp2102(path, sizeof(path)))
   {
      logmsg("CP2102 (10c4:ea60) nao encontrado no USB");
      return -1;
   }
   l->fd = open(path, O_RDWR);
   if (l->fd < 0)
   {
      logmsg("nao abri %s: %s", path, strerror(errno));
      return -1;
   }

   /* Ler o arquivo do usbfs devolve os descritores; pegamos os endpoints
    * bulk da interface 0. */
   l->ep_in = l->ep_out = 0;
   n = read(l->fd, desc, sizeof(desc));
   for (i = 0; i + 1 < n && desc[i] > 0; i += desc[i])
   {
      if (desc[i + 1] == 5 && desc[i] >= 7 && (desc[i + 3] & 3) == 2)
      {
         if (desc[i + 2] & 0x80)
            l->ep_in = desc[i + 2];
         else
            l->ep_out = desc[i + 2];
      }
   }
   if (!l->ep_in || !l->ep_out)
   {
      logmsg("endpoints bulk do CP2102 nao encontrados");
      close(l->fd);
      return -1;
   }
   if (ioctl(l->fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0)
   {
      logmsg("nao consegui usar a interface do CP2102: %s", strerror(errno));
      close(l->fd);
      return -1;
   }

   /* Liga a UART, define 8N1 e a velocidade, e limpa os buffers. */
   if (cp210x_ctrl(l, 0x00, 0x0001, NULL, 0) < 0      /* IFC_ENABLE   */
       || cp210x_ctrl(l, 0x03, 0x0800, NULL, 0) < 0   /* SET_LINE_CTL */
       || cp210x_ctrl(l, 0x1E, 0, &b, 4) < 0          /* SET_BAUDRATE */
       || cp210x_ctrl(l, 0x12, 0x000F, NULL, 0) < 0)  /* PURGE        */
   {
      logmsg("falha configurando o CP2102: %s", strerror(errno));
      close(l->fd);
      return -1;
   }

   /* Enfileira as leituras (ver IN_URBS). */
   for (i = 0; i < IN_URBS; i++)
   {
      struct usbdevfs_urb *u = &l->urbs[i];

      memset(u, 0, sizeof(*u));
      u->type          = USBDEVFS_URB_TYPE_BULK;
      u->endpoint      = l->ep_in;
      u->buffer        = l->in_buf[i];
      u->buffer_length = IN_URB_SIZE;
      if (ioctl(l->fd, USBDEVFS_SUBMITURB, u) < 0)
      {
         logmsg("falha enfileirando leituras do CP2102: %s", strerror(errno));
         close(l->fd);
         return -1;
      }
   }
   l->is_usb = 1;
   logmsg("CP2102 em %s (in 0x%02x, out 0x%02x), %d baud", path, l->ep_in, l->ep_out, baud);
   return 0;
}

static void link_close(struct link *l)
{
   if (l->fd >= 0)
      close(l->fd);
   l->fd = -1;
}

/* Le ate max bytes; 0 = nada no prazo; -1 = erro. */
static ssize_t link_read(struct link *l, uint8_t *buf, size_t max, int timeout_ms)
{
   if (l->is_usb)
   {
      size_t n = 0;
      int waited = 0;

      for (;;)
      {
         struct usbdevfs_urb *u = NULL;
         struct pollfd p;

         /* Colhe as leituras concluidas e devolve cada uma a fila. */
         while (n + IN_URB_SIZE <= max
               && ioctl(l->fd, USBDEVFS_REAPURBNDELAY, &u) == 0)
         {
            if (u->status == 0 && u->actual_length > 0)
            {
               memcpy(buf + n, u->buffer, u->actual_length);
               n += u->actual_length;
               st.leituras++;
               st.bytes_lidos += u->actual_length;
            }
            else if (u->status == -ENODEV || u->status == -ESHUTDOWN)
               return n ? (ssize_t)n : -1; /* CP2102 desconectado */
            u->status        = 0;
            u->actual_length = 0;
            if (ioctl(l->fd, USBDEVFS_SUBMITURB, u) < 0)
               return n ? (ssize_t)n : -1;
         }
         if (n > 0)
            return (ssize_t)n;
         if (waited)
            return 0;

         /* O usbfs sinaliza POLLOUT quando ha leitura concluida. */
         p.fd      = l->fd;
         p.events  = POLLOUT;
         p.revents = 0;
         if (poll(&p, 1, timeout_ms) <= 0)
            return 0;
         if (p.revents & (POLLERR | POLLHUP))
            return -1;
         waited = 1;
      }
   }
   else
   {
      struct pollfd p = { l->fd, POLLIN, 0 };
      ssize_t r;

      if (poll(&p, 1, timeout_ms) <= 0)
         return 0;
      r = read(l->fd, buf, max);
      if (r < 0)
         return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
      st.leituras++;
      st.bytes_lidos += (unsigned long)r;
      return r;
   }
}

static int link_write(struct link *l, const uint8_t *buf, size_t len)
{
   while (len > 0)
   {
      ssize_t w;

      if (l->is_usb)
      {
         struct usbdevfs_bulktransfer bt;
         bt.ep      = l->ep_out;
         bt.len     = len > 4096 ? 4096 : (unsigned)len;
         bt.timeout = 2000;
         bt.data    = (void*)buf;
         w = ioctl(l->fd, USBDEVFS_BULK, &bt);
      }
      else
      {
         struct pollfd p = { l->fd, POLLOUT, 0 };
         poll(&p, 1, 2000);
         w = write(l->fd, buf, len);
         if (w < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
      }
      if (w <= 0)
         return -1;
      buf += w;
      len -= (size_t)w;
   }
   return 0;
}

/* --- Quadros ---------------------------------------------------------- */

static uint8_t *rx;
static size_t rx_len, rx_cap;

static void put_u16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put_u32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint16_t get_u16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t get_u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int send_frame(struct link *l, uint8_t type, uint16_t id, const uint8_t *data, uint32_t len)
{
   uint8_t hdr[HEADER_SIZE], tail[4];
   uint32_t crc;

   hdr[0] = 'R'; hdr[1] = 'A'; hdr[2] = 1; hdr[3] = type;
   put_u16(hdr + 4, id);
   put_u32(hdr + 6, len);
   crc = crc32_update(0, hdr, HEADER_SIZE);
   crc = crc32_update(crc, data, len);
   put_u32(tail, crc);
   if (link_write(l, hdr, HEADER_SIZE) < 0 || (len && link_write(l, data, len) < 0)
         || link_write(l, tail, 4) < 0)
      return -1;
   return 0;
}

/* Quadro em rx[pos]: 1 = completo e valido, 0 = incompleto, -1 = invalido. */
static int frame_at(size_t pos, uint32_t *total)
{
   const uint8_t *f = rx + pos;
   size_t avail = rx_len - pos;
   uint32_t plen;

   if (avail < 2)
      return 0;
   if (f[0] != 'R' || f[1] != 'A')
      return -1;
   if (avail < HEADER_SIZE)
      return 0;
   plen = get_u32(f + 6);
   if (f[2] != 1 || plen > MAX_PAYLOAD)
      return -1;
   *total = HEADER_SIZE + plen + 4;
   if (avail < *total)
      return 0;
   return crc32_update(0, f, HEADER_SIZE + plen) == get_u32(f + HEADER_SIZE + plen) ? 1 : -1;
}

/* Tira um quadro valido do inicio de rx, se houver. 1 = achou. */
static int take_frame(uint8_t *type, uint16_t *id, uint8_t **data, uint32_t *len)
{
   for (;;)
   {
      uint32_t total = 0, plen;
      size_t j;
      int r = frame_at(0, &total);

      if (r < 0)
      {
         memmove(rx, rx + 1, --rx_len);
         st.lixo++;
         continue;
      }
      if (r == 0)
      {
         /* Um falso cabecalho (ruido) pode anunciar um tamanho enorme e
          * prender os quadros de verdade que vem depois: se ja houver um
          * quadro completo e valido mais adiante, o lixo antes dele sai. */
         for (j = 1; j + 1 < rx_len; j++)
            if (rx[j] == 'R' && rx[j + 1] == 'A' && frame_at(j, &total) == 1)
               break;
         if (j + 1 >= rx_len)
            return 0;
         memmove(rx, rx + j, rx_len - j);
         rx_len -= j;
         st.lixo += j;
         continue;
      }

      plen  = total - HEADER_SIZE - 4;
      *type = rx[3];
      *id   = get_u16(rx + 4);
      *len  = plen;
      *data = malloc(plen + 1);
      memcpy(*data, rx + HEADER_SIZE, plen);
      memmove(rx, rx + total, rx_len - total);
      rx_len -= total;
      return 1;
   }
}

/* Espera, ate o prazo, um quadro com o id pedido e um dos tipos da mascara
 * (bit 1 << tipo). 0 = recebido, -1 = prazo esgotado, -2 = cabo caiu. */
static int wait_frame(struct link *l, unsigned types, uint16_t want_id,
      uint8_t *got_type, uint8_t **data, uint32_t *len, int timeout_ms)
{
   long long deadline = now_ms() + timeout_ms;

   for (;;)
   {
      uint8_t type;
      uint16_t id;
      ssize_t n;
      long long left;

      while (take_frame(&type, &id, data, len))
      {
         if (((1u << type) & types) && id == want_id)
         {
            if (got_type)
               *got_type = type;
            return 0;
         }
         free(*data); /* resposta atrasada de um pedido antigo */
      }

      left = deadline - now_ms();
      if (left <= 0)
         return -1;
      if (rx_cap - rx_len < 8192)
      {
         rx_cap = rx_cap ? rx_cap * 2 : 65536;
         if (rx_cap > 2 * MAX_PAYLOAD)
         {
            rx_len = 0; /* lixo demais; recomeca */
            rx_cap = 65536;
         }
         rx = realloc(rx, rx_cap);
      }
      n = link_read(l, rx + rx_len, rx_cap - rx_len, left > 200 ? 200 : (int)left);
      if (n < 0)
         return -2;
      rx_len += (size_t)n;
   }
}

/* Leva um pedido HTTP pelo cabo e traz a resposta (0 = ok, -1 = sem resposta,
 * -2 = cabo caiu). Resposta pequena vem num quadro so. A grande vem em
 * pedacos que a placa pede um a um: nunca ha mais que PEDACO_MAX bytes a
 * caminho, e um pedaco perdido e pedido de novo sozinho, sem repetir o resto.
 * Com o jogo rodando, a placa perde bytes de rajadas longas (o controlador
 * USB dessa porta depende da CPU), e uma resposta de 21 KB inteira nunca
 * chegava sem erro. */
static int transact(struct link *l, uint16_t id, const uint8_t *req, uint32_t req_len,
      uint8_t **resp, uint32_t *resp_len)
{
   long long deadline = now_ms() + RESPONSE_WAIT_MS, progress;
   uint8_t type, *data = NULL, *body;
   uint32_t len, total, crc, off = 0, chunk = PEDACO_MAX;
   int r = -1, streak = 0;

   /* Reenvia com o mesmo id se a resposta nao vier: o Pi guarda as ultimas
    * respostas e devolve a mesma, sem refazer a chamada. */
   while (r == -1 && now_ms() < deadline)
   {
      long long left = deadline - now_ms();

      st.tentativas++;
      if (send_frame(l, FRAME_PEDIDO_PEDACOS, id, req, req_len) < 0)
         return -2;
      r = wait_frame(l, 1u << FRAME_RESPOSTA | 1u << FRAME_TAMANHO, id, &type,
            &data, &len, left < RETRY_MS ? (int)left : RETRY_MS);
   }
   if (r != 0)
      return r;
   if (type == FRAME_RESPOSTA)
   {
      *resp     = data;
      *resp_len = len;
      return 0;
   }

   if (len != 8 || get_u32(data) > MAX_PAYLOAD)
   {
      free(data);
      return -1;
   }
   total = get_u32(data);
   crc   = get_u32(data + 4);
   free(data);
   body = malloc(total ? total : 1);

   progress = now_ms();
   while (off < total)
   {
      uint32_t want = total - off < chunk ? total - off : chunk;
      uint8_t ler[6];
      long long start;
      unsigned long base;
      int got = 0;

      if (now_ms() - progress > STALL_MS)
         break;
      put_u32(ler, off);
      put_u16(ler + 4, (uint16_t)want);
      if (send_frame(l, FRAME_LER, id, ler, sizeof(ler)) < 0)
      {
         free(body);
         return -2;
      }
      st.pedacos++;

      /* Espera o pedaco desse offset; um pedaco repetido que chegou atrasado
       * (de um LER anterior) e descartado. Se o pedaco comecou a chegar e
       * parou, faltam bytes: pede de novo sem esperar o prazo inteiro. */
      start = now_ms();
      base  = st.bytes_lidos;
      for (;;)
      {
         unsigned long before = st.bytes_lidos;
         long long left = start + PEDACO_WAIT_MS - now_ms();

         if (left <= 0)
            break;
         r = wait_frame(l, 1u << FRAME_PEDACO, id, NULL, &data, &len,
               left < PEDACO_GAP_MS ? (int)left : PEDACO_GAP_MS);
         if (r == -2)
         {
            free(body);
            return -2;
         }
         if (r < 0)
         {
            if (st.bytes_lidos == before && st.bytes_lidos != base)
               break;
            continue;
         }
         if (len >= 4 && get_u32(data) == OFFSET_DESCONHECIDO)
         {
            /* O Pi nao tem mais essa resposta (reiniciou, por exemplo). */
            free(data);
            free(body);
            return -1;
         }
         if (len == 4 + want && get_u32(data) == off)
         {
            memcpy(body + off, data + 4, want);
            off += want;
            progress = now_ms();
            got = 1;
            free(data);
            break;
         }
         free(data);
      }

      /* Pedacos menores quando o cabo perde muito; volta a crescer depois
       * de uma sequencia sem perdas. */
      if (got)
      {
         if (++streak >= 8 && chunk < PEDACO_MAX)
         {
            chunk *= 2;
            streak = 0;
         }
      }
      else
      {
         st.pedacos_repetidos++;
         streak = 0;
         if (chunk > PEDACO_MIN)
            chunk /= 2;
      }
   }

   if (off < total || crc32_update(0, body, total) != crc)
   {
      free(body);
      return -1;
   }
   *resp     = body;
   *resp_len = total;
   return 0;
}

/* --- HTTP local --------------------------------------------------------- */

static const char RESPOSTA_502[] =
   "HTTP/1.0 502 Bad Gateway\r\nContent-Type: text/plain\r\nContent-Length: 20\r\n"
   "Connection: close\r\n\r\nrelay sem resposta\r\n";

/* Le a requisicao inteira (cabecalhos + corpo pelo Content-Length). */
static uint8_t *read_request(int c, size_t *out_len)
{
   size_t cap = 8192, len = 0, want = 0;
   uint8_t *buf = malloc(cap + 1);
   char *end = NULL;

   for (;;)
   {
      ssize_t n;

      if (len == cap)
      {
         if (cap >= MAX_REQUEST)
            break;
         cap *= 2;
         buf = realloc(buf, cap + 1);
      }
      n = recv(c, buf + len, cap - len, 0);
      if (n <= 0)
         break;
      len += (size_t)n;
      buf[len] = '\0';

      if (!end && (end = strstr((char*)buf, "\r\n\r\n")))
      {
         const char *cl = NULL, *p = (char*)buf;
         size_t body = 0;

         while ((p = strchr(p, '\n')) && p < end)
         {
            p++;
            if (strncasecmp(p, "Content-Length:", 15) == 0)
               cl = p + 15;
         }
         if (cl)
            body = strtoul(cl, NULL, 10);
         want = (size_t)(end + 4 - (char*)buf) + body;
      }
      if (end && len >= want)
      {
         *out_len = want;
         return buf;
      }
   }
   free(buf);
   return NULL;
}

/* O "r=" da API (login2, patch, awardachievement...) para o log; o resto do
 * pedido tem a senha e o token, que nao vao para o log. */
static void api_name(const uint8_t *req, size_t len, char *out, size_t size)
{
   size_t i, n = 0;

   for (i = 0; i + 2 < len; i++)
   {
      if (req[i + 1] != 'r' || req[i + 2] != '='
            || (req[i] != '?' && req[i] != '&' && req[i] != '\n'))
         continue;
      for (i += 3; i < len && n + 1 < size
            && (isalnum(req[i]) || req[i] == '_'); i++)
         out[n++] = (char)req[i];
      break;
   }
   if (!n)
      out[n++] = '-';
   out[n] = '\0';
}

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

int main(int argc, char **argv)
{
   const char *tty = NULL;
   int baud = 921600, port = 8080, ping_only = 0, i, srv, one = 1;
   uint16_t next_id;
   static struct link l;
   struct sockaddr_in addr;

   for (i = 1; i < argc; i++)
   {
      if (!strcmp(argv[i], "-t") && i + 1 < argc)      tty = argv[++i];
      else if (!strcmp(argv[i], "-u"))                 tty = NULL;
      else if (!strcmp(argv[i], "-b") && i + 1 < argc) baud = atoi(argv[++i]);
      else if (!strcmp(argv[i], "-p") && i + 1 < argc) port = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--ping"))             ping_only = 1;
      else
      {
         fprintf(stderr, "uso: %s [-t tty | -u] [-b baud] [-p porta] [--ping]\n", argv[0]);
         return 2;
      }
   }

   l.fd = -1;
   crc_init();
   /* Id inicial aleatorio: um pedido novo nunca casa com uma resposta que o
    * Pi guardou de uma execucao anterior. */
   srand((unsigned)(now_ms() ^ getpid()));
   next_id = (uint16_t)rand();
   signal(SIGPIPE, SIG_IGN);
   {
      /* Sem SA_RESTART: o accept() precisa ser interrompido para sair. */
      struct sigaction sa;
      memset(&sa, 0, sizeof(sa));
      sa.sa_handler = on_signal;
      sigaction(SIGINT, &sa, NULL);
      sigaction(SIGTERM, &sa, NULL);
   }
   {
      /* Tempo real e memoria travada: o relay recolhe as leituras do USB
       * assim que chegam, mesmo com o jogo ocupando a CPU, e nunca espera o
       * pendrive para recarregar paginas do proprio binario. */
      struct sched_param sp;
      memset(&sp, 0, sizeof(sp));
      sp.sched_priority = 10;
      if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
         logmsg("sem prioridade de tempo real: %s", strerror(errno));
      if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0)
         logmsg("sem travar a memoria: %s", strerror(errno));
   }

#define OPEN_LINK() (tty ? link_open_tty(&l, tty, baud) : link_open_cp2102(&l, baud))

   if (ping_only)
   {
      uint8_t *data;
      uint32_t len;
      long long t0;

      if (OPEN_LINK() < 0)
         return 1;
      t0 = now_ms();
      if (send_frame(&l, FRAME_PING, next_id, NULL, 0) < 0
            || wait_frame(&l, 1u << FRAME_PONG, next_id, NULL, &data, &len, 3000) < 0)
      {
         logmsg("PING sem resposta");
         return 1;
      }
      free(data);
      logmsg("PONG em %lld ms", now_ms() - t0);
      return 0;
   }

   srv = socket(AF_INET, SOCK_STREAM, 0);
   setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
   memset(&addr, 0, sizeof(addr));
   addr.sin_family      = AF_INET;
   addr.sin_port        = htons((uint16_t)port);
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(srv, 16) < 0)
   {
      logmsg("nao consegui escutar em 127.0.0.1:%d: %s", port, strerror(errno));
      return 1;
   }
   logmsg("escutando em 127.0.0.1:%d", port);
   OPEN_LINK();

   while (running)
   {
      struct timeval tv = { 10, 0 };
      uint8_t *req, *resp = NULL;
      uint32_t resp_len = 0;
      size_t req_len;
      char api[24];
      long long t0;
      int c = accept(srv, NULL, NULL), r;

      if (c < 0)
         continue;
      setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      req = read_request(c, &req_len);
      if (!req)
      {
         close(c);
         continue;
      }

      /* Reabre o cabo se ele caiu (CP2102 desconectado, por exemplo). */
      if (l.fd < 0)
         OPEN_LINK();

      memset(&st, 0, sizeof(st));
      t0 = now_ms();
      r  = l.fd >= 0 ? transact(&l, next_id, req, (uint32_t)req_len, &resp, &resp_len) : -2;
      if (r == -2)
         link_close(&l);

      if (r == 0)
         send(c, resp, resp_len, 0);
      else
         send(c, RESPOSTA_502, sizeof(RESPOSTA_502) - 1, 0);
      close(c);

      /* O log vai para o pendrive, que e lento: so depois de responder. */
      api_name(req, req_len, api, sizeof(api));
      logmsg("pedido %u (%s): %s, %u bytes em %lld ms; tentativas %d, pedacos %d, "
            "perdidos %d, lixo %lu B, %lu leituras (%lu B)",
            next_id, api, r == 0 ? "ok" : "SEM RESPOSTA", r == 0 ? resp_len : 0,
            now_ms() - t0, st.tentativas, st.pedacos, st.pedacos_repetidos,
            st.lixo, st.leituras, st.bytes_lidos);
      next_id++;
      free(req);
      free(resp);
   }

   link_close(&l);
   close(srv);
   return 0;
}
