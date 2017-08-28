This is the AISTARVISION MIPI Adapter,V2.0! It's updated from V1.0,with the goal of two CSI2 port support.Still,it's only for 96boards CE Edition.Our goal is to make Dragonboard support multiple CMOS image sensors,include SOC sensor and raw bayer sensor.

Sensor support list:

1)OV5645:2592*1944@15fps,1920*1080@30fps,1280*960@30fps

2)OV5640:2592*1944@15fps,1920*1080@30fps,1280*960@30fps

3)OV7251:640*480@100fps

4)MT9V024 with Toshiba MIPI Bridge:752*480@60fps

5)IMX185:1080p@60fps

6)AP0202+AR0230:1080p@30fps,WDR

Driver under development:

1) IMX274
  4K@30fps,ETA by 7/22/2017~8/30/2017,delay due to camera board
  
2) OV5648/OV8865/OV13850 for Android,ETA,by the end of August

MIPI Adapter further update:V2.1,ETA,by the end of August

1) Flexibility
2) Mechinical(mounting hole)
  
  


![img_1920](https://cloud.githubusercontent.com/assets/22780075/25014460/b3ec0d7c-202c-11e7-958e-fe873ddf64c9.JPG)

a)Build and Flash

For Linaro 16.09 release,refer to https://builds.96boards.org/releases/dragonboard410c/linaro/debian/16.09        Note that,the original Linaro 16.09 release removed camera nodes from device tree.To add camera support,use the updated apq8016-sbc.dtsi in this GitHub：Pre-built/Debian-16.09/ to replace the original one,then build.

If you use Linaro 16.06 release,then don't need do anything else,the default build would support our camera.Linaro 16.06 release:https://builds.96boards.org/releases/dragonboard410c/linaro/debian/16.06/

b)Get the hardware

CAMERA KIT Bundle:http://www.ebay.com/itm/96Boards-MIPI-Adapter-with-OV5645-auto-focus-module-/252956476095?ssPageName=STRK:MESE:IT

AISTARVISION MIPI adapter ONLY:http://www.ebay.com/itm/96Boards-MIPI-Adapter-/252900099832?hash=item3ae20546f8:g:w1MAAOSw03lY5Aaf

STEREO CAMERA KIT:http://www.ebay.com/itm/-/253009646380?ssPageName=STRK:MESE:IT

OV5645 AUTO FOCUS Module:http://www.ebay.com/itm/OV5645-auto-focus-module-/252956491650?hash=item3ae561bf82:g:YXUAAOSwsW9Y30ik

1>Single camera support
One OV5645/OV5640 auto focus module
![img_1948](https://cloud.githubusercontent.com/assets/22780075/24592272/728c99a8-17c8-11e7-880a-757cf84d0f45.jpg)

2>Dual camera support

Two OV5645/OV5640 supported by AISTARVISION MIPI Adapter V2.0
![img_1966](https://cloud.githubusercontent.com/assets/22780075/24592212/ca0ae0e6-17c7-11e7-9c82-a632147f91d1.jpg)
![img_2600](https://user-images.githubusercontent.com/22780075/29755167-8176956a-8b48-11e7-97b3-897652bd19ed.JPG)

3>Camera boards

a)OV5645/OV5640 camera board
![img_2603](https://user-images.githubusercontent.com/22780075/29755205-53cf2e82-8b49-11e7-94a3-ab23203a82e1.JPG)

b)OV7251 camera board



