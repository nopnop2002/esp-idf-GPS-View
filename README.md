# esp-idf-GPS-View
GPS NMEA Viewer for M5Stack.

You can view NMEA message.

# Hardware requirements
GPS module like NEO-6M.

# Wireing
You can use GROVE port as UART RXD.   
SCL of GROVE is GPIO22.   
SDA of GROVE is GPIO21.   
YOu can choice UART RXD GPIO using menuconfig.   
![View-1](https://user-images.githubusercontent.com/6020549/62000281-f84d0880-b10d-11e9-8c1c-895da5ad20bd.JPG)

# Operation
When button A(Left button) is pressed, the display starts.
Press button A(Left button) again to stop the display.
![View-2](https://user-images.githubusercontent.com/6020549/62000282-f84d0880-b10d-11e9-95fb-19ef2ebcbae9.JPG)

When button B(Center button) is pressed, the data to be displayed can be selected.
![View-3](https://user-images.githubusercontent.com/6020549/62000277-f7b47200-b10d-11e9-9263-84c08dd6985b.JPG)
![View-4](https://user-images.githubusercontent.com/6020549/62000278-f7b47200-b10d-11e9-8409-6d61981b655a.JPG)


When button C(Right button) is pressed, Formatted-View is displayed.
![View-5](https://user-images.githubusercontent.com/6020549/62000279-f84d0880-b10d-11e9-8c2d-de1d76d25c33.JPG)
![View-6](https://user-images.githubusercontent.com/6020549/62000280-f84d0880-b10d-11e9-9c73-c38ffd0927dd.JPG)

# Used with u-center
u-center is a very powerful NMEA message analysis tool.   
You can use M5Stack as u-center's GPS Server.   
M5Stack is acting as a router.   
The SSID is myssid.   
Connect to this access point.   

When button C(Right button) is pressed sometime, Network Information is displayed.
![Network-1](https://user-images.githubusercontent.com/6020549/62000293-4a8e2980-b10e-11e9-8248-9b651b23ba53.JPG)

Start u-center and connect to M5Stack.
![u-center-1](https://user-images.githubusercontent.com/6020549/62000222-57aa1900-b10c-11e9-9d7d-aa4d32cdafbe.jpg)
![u-center-2](https://user-images.githubusercontent.com/6020549/62000218-57118280-b10c-11e9-867b-afa20d1caee3.jpg)
![u-center-3](https://user-images.githubusercontent.com/6020549/62000219-57118280-b10c-11e9-84ae-f07103141d4f.JPG)
![u-center-4](https://user-images.githubusercontent.com/6020549/62000220-57118280-b10c-11e9-825f-cf77f2fdcb5b.JPG)
![u-center-5](https://user-images.githubusercontent.com/6020549/62000221-57aa1900-b10c-11e9-833d-1a5a05aa68ae.jpg)

# Configure
You can configure UART GPIO port / WiFi setting using menuconfig.
![Config-1](https://user-images.githubusercontent.com/6020549/62000333-54645c80-b10f-11e9-812a-89df8290bbd9.jpg)

UART GPIO Setting
![Config-2](https://user-images.githubusercontent.com/6020549/62000334-54645c80-b10f-11e9-877f-e715b2b03b09.jpg)

WiFi Setting
![Config-3](https://user-images.githubusercontent.com/6020549/62000335-54645c80-b10f-11e9-9c0f-fc35b390fdce.jpg)

u-center Server Setting
![Config-4](https://user-images.githubusercontent.com/6020549/62000332-54645c80-b10f-11e9-97e5-042499093d7c.jpg)


