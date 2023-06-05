# drm-examples

## plane-test
An exmaple of using a hardware layer (plane) as an overlay.
The RZ/G2L SMARC board was used, but it should work on any RZ/G board. I will also work with RZ/V2L.

1. Building <br>
To build, simple run the build.sh script, then copy the executable **plane-test** to the board.
<pre>
$ ./build.sh
</pre>


2. Stop Weston on the board <br>
To use the DRM interface, you have tostop Weston first.
<pre>
$ killall weston
</pre>
 
3. Display a background on /dev/fb0 <br>
To show the alpha blending, it is helpful if there is a background. <br>
You can use this command to display a background image that comes with weston. <br>
Change the resolution to match your screen size. <br>
<pre>
gst-launch-1.0 -v filesrc location=/usr/share/weston/background.png ! pngdec ! videoconvert ! videoscale ! video/x-raw,width=1280,height=800 ! fbdevsink device=/dev/fb0
</pre>

4. Run the program <br>
Run the program by passing a file name of a png to display. <br>
There are some png files already on the board because of weston.
<pre>
./plane-test /usr/share/icons/hicolor/256x256/apps/gtk3-widget-factory.png
./plane-test /usr/share/icons/hicolor/256x256/apps/gtk3-widget-factory-symbolic.symbolic.png
./plane-test /usr/share/icons/hicolor/256x256/apps/gtk3-demo.png
./plane-test /usr/share/icons/hicolor/256x256/apps/gtk3-demo-symbolic.symbolic.png
</pre>

## Change kernel driver from 1-bit Alpha to 8-bit Alpha
By default, the kernel driver for the VSP is set so the hardware layer will only be 1-bit Alpha, even if your image has 8-bit Alpha.
To use 8-bit Alpha, you have to change the kernel driver to set the MASK_EN bit in regiser VI6_RPF_MSK_CTRL.

Driver File: **drivers/media/platform/vsp1/vsp1_rpf.c**

Make this change at the end of function rpf_configure_stream()
<pre>
-    vsp1_rpf_write(rpf, dlb, VI6_RPF_MSK_CTRL, 0);
+    vsp1_rpf_write(rpf, dlb, VI6_RPF_MSK_CTRL, VI6_RPF_MSK_CTRL_MSK_EN);
</pre>

In Yocto, you can edit this file under work-shared:
* build/tmp/work-shared/smarc-rzg2l/kernel-source/drivers/media/platform/vsp1/vsp1_rpf.c

Then, you can rebuild by entering devshell and typing make.

Enter devshell for the kernel: <br>
<pre>
bitbake linux-renesas -c devshell
</pre>

Rebuild the kernel: <br>
(Hint, the -j8 is to make it faster buy using multiple CPU threads)
<pre>
make -j8
</pre>

Your new Image file will be :
* build/tmp/work/smarc_rzg2l-poky-linux/linux-renesas/5.10.145-cip17+gitAUTOINC+13dea4598e-r1/linux-smarc_rzg2l-standard-build/arch/arm64/boot/Image

You can replace that on your SD Card.
