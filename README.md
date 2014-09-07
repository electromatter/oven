OVEN
====
Super simple gpio thing for toaster-oven reflow-oven.


uart at 115200 over usb


write '1' to enable the oven for the value of timeoff in milliseconds
write '0' to disable the oven
arduino responds with: base64 encoded varint containing the analog value


equations:
voltage from analog value
`V = A * (5.0 / 1023.0)`
temperature (in *C) from voltage
`T = (V - 1.25) / 0.005`
