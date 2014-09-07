OVEN
====
Super simple gpio thing for toaster-oven reflow-oven using Arduino Micro.

UART at 115200 over usb

Write '1' to enable the oven for the value of timeoff in milliseconds

Write '0' to disable the oven

Arduino responds with: base64 encoded varint containing the analog value


Equations:
--

Voltage from analog value:


````
V = A * (5.0 / 1023.0)
````


Temperature (in *C) from voltage:


````
T = (V - 1.25) / 0.005
````
