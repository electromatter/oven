Oven
====

Simple arduino program to control my toaster oven.
Licensed under the MIT License.
Written for my arduino micro, but is very portable.
See oven.ino for the serial protocol.


Equations
--

Voltage from analog value:

```
# ADC Reference voltage
# on the arduino its the same is the line voltage
# usually in the range: 4.5-5.1
ADC_Ref = 5.0
# Conversion constant
k = (ADC_Ref / 1023.)
Volts = k * ADC_Reading
```

Voltage from analog value, with calibration:

```
# This calulates the conversion value from a voltage
# reference, but still uses the line voltage as the
# adc reference
# Calibration_Voltage is read from the config and is
# usually between 1.0-4.0 Volts
Calibration_Voltage = 3.3
Volts = ADC_Reading * Calibration_Voltage / Calibration_Reading
```

Temperature (in deg C) from voltage:

```
# May vary, see Termocouple amp datasheet
Temperature = (Volts - 1.25) / 0.005
```

Notes
----
Using the calibration on a seperate pin may introduce noise ~N(1/x).
It may be better to put the reference on the adc reference pin

