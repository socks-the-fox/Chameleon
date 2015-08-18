Hello, and thank you for showing your interest in Chameleon!

This is a simple plugin that will sample colors from an image
allowing your skins to seamlessly blend in with the user's
wallpapers with minimal setup!

Chameleon can be used with more than just wallpapers, though!
With certain options, you can pass it any file you chose and
it will do it's best to grab some colors from it.

Currently it supports the following formats directly:

* JPEG
* PNG
* BMP (non-RLE, non-1bpp)
* GIF (just like Rainmeter, first frame only)
* TGA
* Some PSDs
* HDR (radiance rgbE format)
* PIC (Softimage PIC)
* PNM (PPM and PGM binary only)

Anything else, and it'll grab the icon and use the colors
from that. This does mean that it also supports .ico and
.exe icon sampling! Unfortunately Rainmeter doesn't
support displaying icons at the time this was written so
you'll still need to figure that out on your own. Sorry!

Installing Chameleon is a bit different from what Rainmeter
normally does. Since my intent is to have it be used by a
number of skins, I prefer it to be installed directly to
the Rainmeter `Plugins` folder. No point in having 6 copies
of the same DLL all running at once! As such, you'll need
to pick the correct architecture (x64 if you use 64-bit
Rainmeter, x86 if you use 32-bit) and copy it over yourself.

Using Chameleon is really simple! You can set it to
either sample from the desktop or directly from a specific
image. If you set it up to sample from the desktop, it'll
automatically pick the wallpaper for the monitor that the
skin is currently on.

You'll need a parent measure, which tells Chameleon what
image you'll be sampling from, and a set of child measures
that tell Chameleon which colors you're interested in.

For the parent measure you can set either `Desktop` or `File`.

If you set the type as `File` you'll need to specify `Path`
as well. *This can be the output of another measure!*

Optionally you can tell Chameleon to always sample from a file
as an icon using the `ForceIcon` parameter (such as if you
have a .png version of an icon).

Parent measures also offer the super-sneaky bonus of returning
the path of the image it's sampling from!

For the child measures you'll need to specify `Parent` to
tell Chameleon which measure you're getting the colors from,
and `Color` which tells Chameleon which color you're interested
in.

`Color` can be one of:

* `Background1`
* `Foreground1`
* `Background2`
* `Foreground2`

Here's an example:

    [ChameleonDesktop]
    Measure=Plugin
    Plugin=Chameleon
    Type=Desktop

    [DesktopBG1]
    Measure=Plugin
    Plugin=Chameleon
    Parent=ChameleonDesktop
    Color=Background1

In this case, `[ChameleonDesktop]` would return the wallpaper
the skin is currently sitting on, and `[DesktopBG1]` would
return the color Chameleon thinks is the main background.

Note: Currently Chameleon returns this color with the Alpha
set to `FF` to make it a solid opaque color. I may change this
in the future if people want it!

Another case users might be interested in is having a meter
colored based on the NowPlaying album art image. This is
super easy to set up too!

    [ChameleonFile]
    Measure=Plugin
    Plugin=Chameleon
    Type=File
    Path=[NowPlayingAA]

That's it! Everything else will automatically update, as long
as you've set DynamicVariables=1 on the apropriate meters.