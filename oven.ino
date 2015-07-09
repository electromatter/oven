/*******************************************************************************
 * Oven v2 - uses a line based protocol to control my toaster oven 
 *
 * Copyright (C) 2015 Eric Chai <electromatter@gmail.com>
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 *******************************************************************************
 * 
 * Accepts '\r', '\n', or '\r\n' line endings
 * Always sends data back with '\r\n' line endings
 * To issue a keep-alive, use send an empty line to read the sensor values
 * 
 * To get a reading just send 'read' or an empty line. This device responds with:
 *  '<thermo reading> <cal reading> <oven enabled> <time since last command>'
 *   thermo and cal readings are raw analog readings. bound: 0-1023 (int)
 * 
 *   cal shunt value is the voltage expected on the cal pin. bound: 0-5 (float) 
 *   cal reading and shunt value are x if cal is disabled
 * 
 *   oven enabled 1 for enabled, 0 for disabled
 * 
 *   time since last command is the milliseconds since last command bound: 0-maxtimeout (int)
 *   uses the internal clock, it is ment to be summed estimate the total ammount of thermal energy
 *   dumped into the oven
 *   time since last command is x if the time exceeds maxtimeout or if the oven is off
 * 
 * To enable the oven send 'on <duration>'
 *   duration is in milliseconds
 *   responds with the sensor readings
 * 
 * To disable the oven send 'off'
 *   this is useful if you want to immediatly turn the oven off.
 *   responds with the sensor readings
 * 
 * To get a help message send 'help'
 *   the help message is terminated with an empty line
 *   the help message always starts with a version string eg: 'Oven version 2.0 <extra info>'
 *   extra info could be empty, if it is the trailing space is not required
 * 
 * To just get the version string send 'version'
 * 
 * To get some basic config info send 'config'. This device responds with:
 *  '<cal_enabled> <cal_voltage> <relay_enabled> <maxtimeout> <linemax>'
 * 
 * If a command is unreconized, this device responds with:
 *  'Invalid command, use help for a list of commands.'
 * 
 * Equations:
 *  Voltage from analog value:
 *   // Voltage reference on the AVR ADC
 *   ADC_Ref = 5.0
 *   k = (ADC_Ref / 1023.)
 *   Volts = k * ADC_Reading
 *  Voltage from analog value, with calibration:
 *   Calibration_Voltage = 3.3
 *   Volts = (Calibration_Voltage / Calibration_Reading) * ADC_Reading
 *  Temperature (in deg C) from voltage:
 *   // May vary, see your thermocouple amp datasheet
 *   Temperature = (Volts - 1.25) / 0.005
 * 
 * improvements:
 *   add current/voltage/or energy mesurement to the oven elements to more accurately get the energy
 *   pid on the arduino instead of the controller
 *   relay feedback
 *   smarter parse_command
 *   vt100 terminal/better shell echoing input
 *   reduce the number of global variable memory usage: use macros
 *   more accurate delta time for off command, currently it reports x
 *   better error reporting
 *   clean up oven design add lcd, buttons, sd, ethernet, safety features
 *   more accurate adc
 *   use eeprom for config storage
 */

/* configuration constants */
/*baud rate 9600 is a good default, but 115200 is much faster */
const int baud = 115200;
/* analog pin the thermocuple amplifier is on                                  */
const int therm_pin = 5;
/* analog pin the calibration diode is on or -1 to disable calibration         */
const int cal_pin = -1;
/* the voltage of the cal pin. Omit the units it is always volts.              *
 *   ignored if calibration is disabled (cal_pin=-1)                           */
const char *cal_volts = "5.0";
/* relay pin or -1 to disable the relay                                        */
const int relay_pin = 2;
/* pin of the status led that is on when the relay is on or -1 to disable      */
const int led_pin = 13;
/* max timeout in milliseconds. recommend: 100 is bounded 0-1048, see set_wdt  */
const int maxtimeout = 100;
/* must be greater than zero. recommend: 80                                    */
const int linemax = 80;

/* null terminated line string */
char line[linemax + 1] = {0};

/* internal */
static const char *oven_version = "Oven version 2.0";
static const char *copyright = "Copyright (C) 2015 Eric Chai <electromatter@gmail.com>\r\nAll rights reserved.";
static const char *usage = 
"Oven uses a line based protocol with '\\r\\n' line terminators\r\n"
"This message always ends with an empty line\r\n"
"If a feature is not enabled i.e. calibration, an x will be used inplace of its reading\r\n"
" read            reads out the sensor values formatted as:\r\n"
"                     <therm reading> <cal reading> <relay status> <time sence last read>\r\n"
"                 sensors can also be read by sending an empty line\r\n"
" on <duration>   turns the oven on for duration milliseconds and reads sensor values\r\n"
" off             turns the oven off and reads sensor values\r\n"
" version         prints version string formatted as:\r\n"
"                     <name> version <version> <extra info>\r\n"
" config          prints config string formatted as:\r\n"
"                     <cal enabled> <cal_voltage> <relay_enabled> <maxtimeout> <linemax>\r\n"
" help            prints this message\r\n"
"The source code is available under the MIT License on github at:\r\n"
"https://github.com/electromatter/oven";

static int state = 0;
static int linelen = 0;

static int oven_enabled = 0;
static int oven_timeout = 0;

static volatile int wdt = 0;

static const int max_args = 32;

static const char *commands[] = {"read", "on", "off", "version", "config", "help", NULL};

#include <ctype.h>
#include <string.h>

void wdt_expire(void);

void init_wdt(void) {
  noInterrupts();
  TCCR1A = 0;
  /* set the scaler to 256 */
  TCCR1B = (1 << CS12);
  /* enable timer interrupts */
  TIMSK1 |= (1 << TOIE1);
  interrupts();
}

/* postpones the watchdog timer to occur a number of milliseconds from now *
 *  ms is bounded: 0-1048 at F_CPU=16000000. the scaler is set to 256      *
 *  formula for the maximum is: (65535)*(SCALER)*(1000)/(F_CPU)            */
void set_wdt(int ms) {
  /* slow, but accurate */
  unsigned int to = 65535 - ((F_CPU * (uint64_t)ms) + 128000UL)/256000UL;
  /* out of range, so set to maximum timeout, 1048 milliseconds at F_CPU=16000000 */
  if (ms > ((65535ULL)*(256ULL)*(1000ULL))/(F_CPU))
    to = 0;
  noInterrupts();
  wdt = 1;
  TCNT1 = to;
  interrupts();
}

ISR(TIMER1_OVF_vect) {
  if (!wdt)
    return;
  wdt = 0;
  wdt_expire();
}

/* clears the watchdog */
void clear_wdt(void) {
  if (!wdt)
    return;
  noInterrupts();
  wdt = 0;
  interrupts();
}

void setup(void) {
  if (relay_pin >= 0)
    pinMode(relay_pin, OUTPUT);
  if (led_pin >= 0)
    pinMode(led_pin, OUTPUT);
  Serial.begin(baud);
  init_wdt();
}

/* sets the null terminator on the current line and resets the pointer */
static void clear_line_buffer(void) {
  line[linelen] = 0;
  linelen = 0;
}

/* appends a byte to the line buffer *
 * returns -1 for overflow           *
 * returns 0 for sucess              */
static int append_line_byte(int data) {
  /* sanitize input */
  line[linelen++] = data;
  line[linelen] = 0;
  if (linelen < linemax)
    return 0;
  clear_line_buffer();
  return -1;
}

/* reads a byte from the serial interface into the line buffer *
 * returns 0 if the line still has not been terminated yet     *
 * returns 1 if the line has been compleatly read              *
 * returns -1 if the line is too long                          */
int read_line_byte(void) {
  int data = Serial.read();

  /* there is no data available; wait for data to be available */
  if (data < 0)
    return 0;

  /* fsm to read a line with '\r', '\r\n', or '\n' line endings */
  switch (state) {
  case 0:
    switch (data) {
    /* '\r' or start of an '\r\n'; got a line */
    case '\r':
      state = 1;
      clear_line_buffer();
      return 1;
    /* '\n'; got a line */
    case '\n':
      clear_line_buffer();
      return 1;
    /* recv'd a byte of data */
    default:
      return append_line_byte(data);
    }
    break;
  case 1:
    state = 0;
    switch (data) {
    /* was an '\r' and here is another one; got a line */
    case '\r':
      clear_line_buffer();
      return 1;
    /* was an '\r\n' */
    case '\n':
      return 0;
    /* recv'd a byte of data */
    default:
      return append_line_byte(data);
    }
    break;
  }
}

/* returns 0 when a compleate line has not been read;      *
 *           call again once more data has been recv'd     *
 * returns 1 when a compleate line has been read           *
 * returns -1 when a line is too long to fit in the buffer */
int read_line() {
  int ret;
  while (Serial.available()) {
    ret = read_line_byte();
    if (ret != 0)
      return ret;
  }
  return 0;
}

void sensor_readings(void) {
  int now;
  /* therm */
  Serial.print(analogRead(therm_pin));
  Serial.print(" ");
  /* cal */
  if (cal_pin < 0)
    Serial.print("x");
  else
    Serial.print(analogRead(cal_pin));
  /* oven enabled */
  Serial.print(" ");
  if (relay_pin < 0) {
    Serial.println("x x");
  } else {
    Serial.print(oven_enabled);
    /* oven delta time */
    if (oven_enabled) {
      Serial.print(" ");
      now = millis();
      /* this here handles rollover. this function is guarenteed to be *
       *   called on either one or zero rollovers                      */
      if (now > oven_timeout)
        Serial.println(now - oven_timeout);
      else
        Serial.println(oven_timeout - now);
      oven_timeout = now;
    } else {
      Serial.println(" x");
    }
  }
}

void oven_power(int powered) {
  /* relay not attached */
  oven_enabled = !!powered;
  if (relay_pin < 0)
    return;
  digitalWrite(relay_pin, powered ? HIGH : LOW);
  /* led not attached */
  if (led_pin < 0)
    return;
  digitalWrite(led_pin, powered ? HIGH : LOW);
}

void wdt_expire(void) {
  /* timer expired. turn oven off */
  oven_power(0);
}

void on(int duration) {
  /* invalid argument */
  if (duration <= 0 || duration > maxtimeout) {
    Serial.print("on: Invalid argument. expected 0 < duration < ");
    Serial.print(maxtimeout);
    Serial.print(" got: ");
    Serial.println(duration);
    return;
  }

  /* relay not attached, so just echo sensor readings */
  if (relay_pin < 0) {
    sensor_readings();
    return;
  }

  /* set the wdt, reset the oven time integrator, echo readings*/
  set_wdt(duration);
  if (!oven_enabled)
    oven_timeout = millis();
  oven_power(1);
  sensor_readings();
}

/* turn off the relay */
void off(void) {
  /* relay not attached, so just echo sensor readings */
  if (relay_pin < 0) {
    sensor_readings();
    return;
  }

  /* turn relay off, clear wdt, and echo sensor readings */
  oven_power(0);
  clear_wdt();
  sensor_readings();
}

/* print version message */
void version_message(void) {
  Serial.println(oven_version);
}

/* print version message, copyright message, and usage */
void help_message(void) {
  version_message();
  Serial.println(copyright);
  Serial.println("Usage:");
  Serial.println(usage);
  Serial.println();
}

/* print config message */
void config_message(void) {
  /* calibration enabled */
  Serial.print(cal_pin >= 0);
  Serial.print(" ");
  /* calibration voltage */
  Serial.print(cal_pin >= 0 ? cal_volts : "x");
  Serial.print(" ");
  /* relay enabled */
  Serial.print(relay_pin >= 0);
  Serial.print(" ");
  Serial.print(maxtimeout);
  Serial.print(" ");
  Serial.println(linemax);
}

/* print invalid command error message */
void invalid_command(char *command) {
  Serial.print(command);
  Serial.println(": Command not found. Use help for a list of commands.");
}

/* print too many args error message */
void too_many_args(int command, int expected, int got) {
  Serial.print("In command ");
  Serial.print(commands[command]);
  Serial.print(": expected ");
  Serial.print(expected);
  Serial.print(expected == 1 ? " arg, got " : " args got ");
  Serial.print(got);
  Serial.println(got == 1 ? " arg" : " args");
}

/* return the index of the first whitespace char        *
 * or return -1 if there are no whitespace chars in str */
int strspace(const char *str) {
  int i;
  for (i = 0; str[i] != 0; i++) {
    if (isspace(str[i]))
      return i;
  }
  return -1;
}

/* return the index of the first non-whitespace char        *
 * or return -1 if there are no non-whitespace chars in str */
int strnotspace(const char *str) {
  int i;
  for (i = 0; str[i] != 0; i++) {
    if (!isspace(str[i]))
      return i;
  }
  return -1;
}

/* tokenize a string on whitespace. str is the left part                         *
 * returns the right part, or NULL if there is no further non-whitespace strings */
char *strspacesplit(char *str) {
  char *right;
  int i;
  if (str == NULL)
    return NULL;
  i = strspace(str);
  if (i < 0)
    return NULL;
  str[i] = 0;
  right = str + i + 1;
  i = strnotspace(right);
  if (i < 0)
    return NULL;
  right += i;
  return right;
}

/* returns 0 if successful without error            *
 * returns -1 if there are too many args            *
 *   in that case, all of the args after the        *
 *   last are contained in the last element of argv */
int parse_command(char *line, int *argc, char **argv) {
  int maxargs = *argc;
  char *left, *right = line;
  *argc = 0;
  while (right) {
    /* get next arg and split it as arg0<SPLIT HERE>arg1 arg2 ... argn 
     * left = 'arg0', right = 'arg1 arg2 ... argn'*/
    left = right;
    right = strspacesplit(left);
    /* arg0 was empty, skip it */
    if (!left[0])
      continue;
    /* add arg0 to list of args */
    argv[(*argc)++] = left;
    if (*argc >= maxargs - 1) {
      /* reached the maximum number of args*/
      argv[(*argc)++] = right;
      return -1;
    }
  }
  return 0;
}

/* find a string in an array of strings          *
 * haystack need not be sorted                   *
 * returns the first index of needle in haystack *
 *   or -1 if needle is not found in haystack    */
int arrstr(const char **haystack, char *needle) {
  int i;
  for (i = 0; haystack[i]; i++) {
    if (strcmp(haystack[i], needle) == 0)
      return i;
  }
  return -1;
}

void loop(void) {
  int argc = max_args;
  char *argv[max_args] = {0};
  int command;
  int arg;

  /* read a command */
  if (!read_line())
    return;

  /* parse command */
  parse_command(line, &argc, argv);

  if (argc == 0) {
    sensor_readings();
    return;
  }

  command = arrstr(commands, argv[0]);

  /* process command */
  switch (command) {
  /*read*/
  case 0:
    if (argc != 1) {
      too_many_args(command, 0, argc - 1);
      break;
    }
    sensor_readings();
    break;
  /*on*/
  case 1:
    if (argc != 2) {
      too_many_args(command, 1, argc - 1);
      break;
    }
    arg = atoi(argv[1]);
    on(arg);
    break;
  /*off*/
  case 2:
    if (argc != 1) {
      too_many_args(command, 0, argc - 1);
      break;
    }
    off();
    break;
  /*version*/
  case 3:
    if (argc != 1) {
      too_many_args(command, 0, argc - 1);
      break;
    }
    version_message();
    break;
  /*config*/
  case 4:
    if (argc != 1) {
      too_many_args(command, 0, argc - 1);
      break;
    }
    config_message();
    break;
  /*help*/
  case 5:
    help_message();
    break;
  default:
    invalid_command(argv[0]);
    break;
  }
}

