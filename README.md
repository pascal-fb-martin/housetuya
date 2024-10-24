# HouseTuya
A House web service to control Tuya devices (lights, plugs, valves..)

## Overview

This is a web server to give access to Tuya WiFi devices. This server can sense the current status and control the state of each device. The web API is meant to be compatible with the House control API (e.g. the same web API as [houserelays](https://github.com/pascal-fb-martin/houserelays)).

This service is not really meant to be accessed directly by end-users: these should use [houselights](https://github.com/pascal-fb-martin/houselights) to control Tuya devices.

So far HouseTuya has been tested with the following US models:
* Feit Electric Smart Bulbs (Costco)

This service is not dependent on the Tuya cloud.

## Installation

* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Clone this GitHub repository.
* make
* sudo make install
* Edit /etc/house/Tuya.json (see below)

Otherwise installing [houselights](https://github.com/pascal-fb-martin/houselights) is recommended, but not necessarily on the same computer.

The [housesaga](https://github.com/pascal-fb-martin/housesaga) and [housedepot](https://github.com/pascal-fb-martin/housedepot) projects are also highly recommended, to store logs and configuration files in a more centralized fashion.

## Initial Device Setup

Each device must be setup using the Feit Electric phone app, or the phone app recommended by your specific vendor. The protocol for setting up devices has not been reverse engineered at that time.

## Configuration
The preferred method is to configure the devices from the Configure web page.
The configuration is stored in file /etc/house/tuya.json. A typical example of configuration is:
```
{
    "tuya" : {
        "devices" : [
            {
                "name" : "light1",
                "id" : "xxxxxxxxxx",
                "model" : "xxxxxxxxxx",
                "key" : "xxxxxxxxxx",
                "host" : "192.168.x.y",
                "description" : "a Tuya light"
            },
            {
                "name" : "light2",
                "id" : "xxxxxxxxxx",
                "model" : "xxxxxxxxxx",
                "key" : "xxxxxxxxxx",
                "host" : "192.168.x.z",
                "description" : "another Tuya light"
            }
        ],
        "models": [
            {
                "name" : "vendor model",
                "id" : "xxxxxxxxx",
                "control" : 20, (or some other DP ID value)
            }
        ]
    }
}
```
The "key" field represents the device's local key and is required if the device uses the Tuya protocol version 3.3 or above. Other information (i.e. "host", protocol version, etc.) is retrieved by listening to the devices present on the network.

The devices "model" and "host" fields are saved in the configuration for information only. The application does not use these fields because information is provided by the devices itself during discovery.

This application automatically detects every Tuya device currently active on the network and add any unknown device to the configuration. Only the "id", "model" and "host" fields will be populated when a new device has been detected: the "name", "key" and "description" fields are to be manually populated by the user.

Every Tuya device is controlled by changing the value of data points, and monitored by querying the current value of these data points. Each data point is identified by an index number (starting at 1). The meaning of each data point may change from model to model. You cannot even really rely on a type of device (like a light bulb or switch): each vendor may have changed the mapping. The result is an horrible mess better illustrated by the complexity of the Tuya web UI used to query these device details and properties. (On the plus side, this makes Tuya very flexible and adjustable to every possible type of devices.)

A list of some Tuya models is provided by the [localtuya](https://github.com/rospogrigio/localtuya/wiki/Known-working-and-non-working-devices) project. To obtain a description of the data points for other models, one should check the vendor's documentation, or query the device details in the Device Control section of a Tuya IOT projects (this requires setting up a developer account and creating one dummy IOT project, a.k.a. "Cloud".)

The product key can be used to identify what model the device belongs to.

At this time, this application is only concerned with the data point linked to the on/off command.

A list of known models is included in the configuration. The application comes with a (rather incomplete) initial list, and the user must manually add an entry for each model present on his network.

## Obtaining the Local Key

The local key is not disclosed by the device, obviously. The only way to obtain this key is to "extend" your account (created using the Tuya app) into a free developper account and then create an IOT project (also known as a Cloud project).

Once this was done, the devices initialized through the Tuya app can be listed, and their details revealed. The Tuya web site changes from time to time, so this is a discovery expedition every time.

A free Tuya developer account expires after a month or so, but it can be renewed when needed.

## Tuya Protocol

This section describes the subset of the Tuya LAN protocol that is implemented in HouseTuya. This is not a complete description of the Tuya protocol.

Information about the tuya protocol came thanks to the [tinytuya](https://github.com/jasonacox/tinytuya project) project and its debug output. You can also look at project [tuyapi](https://github.com/codetheweb/tuyapi).

Note that some discrepancies in the protocol used by different device types might be attributed to the firmware version: when identifying a device it is recommended to consider both the type of device and the firmware version.

All commands and responses contain an encrypted JSON structure. The AES encryption is used in ECB mode.

The Tuya protocol uses ports 6666 and 6667 (UDP) and 6668 (TCP).

Each JSON payload is prefixed with an envelop that provides a sequence number, a command code and a payload length. UDP packets do not use sequence number (always 0).

The UDP messages are periodically broadcasted by the device, as an automatic discovery mechanism. Firmware version 3.1 uses port 6666, while firmware version 3.3 or higher uses port 6667. Payloads for version 3.3 and higher are encrypted using AES in ECB mode and a hard coded key.

The status of each device is represented by a set of _data points_. Each data points is identified by a number. See (the TinyTuya project)[https://github.com/jasonacox/tinytuya] for a list of data points for each type of devices. One important detail is that the mapping of points is different for each type of device, so turning a device on and off may be a different data point. The type of the device must be identified before one can interpret the status. Controls (if supported) are implemented by setting the same data point to the desired state.

### How HouseTuya Uses the Tuya Protocol

This section describes what subset and variant of the local Tuya protocol is used by the HouseTuya service.

#### Detect device:

The device message:
```
{'ip': '192.168.1.xxx', 'gwId': '<ID>', 'active': 2, 'ablilty': 0, 'encrypt': True, 'productKey': '<BLTID>', 'version': '3.3'}
```
(ProductKey seems to be the BlueTooth's device ID.)

That message is encrypted for versions 3.3 and higher, using a harcoded key.

#### Query device status:

The request:
```
{"gwId":"<ID>","devId":"<ID>","uid":"<ID>","t":"<TIME>"}
```
(gwId, DevId and uid are the same if the unit houses a single device. The 't' item is the current time.)

The response from the device (a RGB liht bulb in this example):
```
{"dps":{"20":true,"21":"white","22":1000,"23":0,"24":"003702cd034f","25":"000e0d0000000000000000c80000","26":0}}
```
Each "numbered item" (data point) is a specific portion of the device's state: on/off state, color, dimming level, etc.

#### Turn off the light

The request for an RGB light bulb:
```
{"devId":"<ID>","uid":"<ID>","t":"<TIME>","dps":{"20":false}}
```
(DevId and uid are the same if the unit houses a single device. The 't' item is the current time.)

The response:
```
{"dps":{"20":false},"t":"<TIME>"}
```

The request for a dimmer switch:
```
{"devId":"<ID>","uid":"<ID>","t":"<TIME>","dps":{"1":false}}
```

The response:
```
TBD
```

#### Turn on the light

The request for an RGB light bulb:
```
{"devId":"<ID>","uid":"<ID>","t":"<TIME>","dps":{"20":true}}
```
(DevId and uid are the same if the unit houses a single device. The 't' item is the current time.)

Note: item t may not need to be quoted (see response).

The response:
```
{"dps":{"20":true},"t":<TIME>}
```

The request for a dimmer switch:
```
{"devId":"<ID>","uid":"<ID>","t":"<TIME>","dps":{"1":true}}
```

The response:
```
TBD
```

