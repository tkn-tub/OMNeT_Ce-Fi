# Ce-Fi: Fronthaul-Delay Affected Centralized Wi-Fi in OMNeTpp

## User Guide

### Prerequisites
To use the [proposed Ce-Fi](https://www.tkn.tu-berlin.de/bib/schmitz-heinen2025ce-fi/schmitz-heinen2025ce-fi.pdf) solution or to just experiment with a Fronthaul (FH)-affected Wi-Fi system, you need a current installation of [OMNeT++](https://omnetpp.org/) [1]. 

All testing has been performed on OMNeT++ Version 6.1.

### Installing OMNeT_Ce-Fi
To use the changes provided in this repository, clone it to the "omnetpp-6.x/samples" directory of your OMNeT++ installation.

```bash
git clone git@github.com:tkn-tub/OMNeT_Ce-Fi.git
```
_Hint:_ The repository contains a modified version of INET4.5. Thus, no other INET4.5 folder should be present in your samples directory.  

### Using a FH-affected Wi-Fi system
Changes to the INET4.5 have been performed to allow for the appropriate simulation of a FH-affected Wi-Fi system. 

These changes are mainly done to the `Ieee80211Mac` and `Rx` modules.

Multiple modules use the `fhDelay` parameter to correctly specify the FH-delay for a given device. 

Thus, the device that is supposed to have the Fronthaul delay, lets call it `fhAccessPoint` will require a configuration of the FH-delay with the unit seconds in the `omnetpp.ini` file as follow: 

```.ini
**.fhAccessPoint.**.fhDelay = ${fhDelay =10us}
```

With that done, the behavior of standard IEEE 802.11 with a FH-delay between the MAC-layer and the radio is simulated. 

### Using Ce-Fi 

To use Ce-Fi, there are more changes to be done. 

The specific required changes are as followed: 

(The above mentioned changes to add the FH are required as well).


**Timeout Adjustments**
First of all, the timeouts of acknowledgements have to be adjusted for the system to work properly to adjust for the additional time required for each message to pass through the fronthaul. This has to be done for all Devices inside the FH-affected basic-service-set (BSS). 

This is done like this e.g.:
``` .ini
% For Ack-Timeout
**.staFhBss[*].**.ackTimeout = 75us + 2*${fhDelay}
**.fhAccessPoint.**.ackTimeout = 75us + 2*${fhDelay}

% For ctsTimeout
**.staFhBss[*].**.ctsTimeout = 75us + 2*${fhDelay}
**.fhAccessPoint.**.ctsTimeout = 75us + 2*${fhDelay}

% For blockAckTimeout
**.staFhBss[*].**.blockAckTimeout = 75us + 2*${fhDelay}
**.fhAccessPoint.**.blockAckTimeout = 75us + 2*${fhDelay}
```

**Channel Access Mechanism**

Then, a channel access mechanism has to be chosen. 

The channel access mechanism needs to be the same for each device in the FH-affected BSS and is set as follows: 

``` .ini
**.staFhBss[*].**.fhChannelAccessMechanism = ${channelAccess = "standard", "cefi", "okamoto"}
**.fhAccessPoint.**.fhChannelAccessMechanism = ${channelAccess}
```

`"standard"` describes the standard 802.11 DCF channel access behavior and is set by default. Yet, to perform parameter studies, it might be helpful to include it as well.

`"okamoto"` sets the channel access mechanism to the proposal by Okamoto *et al.* [2] mechanism, where a combination of timeout and NAV extensions as well as (as an AP) responding SIFS after transmitting an ACK is used.

`"cefi"` extends the channel access mechanism by Okamoto *et al.* to include a Forced Transmission Mechanism (FT) to allow for communication even in low uplink scenarios, which are required for the FH-affected AP to piggyback its data after transmitting ACK frames.

**NAV Extension**
The amount to which the NAVs need to be adjusted (as required by `"okamoto"` and `"cefi"`) is not set automatically in the simulation. For the sake of simplicity and adjustability, the NAV extension parameter is set in the `omnetpp.ini` file and is supposed to be equal to the expected FH-delay.

As only the stations need to extend the NAV, the change is performed only for these:

```.ini
**.staFhBss[*].**.dcf.navExtension = ${fhDelay}
```

**Further Settings**
For the FT mechanism to work properly, the stations need to know, which address is allocated to the AP:
``` .ini
**.wlan[*].mac.dcf.apAddress = "0A-AA-00-00-00-01"
```
*Hint:* The address here is (for simplicity reasons) set to the first address distributed by the simulator. To match the AP, we recommend to define the AP node first in the `Network.ned`.

Also, as (again for simplicity) I am using adHocHosts in the simulation, the AP must be informed that it is indeed an AP. This is done to control some procedures in the background.

```.ini
**.fhAccessPoint.**.isFronthaulAffectedAp = true
```

At last, there is also a further parameter to configure. The parameter alpha allows to configure with what probability the AP will transmit data after sending an ACK. 1 means after every ACK, 0.5 means a chance of 50% to do so etc.

``` .ini 
**.fhAccessPoint.**.alpha = 1
```




## References
[1]  Simon Wolfgang Schmitz-Heinen, Anatolij Zubow, Christos Laskos and Falko Dressler, "Ce-Fi: Centralized Wi-Fi over Packet-Fronthaul," Proceedings of 50th IEEE Conference on Local Computer Networks (LCN 2025), Sydney, Australia, October 2025

[2] Y. Okamoto, M. Morikura, T. Nishio, K. Yamamoto, F. Nuno, and T. Sugiyama, “Throughput and QoS Improvement of Wireless LAN System Employing Radio Over Fiber,” in 2014 XXXIth URSI General Assembly and Scientific Symposium (URSI GASS), Beijing, China: IEEE, Jul. 2014, pp. 1–4
