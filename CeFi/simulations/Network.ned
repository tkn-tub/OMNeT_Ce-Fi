//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

package cefi.simulations;


@license(LGPL);
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.node.inet.WirelessHost;
import inet.node.inet.AdhocHost;
import inet.node.wireless.AccessPoint;
import inet.physicallayer.wireless.ieee80211.packetlevel.Ieee80211ScalarRadioMedium;
import inet.visualizer.common.IntegratedVisualizer;
import inet.node.inet.StandardHost;
import inet.node.ethernet.Eth100G;
import inet.visualizer.contract.IIntegratedVisualizer;

// NETWORK USED FOR ALL SIMULATIONS
network adHocNetwork
{
    parameters:
        int nSta = default(2);
        //@statistic[default] (recording = "vector=false; scalar=false");
    submodules:
        configurator: Ipv4NetworkConfigurator {
            config = xmldoc("config.xml");
            @display("p=5,2");
                //config = xml("<config><interface hosts='*' address='145.236.x.x' netmask='255.255.0.0'/></config>");
        }
        radioMedium: Ieee80211ScalarRadioMedium {
            @display("p=10,2");
        }
        visualizer: <default(firstAvailableOrEmpty("IntegratedCanvasVisualizer"))> like IIntegratedVisualizer if typename != "" {
            @display("p=15,2");
        }
        
        adHocAp: AdhocHost {
        @display("i=device/accesspoint"); //p=50,50;
			numWlanInterfaces=2;
        }
        sta[nSta]: AdhocHost {
            @display("r=,,#707070"); //p=0,0,,1,1;
        }

        
}

// Network for simulations with neighboring network
network neighboringNetwork
{
    parameters:
        int nSta = default(2);
        int nLegacySta = default(2);
    submodules:
        configurator: Ipv4NetworkConfigurator {
            config = xmldoc("config.xml");
            @display("p=5,2");
                //config = xml("<config><interface hosts='*' address='145.236.x.x' netmask='255.255.0.0'/></config>");
        }
        radioMedium: Ieee80211ScalarRadioMedium {
            @display("p=10,2");
        }
        visualizer: <default(firstAvailableOrEmpty("IntegratedCanvasVisualizer"))> like IIntegratedVisualizer if typename != "" {
            @display("p=15,2");
        }
        
        adHocAp: AdhocHost {
        @display("i=device/accesspoint");
			numWlanInterfaces=2;
        }
        sta[nSta]: AdhocHost {
            @display("r=,,#707070");
        }
        
        legacyAp: AdhocHost {
        @display("i=device/accesspoint");
        }
        
        legacySta[nSta]: AdhocHost {
        }

        
}