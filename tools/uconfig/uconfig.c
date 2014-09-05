#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <unistd.h>
#include <sysexits.h>
#include <fcntl.h>
#include <errno.h>

#include <katcl.h>
#include <katcp.h>

#include <sys/stat.h>

/******************************************************************************/

#ifdef __PPC__
#define DEFAULT_DEVICE "/dev/mtdblock4"
#else
#define DEFAULT_DEVICE "uboot-config"
#endif

#define LABEL "uconfig"

#if 0
#define DEFAULT_SIZE (512 * 256)
#endif

#define DEFAULT_SIZE        (8 * 1024)
#define DEFAULT_PRIMARY             0
#define DEFAULT_ALTERNATE (128 * 1024)

#define CHECKSUM_SIZE 4

/******************************************************************************/

struct uconfig_item{
  char *i_key;
  char *i_value;
};

struct uconfig_state{
  unsigned char *u_buffer;
  unsigned int u_size;

  struct uconfig_item *u_vector;
  unsigned int u_count;

  unsigned int u_data;
  uint32_t u_checksum;

  unsigned int u_source;
  unsigned int u_target;

  struct katcl_line *u_line;
};

/******************************************************************************/

void destroy_uconfig(struct uconfig_state *uc);

/******************************************************************************/

struct uconfig_state *init_uconfig(unsigned int size)
{
  struct uconfig_state *uc;

  uc = malloc(sizeof(struct uconfig_state));
  if(uc == NULL){
    return NULL;
  }

  uc->u_buffer = malloc(size);
  if(uc->u_buffer == NULL){
    destroy_uconfig(uc);
    return NULL;
  }

  uc->u_size = size;

  uc->u_vector = NULL;
  uc->u_count = 0;

  uc->u_data = 0;
  uc->u_checksum = 0;

  uc->u_source = 0;
  uc->u_target = 0;

  uc->u_line = create_katcl(STDOUT_FILENO);

  return uc;
}

void destroy_uconfig(struct uconfig_state *uc)
{
  unsigned int i;
  struct uconfig_item *item;

  if(uc == NULL){
    return;
  }

  if(uc->u_buffer){
    free(uc->u_buffer);
    uc->u_buffer = NULL;
  }
  uc->u_size = 0;

  if(uc->u_vector){
    for(i = 0; i < uc->u_count; i++){
      item = &(uc->u_vector[i]);

      if(item->i_key){
        free(item->i_key);
      }
      if(item->i_value){
        free(item->i_value);
      }
    }
    free(uc->u_vector);
    uc->u_count = 0;
  }

  uc->u_data = 0;
  uc->u_checksum = 0;

  uc->u_source = 0;
  uc->u_target = 0;

  if(uc->u_line){
    destroy_katcl(uc->u_line, 0);
  }

  free(uc);
}

int fill_uconfig(struct uconfig_state *uc, int fd)
{
  off_t position;
  unsigned int have;
  int rr;

  position = lseek(fd, uc->u_source, SEEK_SET);
  if((position < 0) || (uc->u_source != position)){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to reach position %u: %s", uc->u_source, strerror(errno));
    return -1;
  }

  have = 0;

  do{
    rr = read(fd, uc->u_buffer + have, uc->u_size - have);
    if(rr <= 0){
      if(rr < 0){
        log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "read failed: %s\n", strerror(errno));
        return -1;
      } else {
        return 1;
      }
    } else {
      have += rr;
    }
  } while(have < uc->u_size);

  return 0;
}

int read_uconfig(struct uconfig_state *uc, char *name, unsigned int offset)
{
  int fd;
  off_t position;
  unsigned int have;
  int rr;

  if(uc->u_size <= 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "no buffer available to load %s", name);
    return -1;
  }

  fd = open(name, O_RDONLY);
  if(fd < 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to open %s: %s", name, strerror(errno));
    return -1;
  }

  if(offset >= 0){
    position = lseek(fd, offset, SEEK_SET);
    if((position < 0) || (offset != position)){
      log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to reach position %u in %s: %s", offset, name, strerror(errno));
      close(fd);
      return -1;
    }
  }

  have = 0;

  do{
    rr = read(fd, uc->u_buffer + have, uc->u_size - have);
    if(rr <= 0){
      close(fd);
      if(rr < 0){
        log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "read failed on %s: %s\n", name, strerror(errno));
        return -1;
      } else {
        return 1;
      }
    } else {
      have += rr;
    }
  } while(have < uc->u_size);

  close(fd);

  if(uc->u_size >= 5){
    switch(uc->u_buffer[4]){
      case 0x0 :
      case 0x1 :
        /* has a flags field */
        uc->u_data = 5;
        break;
      default :
        /* no reasonable flags, assume less */
        uc->u_data = 4;
        break;
    }
  } else {
    uc->u_data = 0;
  }

  return 0;
}

int add_item(struct uconfig_state *uc, char *key, char *value)
{
  struct uconfig_item *ip;
  unsigned int i;
  char *ptr;

  if((key == NULL) || (value == NULL)){
    return -1;
  }

  for(i = 0; (i < uc->u_count) && strcmp(key, uc->u_vector[i].i_key); i++);

  if(i < uc->u_count){
    ip = &(uc->u_vector[i]);

    ptr = strdup(value);
    if(ptr == NULL){
      return -1;
    }

    if(ip->i_value){
      free(ip->i_value);
    }

    ip->i_value = ptr;

    return 0;
  }

  ip = realloc(uc->u_vector, sizeof(struct uconfig_item) * (uc->u_count + 1));
  if(ip == NULL){
    return -1;
  }

  uc->u_vector = ip;

  ip = &(uc->u_vector[uc->u_count]);

  ip->i_key = strdup(key);
  if(key == NULL){
    return -1;
  }

  ip->i_value = strdup(value);
  if(ip->i_value == NULL){
    free(ip->i_key);
    ip->i_key = NULL;
    return -1;
  }

  uc->u_count++;
  return 0;
}

int del_item(struct uconfig_state *uc, char *key)
{
  struct uconfig_item *ip, *it;
  unsigned int i;

  if(key == NULL){
    return -1;
  }

  for(i = 0; (i < uc->u_count) && strcmp(key, uc->u_vector[i].i_key); i++);

  if(i >= uc->u_count){
    return -1;
  }

  uc->u_count--;

  ip = &(uc->u_vector[i]);

  while(i < uc->u_count){
    i++;
    it = &(uc->u_vector[i]);
    ip->i_key   = it->i_key;
    ip->i_value = it->i_value;

    ip = it;
  }

  return 0;
}

int insert_item(struct uconfig_state *uc, unsigned int key, unsigned int value)
{
  struct uconfig_item *ip;
  unsigned int amount;

  ip = realloc(uc->u_vector, sizeof(struct uconfig_item) * (uc->u_count + 1));
  if(ip == NULL){
    return -1;
  }

  uc->u_vector = ip;

  ip = &(uc->u_vector[uc->u_count]);

  if((key + 1) > value){
    sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "major logic problem: key null or ahead of value");
    abort();
  }

  ip->i_key = malloc(value - key);
  if(ip->i_key == NULL){
    return -1;
  }

  amount = value - (1 + key);

  memcpy(ip->i_key, uc->u_buffer + key, amount);
  ip->i_key[amount] = '\0';

  /* WARNING: we assume null termination - does our parse routine guarantee this ? */
  ip->i_value = strdup(uc->u_buffer + value);
  if(ip->i_value == NULL){
    free(ip->i_key);
    ip->i_key = NULL;
    return -1;
  }

  uc->u_count++;
  return 0;
}

int parse_uconfig(struct uconfig_state *uc)
{
#define STATE_KEY      1
#define STATE_VALUE    2
#define STATE_NULL     3
  unsigned int state, i, key, value;

  if(uc->u_data <= 0){
    return -1;
  }

  i = uc->u_data;

  state = STATE_KEY;
  key = i;

  /* pacify compiler */
  value = 0;

  while(i < uc->u_size){

#ifdef DEBUG
    fprintf(stderr, "[%c<%d] ", uc->u_buffer[i], state);
#endif

    switch(uc->u_buffer[i]){
      case '\0' :
        i++;
        switch(state){
          case STATE_KEY :
            /* encountered key\0 ... possibly just skip ? */
#ifdef DEBUG
            fprintf(stderr, "\n");
#endif
            return -1;
          case STATE_VALUE :
            state = STATE_NULL;
            if(insert_item(uc, key, value) < 0){
#ifdef DEBUG
              fprintf(stderr, "\n");
#endif
              return -1;
            }
            key = i;
            break;
          case STATE_NULL :
            /* double nul, now done */
#ifdef DEBUG
            fprintf(stderr, "\n");
#endif
            return 0;
        }
        break;

      case '='  :
        i++;
        switch(state){
          case STATE_KEY   :
            value = i;
            state = STATE_VALUE;
            break;
          case STATE_VALUE :
            /* all ok */
            break;
          case STATE_NULL  :
            /* encountered \0=  */
#ifdef DEBUG
            fprintf(stderr, "\n");
#endif
            return -1;
        }
        break;

      default :
        i++;
        switch(state){
          case STATE_KEY   :
            /* all ok */
            break;
          case STATE_VALUE :
            /* all ok */
            break;
          case STATE_NULL  :
            state = STATE_KEY;
            break;
        }
        break;
    }
  }

#ifdef DEBUG
  fprintf(stderr, "\n");
#endif

  return -1;
}

#define CRCPOLY_LE 0xedb88320

uint32_t crc32_le(uint32_t crc, unsigned char *p, unsigned int len)
{
  int i;
  while (len--) {
    crc ^= *p++;
    for (i = 0; i < 8; i++)
      crc = (crc >> 1) ^ ((crc & 1) ? CRCPOLY_LE : 0);
  }
  return crc;
}

void checksum_uconfig(struct uconfig_state *uc)
{
  uint32_t checksum;

  if(uc->u_data <= 0){
    uc->u_checksum = 0;
    return;
  }

  checksum = crc32_le(0xffffffffUL, uc->u_buffer + uc->u_data, uc->u_size - uc->u_data);

  uc->u_checksum = checksum ^ 0xffffffffUL;

#ifdef DEBUG
  fprintf(stderr, "checksum: computed checksum 0x%08x\n", uc->u_checksum);
#endif
}

void word_to_bytes(unsigned char *buffer, uint32_t word)
{
  buffer[0] = (word >> 24) & 0xff;
  buffer[1] = (word >> 16) & 0xff;
  buffer[2] = (word >>  8) & 0xff;
  buffer[3] = (word      ) & 0xff;
}

int flatten_uconfig(struct uconfig_state *uc)
{
  unsigned int i, need, used, kl, vl;
  struct uconfig_item *ip;

  i = uc->u_data;

  if(uc->u_data == 0){
    uc->u_data = 5; /* ... guessing a format ... */
  }

  used = uc->u_data;

  for(i = 0; i < uc->u_count; i++){
    ip = &(uc->u_vector[i]);

    kl = strlen(ip->i_key);
    vl = strlen(ip->i_value);

    need = kl + 1 + vl + 2;

    if((used + need) > uc->u_size){
      uc->u_buffer[used] = '\0'; /* just in case this gets written out somehow */
      return -1;
    }

    memcpy(uc->u_buffer + used, ip->i_key, kl);
    used += kl;

    uc->u_buffer[used] = '=';
    used++;

    memcpy(uc->u_buffer + used, ip->i_value, vl);
    used += vl;

    uc->u_buffer[used] = '\0';
    used++;
  }

  uc->u_buffer[used] = '\0';

  return 0;
}

int write_uconfig(struct uconfig_state *uc, int fd)
{
  off_t position;
  unsigned int have;
  int wr;

  position = lseek(fd, uc->u_target, SEEK_SET);
  if((position < 0) || (uc->u_target != position)){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to reach position %u: %s", uc->u_target, strerror(errno));
    return -1;
  }

  have = 0;
  do{
    wr = write(fd, uc->u_buffer + have, uc->u_size - have);
    if(wr <= 0){
      if(wr < 0){
        log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "write: unable to write: %s", strerror(errno));
      }
      return -1;
    }
    have += wr;
  } while(have < uc->u_size);

  return 0;
}

/******************************************************************************/

int load_uconfig(struct uconfig_state *uc, char *name)
{
#define HEADER   5
  unsigned char header[HEADER];
  unsigned char alt[HEADER];
  unsigned char checksum[CHECKSUM_SIZE];
  off_t position;
  int rr, fd;

  fd = open(name, O_RDONLY);
  if(fd < 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to open %s: %s", name, strerror(errno));
    return -1;
  }

  rr = read(fd, header, HEADER);
  if(rr < 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to read from %s: %s", name, strerror(errno));
    close(fd);
    return -1;
  }
  if(rr != HEADER){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "short read from %s", name);
    close(fd);
    return -1;
  }

  position = lseek(fd, DEFAULT_ALTERNATE, SEEK_SET);
  if((position < 0) || (position != DEFAULT_ALTERNATE)){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to reach position %u in %s: %s", DEFAULT_ALTERNATE, name, strerror(errno));
    close(fd);
    return -1;
  }

  rr = read(fd, alt, HEADER);
  if(rr < 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to read from %s: %s", name, strerror(errno));
    close(fd);
    return -1;
  }
  if(rr != HEADER){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "short read from %s", name);
    close(fd);
    return -1;
  }

  uc->u_source = 0;

  switch(header[4]){
    case 0x0  : /* obsolete */
      if(alt[4] == 0x1){ /* ... and alternate valid */
        uc->u_source = DEFAULT_ALTERNATE;
        uc->u_target = 0;
        uc->u_data = 5;
        log_message_katcl(uc->u_line, KATCP_LEVEL_INFO, LABEL, "loading second config block");
        break;
      }
      /* fall */
    case 0x1  : /* active */
      uc->u_source = 0;
      uc->u_target = DEFAULT_ALTERNATE;
      uc->u_data = 5;
      log_message_katcl(uc->u_line, KATCP_LEVEL_INFO, LABEL, "loading first config block");
      break;
    default   : /* can't possibly be redundant */
      memset(alt, 0xff, HEADER);
      if(memcmp(alt, header, HEADER)){
        log_message_katcl(uc->u_line, KATCP_LEVEL_INFO, LABEL, "no redundant config found, using only one");
        uc->u_source = 0;
        uc->u_target = 0;
        uc->u_data = 4;
      } else {
        log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "no environment found, giving up");
        close(fd);
        return -1;
      }
      break;
  }

  if(fill_uconfig(uc, fd) < 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to load %s", name);
    close(fd);
    return -1;
  }

  close(fd);

  checksum_uconfig(uc);

  word_to_bytes(checksum, uc->u_checksum);
  
  if(memcmp(checksum, uc->u_buffer, CHECKSUM_SIZE)){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "invalid checksum, expected 0x%08x", uc->u_checksum);
    return -1;
  }

  if(parse_uconfig(uc) < 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to parse config block");
    return -1;
  }

  return 0;
#undef HEADER
}

#if 0
int load_uconfig(struct uconfig_state *uc, char *name)
{
  unsigned char buffer[4];

  if(read_uconfig(uc, name, offset) < 0){
    return -1;
  }

  log_message_katcl(uc->u_line, KATCP_LEVEL_INFO, LABEL, "read configuration variables from %s", name);

  if(parse_uconfig(uc) < 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to parse config block");
    return -1;
  }

  checksum_uconfig(uc);

  word_to_bytes(buffer, uc->u_checksum);
  
  if(memcmp(buffer, uc->u_buffer, 4)){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "invalid checksum, expected 0x%08x", uc->u_checksum);
    return -1;
  }

  log_message_katcl(uc->u_line, KATCP_LEVEL_INFO, LABEL, "checksum 0x%08x ok", uc->u_checksum);

  return 0;
}
#endif

int save_uconfig(struct uconfig_state *uc, char *name)
{
  unsigned char obsolete;
  off_t position;
  int fd;

  if(flatten_uconfig(uc) < 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to serialise configuration variables");
    return -1;
  }

  log_message_katcl(uc->u_line, KATCP_LEVEL_INFO, LABEL, "serialised configuration variables");

  checksum_uconfig(uc);

  word_to_bytes(uc->u_buffer, uc->u_checksum);

  fd = open(name, O_WRONLY);
  if(fd < 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to open %s for writing: %s", name, strerror(errno));
    return -1;
  }

  if(write_uconfig(uc, fd) < 0){
    log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to write out variables to %s at 0x%x", name, DEFAULT_PRIMARY);
    close(fd);
    return -1;
  }

  log_message_katcl(uc->u_line, KATCP_LEVEL_INFO, LABEL, "wrote config block to 0x%x", uc->u_target);

  if(uc->u_target != uc->u_source){
    obsolete = 0;

    /* invalidate source */
    position = lseek(fd, uc->u_source + CHECKSUM_SIZE, SEEK_SET);
    if((position < 0) || ((uc->u_source + CHECKSUM_SIZE) != position)){
      log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to reach position %u: %s", uc->u_source, strerror(errno));
      close(fd);
      return -1;
    }

    if(write(fd, &obsolete, 1) != 1){
      log_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to invalidate previous config block");
      close(fd);
      return -1;
    }

    log_message_katcl(uc->u_line, KATCP_LEVEL_INFO, LABEL, "invalidated previous config block at 0x%x", uc->u_source);
  }

  fsync(fd);
  close(fd);

  log_message_katcl(uc->u_line, KATCP_LEVEL_INFO, LABEL, "wrote out configuration to %s", name);

  return 0;
}

int print_uconfig(struct uconfig_state *uc)
{
  unsigned int i;

  if(uc == NULL){
    return -1;
  }

  for(i = 0; i < uc->u_count; i++){
    printf("%s=%s\n", uc->u_vector[i].i_key, uc->u_vector[i].i_value);
  }

#if 0
  printf("checksum=%02x %02x %02x %02x\n", 0xff & uc->u_buffer[0], 0xff & uc->u_buffer[1], 0xff & uc->u_buffer[2], 0xff & uc->u_buffer[3]);
#endif

  return 0;
}

void usage(char *app)
{
  printf("usage: %s [-h] [-p] [-i device] [-d key]* [-a key value]*\n", app);
  printf("-p             do not update the device\n");
  printf("-h             this help\n");
  printf("-i device      image/device to modifiy (default is %s)\n", DEFAULT_DEVICE);
  printf("-a key value   variable to add\n");
  printf("-d key         variable to remove\n");
}

int main(int argc, char **argv)
{
  int i, j, c;
  struct uconfig_state *uc;
  int loaded, printonly;
  char *key, *value, *device;

  device = DEFAULT_DEVICE;

  uc = init_uconfig(DEFAULT_SIZE);
  if(uc == NULL){
    fprintf(stderr, "unable to initialise state\n");
    return EX_OSERR;
  }

  loaded = 0;
  printonly = 0;
  key = NULL;
  value = NULL;

  i = j = 1;
  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(argv[0]);
          return 1;

        case '-' :
          j++;
          break;

        case 'p' : 
          printonly = 1;
          j++;
          break;

        case 'a' :

          if(loaded == 0){
            if(load_uconfig(uc, device) < 0){
              sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to load %s", device);
              return 2;
            }
            loaded = 1;
          }

          j++;

          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }

          if (i >= argc) {
            sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "add needs a key value pair");
            return 2;
          }

          key = argv[i] + j;

          j = 0;
          i++;

          if (i >= argc) {
            sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "add needs a key value pair", argv[0]);
            return 2;
          }

          value = argv[i];

          if(add_item(uc, key, value) < 0){
            sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to add %s=%s\n", key, value);
            return 2;
          }

          log_message_katcl(uc->u_line, KATCP_LEVEL_DEBUG, LABEL, "updating %s", key);
          
          break;

        case 'd' :

          if(loaded == 0){
            if(load_uconfig(uc, device) < 0){
              sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to load %s\n", device);
              return 2;
            }
            loaded = 1;
          }

          j++;

          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "delete needs a key to remove");
            return 2;
          }

          key = argv[i] + j;

          if(del_item(uc, key) < 0){
            sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to remove %s\n", key);
            return 2;
          }

          log_message_katcl(uc->u_line, KATCP_LEVEL_DEBUG, LABEL, "removing %s", key);

          break;

        case 'i' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }

          if (i >= argc) {
            sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "need a device or image");
            return 2;
          }

          device = argv[i] + j;
          break;

        case '\0':
          j = 1;
          i++;
          break;

        default:
          sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unknown option -%c", c);
          return 2;
      }
    } else {
      i++;
      j = 1;
    }
  }

  if(loaded == 0){
    if(load_uconfig(uc, device) < 0){
      sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to load %s", device);
      return 2;
    }
    loaded = 1;
  }

  if(printonly){
    print_uconfig(uc);
    destroy_uconfig(uc);
    return 0;
  } 

  if(save_uconfig(uc, device) < 0){
    sync_message_katcl(uc->u_line, KATCP_LEVEL_ERROR, LABEL, "unable to write out updates to %s", device);
    return 2;
  }

  sync_message_katcl(uc->u_line, KATCP_LEVEL_INFO, LABEL, "completed update");
  destroy_uconfig(uc);

  return 0;
}
