//OVEN PROGRAM
//super simple gpio thing.
//uart at 115200 over usb
//write '1' to enable the oven for the value of timeoff in milliseconds
//write '0' to disable the oven
//write a character, and the arduino responds with the sensor value
//iff the character is '1' or '0'
//arduino writes: base64 encoded varint containing the analog value
//equations:
//voltage from analog value
//V = A * (5.0 / 1023.0)
//temperature (in *C) from voltage
//T = (V - 1.25) / 0.005

//config variables
//thrmocouple amplifier pin
const int analogPin = 5;
//relay output pin
const int relayPin = 2;
//oven on cycle in milliseconds
const int timeoutms = 32;
//timeout pin
const int ledPin = 13;

//internal state variables
static const int timeout_counter = 65536 - ((16000000 * timeoutms) / 256 + 500) / 1000;
static int active = 0;

static void reset_timer()
{
        TCNT1 = timeout_counter;
}

static void set_timer()
{
        active = 1;
        reset_timer();
}

static void clear_timer()
{
        active = 0;
}

static void timeout();

ISR(TIMER1_OVF_vect)
{
        digitalWrite(ledPin, LOW);
        reset_timer();
        if (active)
                timeout();
}

void setup()
{
        pinMode(relayPin, OUTPUT);
        digitalWrite(relayPin, LOW);
        Serial.begin(115200);

        noInterrupts();
        TCCR1A = 0;
        TCCR1B = 0;
        set_timer();
        clear_timer();
        TCCR1B |= (1 << CS12);
        TIMSK1 |= (1 << TOIE1);
        interrupts();
}

//from ec10k
static int pu_en_varint(unsigned long src, void *dest, size_t max)
{
	uint8_t *out = (uint8_t*)dest;
	size_t i;
	//encode varint
	for (i = 0; i < max && src > 0x7F; i++) {
		out[i] = 0x80 | (src & 0x7F);
		src >>= 7;
	}
	//buffer overflow
	if (i >= max)
		return -1;
	out[i] = src;
	return i + 1;
}

const uint8_t enbase64[66] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456798+/=";

static int pu_en_base64(void *src, size_t insz, void *dest, size_t max)
{
	uint8_t *in = (uint8_t*)src;
	uint8_t *out = (uint8_t*)dest;
        int tmp;
        size_t i = 0;
        size_t j = 0;
        
        if (max < ((insz + 2) / 3) * 4)
                return -1;
        
        while (insz - i >= 3) {
                tmp = (in[i] >> 2) & 0x3F;
                out[j++] = enbase64[tmp];
                tmp = ((in[i] << 4) & 0x30) | ((in[i + 1] >> 4) & 0x0F);
                out[j++] = enbase64[tmp];
                tmp = ((in[i + 1] << 2) & 0x3C) | ((in[i + 2] >> 6) & 0x03);
                out[j++] = enbase64[tmp];
                tmp = in[i + 2] & 0x3F;
                out[j++] = enbase64[tmp];
                i += 3;
        }
        
        switch (insz - i) {
        case 0:
                break;
        case 1:
                tmp = (in[i] >> 2) & 0x3F;
                out[j++] = enbase64[tmp];
                tmp = (in[i] << 4) & 0x30;
                out[j++] = enbase64[tmp];
                out[j++] = enbase64[64];
                out[j++] = enbase64[64];
                break;
        case 2:
                tmp = (in[i] >> 2) & 0x3F;
                out[j++] = enbase64[tmp];
                tmp = ((in[i] << 4) & 0x30) | ((in[i + 1] >> 4) & 0x0F);
                out[j++] = enbase64[tmp];
                tmp = (in[i + 1] << 2) & 0x3C;
                out[j++] = enbase64[tmp];
                out[j++] = enbase64[64];
                break;
        }
        
        return j;
}

static void write_response()
{
        int val;
        val = analogRead(analogPin);
        char bufa[16];
        char bufb[16];
        int la = pu_en_varint(val, bufa, 16);
        int lb = pu_en_base64(bufa, 2, bufb, 16);
        bufb[lb] = 0;
        Serial.println(bufb);
}

static void oven_power(int enabled);

void loop()
{
        if (!Serial.available())
                return;
        int cmd = Serial.read();
        if (cmd == '1')
                oven_power(1);
        else
                oven_power(0);
        if (cmd == '1' || cmd == '0')
                write_response();
}

static void timeout()
{
        digitalWrite(ledPin, HIGH);
        oven_power(0);
}

static void oven_power(int enabled)
{
        if (enabled) {
                digitalWrite(relayPin, HIGH);
                set_timer();
        } else {
                digitalWrite(relayPin, LOW);
                clear_timer();
        }
}

