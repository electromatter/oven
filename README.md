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
k = Calibration_Voltage / ADC_Reading
Volts = k * ADC_Reading
```

Temperature (in deg C) from voltage:

```
# May vary, see Termocouple amp datasheet
Temperature = (Volts - 1.25) / 0.005
```

