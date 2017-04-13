This is the AISTARVISION MIPI Adapter,V2.0! It's updated from V1.0,with the goal of two CSI2 portS supporting.Still,it's only for 96boards CE Edition.Our goal is to make Dragonboard support multiple CMOS image sensors,include SOC sensor and raw data sensor.Currently we have OV5645 and OV5640 module supported.

![img_1920](https://cloud.githubusercontent.com/assets/22780075/25014460/b3ec0d7c-202c-11e7-958e-fe873ddf64c9.JPG)

a)Build and Flash

For Linaro 16.09 release,refer to https://builds.96boards.org/releases/dragonboard410c/linaro/debian/16.09        Note that,the original Linaro 16.09 release removed camera nodes from device tree.To add camera support,use the updated apq8016-sbc.dtsi in this GitHub：Pre-built/Debian-16.09/ to replace the original one,then build.

If you use Linaro 16.06 release,then don't need do anything else,the default build would support our camera.Linaro 16.06 release:https://builds.96boards.org/releases/dragonboard410c/linaro/debian/16.06/

b)Set up your camera
 
   Fisrt you need apt-get update then install v4l-utils,then cofigure the media device,for CSI0:
  
media-ctl -d /dev/media1 -l '"msm_csiphy0":1->"msm_csid0":0[1],"msm_csid0":1->"msm_ispif0":0[1],"msm_ispif0":1->"msm_vfe0_rdi0":0[1]'

media-ctl -d /dev/media1 -V '"ov5645 1-0076":0[fmt:UYVY2X8/1920x1080],"msm_csiphy0":0[fmt:UYVY2X8/1920x1080],"msm_csid0":0[fmt:UYVY2X8/1920x1080],"msm_ispif0":0[fmt:UYVY2X8/1920x1080],"msm_vfe0_rdi0":0[fmt:UYVY2X8/1920x1080]'

gst-launch-1.0 v4l2src ! glimagesink

For CSI1:

media-ctl -d /dev/media1 -l '"msm_csiphy1":1->"msm_csid1":0[1],"msm_csid1":1->"msm_ispif1":0[1],"msm_ispif1":1->"msm_vfe0_rdi1":0[1]'

media-ctl -d /dev/media1 -V '"ov5645 1-0074":0[fmt:UYVY2X8/1920x1080],"msm_csiphy1":0[fmt:UYVY2X8/1920x1080],"msm_csid1":0[fmt:UYVY2X8/1920x1080],"msm_ispif1":0[fmt:UYVY2X8/1920x1080],"msm_vfe0_rdi1":0[fmt:UYVY2X8/1920x1080]'

gst-launch-1.0 v4l2src device=/dev/video1 ! glimagesink


c)Get the hardware

AISTARVISION MIPI adapter:http://www.ebay.com/itm/-/252845198835?ssPageName=STRK:MESE:IT

OV5645 AUTO FOCUS Module:http://www.ebay.com/itm/OV5645-auto-focus-module-/252844782263?hash=item3adeb932b7:g:YXUAAOSwsW9Y30ik

CAMERA Bundle:http://www.ebay.com/itm/96boards-MIPI-Adapter-with-OV5645-auto-focus-module-/252844782266?hash=item3adeb932ba:g:8z4AAOSwA29Y30WD

1>Single camera support
One OV5645/OV5640 auto focus module
![img_1948](https://cloud.githubusercontent.com/assets/22780075/24592272/728c99a8-17c8-11e7-880a-757cf84d0f45.jpg)

2>Dual camera support

Two OV5645/OV5640 supported by AISTARVISION MIPI Adapter V2.0
![img_1966](https://cloud.githubusercontent.com/assets/22780075/24592212/ca0ae0e6-17c7-11e7-9c82-a632147f91d1.jpg)



