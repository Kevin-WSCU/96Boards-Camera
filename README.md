This is the AISTARVISION MIPI Adapter,V2.0! It's updated from V1.0,with the goal of two CSI2 port support.Still,it's only for 96boards CE Edition.Our goal is to make 96Boards support variety of image sensors,include SOC sensor and raw bayer sensor.

P.S. AISTARVISION has joined DeltaVision team which has the same passion and mission to bring camera everywhere,making your project smarter than ever before.Take a look at our site:www.deltavision.io for more interesting cameras

Sensor sample drivers available in Debian 16.09 for Dragonboard410C.

https://github.com/Kevin-WSCU/Debian-169.git

Android sensor drivers:Pre-built/Android_5.1.1/ 

Support list(Default on Debian):

1)OV5645:2592*1944@15fps,1920*1080@30fps,1280*960@30fps

2)OV5640:2592*1944@15fps,1920*1080@30fps,1280*960@30fps

3)OV7251:640*480@100fps

4)MT9V024 with Toshiba MIPI Bridge:752*480@60fps

5)IMX185:1080p@60fps

6)AP0202+AR0230:1080p@30fps,WDR

7)OV13850 supported by Android

8)OV8865 supported by Android

9)Raspberry PI camera support(OV5647 and IMX219) by Android

See our ebay store for available items:
https://www.ebay.com/sch/aiwills-8/m.html?item=253133569033&rt=nc&_trksid=p2047675.l2562



Build and Flash

  Refer to https://builds.96boards.org/releases/dragonboard410c/linaro/debian/  for more building information      Note that official release removed camera nodes from device tree.We provide pre-built binaries for you to test cameras boards and also device tree file if you want to build driver by yourself

P.S. For Linaro 16.06 release,official build supports our camera OV5645,it's enabled in device tree by default




