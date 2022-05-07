# esp-idf-GPS-View
GPS NMEA Viewer for M5Stack.   
You can view NMEA message.

# Hardware requirements
- M5Stack   
- GPS module like NEO-6M   

# Software requirements
- esp-idf ver4.1 or later   
 Because uart_enable_pattern_det_intr() has been changed to uart_enable_pattern_det_baud_intr().

# Wireing to GPS
You can use GROVE port as UART Connection.   

|GPS||M5Stack|
|:-:|:-:|:-:|
|VCC|--|GROVE(Red Line)|
|GND|--|GROVE(Black Line)|
|TXD|--|GROVE(Yellow Line or White Line)(*1)|

(*1)   
GROVE(Yellow Line) is GPIO22.   
GROVE(White Line) is GPIO21.   

You can choice UART RXD GPIO using menuconfig.   
![View-1](https://user-images.githubusercontent.com/6020549/62000281-f84d0880-b10d-11e9-8c1c-895da5ad20bd.JPG)


# Installation
```
git clone https://github.com/nopnop2002/esp-idf-GPS-View
cd esp-idf-GPS-View
idf.py menuconfig
idf.py flash
```


# Configuration
You can configure UART GPIO port / WiFi setting using menuconfig.

![cinfig-top](https://user-images.githubusercontent.com/6020549/167273686-1fb3f8e5-8cc2-4e98-b479-8086ed1448bc.jpg)
![config-app](https://user-images.githubusercontent.com/6020549/167273688-111e88f1-4bae-4090-802f-fedfbae0281a.jpg)

## GPS Uart Setting

![config-gps](https://user-images.githubusercontent.com/6020549/167273706-92f0a7ea-d43d-4356-8e5d-2afbdf3bea19.jpg)

The GPS module has TX and RX, but this project uses only TX.   

## WiFi Setting

- For AP Mode

![config-wifi-1](https://user-images.githubusercontent.com/6020549/167273811-072d0e0b-2495-4327-8d71-a3e66f0839c6.jpg)

- For ST Mode

![config-wifi-2](https://user-images.githubusercontent.com/6020549/167273739-88195db2-e72c-4c37-b79b-f00c9bdfd6b6.jpg)

## TCP Server Setting

![cinfig-tcp](https://user-images.githubusercontent.com/6020549/167273826-8c633493-f784-4aed-b96a-bd4b0ee4126d.jpg)

This project can be used as a GPS Server for u-center.   


# Operation
When button A(Left button) is pressed, the display starts.   
Press button A(Left button) again to stop the display.   
![View-2](https://user-images.githubusercontent.com/6020549/62000282-f84d0880-b10d-11e9-95fb-19ef2ebcbae9.JPG)

When button B(Middle button) is pressed, a single message is displayed.   
![View-3](https://user-images.githubusercontent.com/6020549/62000277-f7b47200-b10d-11e9-9263-84c08dd6985b.JPG)
![View-4](https://user-images.githubusercontent.com/6020549/62000278-f7b47200-b10d-11e9-8409-6d61981b655a.JPG)


When button C(Right button) is pressed, Formatted-View is displayed.   
![View-5](https://user-images.githubusercontent.com/6020549/62000279-f84d0880-b10d-11e9-8c2d-de1d76d25c33.JPG)
![View-6](https://user-images.githubusercontent.com/6020549/62000280-f84d0880-b10d-11e9-9c73-c38ffd0927dd.JPG)


# GPS Server for u-center
[u-center](https://www.u-blox.com/en/product/u-center) is a very powerful NMEA message analysis tool.   
You can use M5Stack as u-center's GPS Server.   
M5Stack acts as a router.   
The SSID of M5Stack is 'myssid'.   
Connect to this access point.   

When button C(Right button) is pressed sometime, Network Information is displayed.

![Network-1](https://user-images.githubusercontent.com/6020549/62000293-4a8e2980-b10e-11e9-8248-9b651b23ba53.JPG)

Start u-center and connect to M5Stack.   
Default port is 5000.   

![u-center-1](https://user-images.githubusercontent.com/6020549/62000222-57aa1900-b10c-11e9-9d7d-aa4d32cdafbe.jpg)
![u-center-2](https://user-images.githubusercontent.com/6020549/62000218-57118280-b10c-11e9-867b-afa20d1caee3.jpg)
![u-center-3](https://user-images.githubusercontent.com/6020549/62000219-57118280-b10c-11e9-84ae-f07103141d4f.JPG)
![u-center-4](https://user-images.githubusercontent.com/6020549/62000220-57118280-b10c-11e9-825f-cf77f2fdcb5b.JPG)
![u-center-5](https://user-images.githubusercontent.com/6020549/62000221-57aa1900-b10c-11e9-833d-1a5a05aa68ae.jpg)


# Without UI Version

https://github.com/nopnop2002/esp-idf-GPS-Repeater

