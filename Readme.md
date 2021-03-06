Hello, and thank you for showing your interest in Chameleon!

If you like this plugin, please consider
[donating via PayPal](https://www.paypal.me/socksthefox)
to help fund further features!

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
.exe icon sampling! Sampling from icons is a bit roundabout,
check out the FileView plugin for how to get them from a .exe.

If you're downloading this from my site, installing Chameleon
is as simple as installing the example .rmskin. Rainmeter will
stash everything where it's supposed to go for you. If you're
grabbing this from GitHub you'll also need to grab the libChameleon
repo and tell Visual Studio where that's built at. From there,
compilation should be fairly straightforward. Then you just need
to toss the apropriate architecture's DLL into the Rainmeter
plugin folder in your AppData's Roaming profile folder.

If you want to build the libChameleon test app you'll need to
grab it's repo. It only depends on wxWidgets and libChameleon.

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

You can also optionally tell Chameleon whether you want these
values as a hex code or a numeric color code by setting the
`Format` option to `Hex` or `Dec`, respectively.

You might find it useful to only grab colors from a cropped
region of an image. You can do so with the `CropX`, `CropY`,
`CropW`, and `CropH` options. These set the position and
dimension of the rectangle to crop out.

By default, Chameleon will attempt to crop desktop images to
the visible portion based on the monitor's resolution. You
can tell Chameleon to not do this by setting `CropDesktop` to 0.

If you are reading a desktop background and are not applying a
custom cropping, Chameleon will by default try to shuffle the
colors it selected to better fit with the area the skin is
covering. You can disable this by setting `ContextAwareColors`
to 0.

You can also tell Chameleon what area of the screen you want
to use for context aware colors by setting the `ContextX`,
`ContextY`, `ContextW` and `ContextH` values. These values
are relative to the overall Windows desktop layout, so you may
need to use negative coordinates to get the right location.
Rainmeter will tell you the coordinates of a skin in that skin's
right click menu under "Manage skin" if you want a hint.

One last optional option is to tell Chameleon what fallback
colors to use when it just can't sample from an image (such
as with the NowPlaying measure's album art). Right now this
can only be specified in hex, and should not include the
alpha value. You can specify this with `FallbackXYZ` where
`XYZ` is one of `BG1`, `BG2`, `FG1`, or `FG2` for it's
respective color.

Parent measures also offer the super-sneaky bonus of returning
the path of the image it's sampling from!

For the child measures you'll need to specify `Parent` to
tell Chameleon which measure you're getting the colors from,
and `Color` which tells Chameleon which color you're interested
in.

The `Light` and `Dark` colors are just the selected foreground and
background colors sorted by how light or dark they are, respectively.

`Color` can be one of:

* `Background1`
* `Foreground1`
* `Background2`
* `Foreground2`
* `Light1`
* `Light2`
* `Light3`
* `Light4`
* `Dark1`
* `Dark2`
* `Dark3`
* `Dark4`
* `Average` which is the overall average color of the image
* `Luminance` which is not a color, but rather a floating point value
between 0 and 1 indicating the average luminance of the image.

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

Chameleon does not add an alpha value to the color codes!
You'll need to add this yourself to tell Rainmeter how
transparent you want the color. By default, just adding
`FF` after wherever you use the measure value works.

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

Check out the example skin `Socks` to see everything in action!