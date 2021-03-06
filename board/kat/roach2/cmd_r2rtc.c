/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <command.h>

#include <asm/processor.h>
#include <asm/ppc4xx.h>
#include <asm/ppc4xx-gpio.h>
#include <asm/ppc4xx-ebc.h>
#include <i2c.h>

#include "include/rtc.h"

#ifdef CONFIG_CMD_R2RTC

static int dump_roach2_rtc(void)
{
  u8 buffer [R2_RTC_DS1307_TIME_LEN];
  if (i2c_read(R2_RTC_DS1307_U11_I2C_ADDR,
               R2_RTC_DS1307_TIME, 1, buffer,
               R2_RTC_DS1307_TIME_LEN) != 0) {
    printf("cannot read from i2c device: %02x\n", R2_RTC_DS1307_U11_I2C_ADDR);
    return 1;
  }

  printf("%s ",R2_RTC_DS1307_DAY(buffer[3]-1));
  printf("%02d/", (buffer[4] & 0xf) + (buffer[4] >> 4)*10);
  printf("%02d/", (buffer[5] & 0xf) + (buffer[5] >> 4)*10);
  printf("20%02d ", (buffer[6] & 0xf) + (buffer[6] >> 4)*10);

  if (buffer[2] & R2_RTC_DS1307_12HR){
    printf("%02d:", (buffer[2] & 0xf) +
                   ((buffer[2] & 0x10) >> 4)*10 +
                   (buffer[2] & 0x2 ? 12 : 0));
  } else {
    printf("%02d:", (buffer[2] & 0xf) + ((buffer[6] & 0x30) >> 4)*10);
  }
  printf("%02d:", (buffer[1] & 0xf) + (buffer[1] >> 4)*10);
  printf("%02d\n", (buffer[0] & 0xf) + ((buffer[0]&0x70) >> 4)*10);

  return 0;
}

#define DATE_BUF_SIZE 256

int str_get_token(const char* str, char delim, char* buffer, int bufsize)
{
  int offset = 0; 
  int got=0;
  int buf_index=0;
  while (str[offset] != '\0' && buf_index < bufsize - 1){
    if (str[offset] == delim){
      if (got == 1)
        got=2;
    } else {
      if (got == 2){
        break;
      } else {
        buffer[buf_index] = str[offset];
        buf_index++;
        got = 1;
      }
    }
    offset++;
  }
  if (buf_index >= bufsize - 1){
    buffer[bufsize - 1] = '\0';
  } else {
    buffer[buf_index] = '\0';
  }
  return offset;
}

static int set_roach2_rtc(const char* time_str)
{
  u8 databuf [R2_RTC_DS1307_TIME_LEN];
  char strbuf [DATE_BUF_SIZE];
  int offset=0;
  u8 i;

  /* Day */
  offset += str_get_token(time_str + offset, ' ', strbuf, DATE_BUF_SIZE);
  for (i=0; i < 7; i++){
    if (!strncmp(R2_RTC_DS1307_DAY(i), strbuf, 3)){
      databuf[3] = i+1;
      break;
    }
    if (i==7){
      printf("error: invalid DAY\n");
      return 1;
    }
  }
  /* Date */
  offset += str_get_token(time_str + offset, '/', strbuf, DATE_BUF_SIZE);
  i = simple_strtoul(strbuf, NULL, 10);
  if (i  < 1  || i > 31){
    printf("error: invalid DATE\n");
    return 1;
  }
  databuf[4] = (i % 10) + ((i/10) << 4);

  /* Month */
  offset += str_get_token(time_str + offset, '/', strbuf, DATE_BUF_SIZE);
  i = simple_strtoul(strbuf, NULL, 10);
  if (i  < 1  || i > 12){
    printf("error: invalid MONTH\n");
    return 1;
  }
  databuf[5] = (i % 10) + ((i/10) << 4);

  /* Year */
  offset += str_get_token(time_str + offset, ' ', strbuf, DATE_BUF_SIZE);
  if (strnlen(strbuf, DATE_BUF_SIZE) == 4){
    i = simple_strtoul(strbuf + 2, NULL, 10);
  } else {
    i = simple_strtoul(strbuf, NULL, 10);
  }
  if (i > 99){
    printf("error: invalid YEAR\n");
    return 1;
  }
  databuf[6] = (i % 10) + ((i/10) << 4);

  /* Hours */
  offset += str_get_token(time_str + offset, ':', strbuf, DATE_BUF_SIZE);
  i = simple_strtoul(strbuf, NULL, 10);
  if (i > 23){
    printf("error: invalid HOURS\n");
    return 1;
  }
  databuf[2] = (i % 10) + ((((i/10) << 4))&0x3);

  /* Minutes */
  offset += str_get_token(time_str + offset, ':', strbuf, DATE_BUF_SIZE);
  i = simple_strtoul(strbuf, NULL, 10);
  if (i > 59){
    printf("error: invalid MINUTES\n");
    return 1;
  }
  databuf[1] = (i % 10) + (((i/10) << 4));

  /* Seconds */
  offset += str_get_token(time_str + offset, '\0', strbuf, DATE_BUF_SIZE);
  i = simple_strtoul(strbuf, NULL, 10);
  if (i > 59){
    printf("error: invalid SECONDS\n");
    return 1;
  }
  databuf[0] = (i % 10) + (((i/10) << 4));

  if (i2c_write(R2_RTC_DS1307_U11_I2C_ADDR,
                R2_RTC_DS1307_TIME, 1, databuf,
                R2_RTC_DS1307_TIME_LEN) != 0) {
    printf("cannot write to i2c device: %02x\n", R2_RTC_DS1307_U11_I2C_ADDR);
    return -1;
  }
  return 0;
}

static int do_roach2_rtc(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
  
  switch (argc) { 
    case 2:
      return set_roach2_rtc(argv[1]);
    case 1:
      return dump_roach2_rtc();
      break;
    default:
      return 1;
  }

  return 1;
}

U_BOOT_CMD(
    r2rtc, 4, 1, do_roach2_rtc,
    "access roach2 real-time clock",
    "         - get the real-time clock time\n"
    "         <time> - set the real-time clock"
);

#endif /* CONFIG_CMD_R2RTC */ 
