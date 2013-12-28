Softimage Krakatoa SR Plugin
============================

This a simple plugin for Softimage that exposes the Krakatoa SR particle renderer from Thinkbox Software as a renderer in Softimage. It is a very basic implementation at this point and is intended as a starting point rather than a fully functional production plugin.

This plugin has NOT been tested in production is just intended as a test/example.

You will need a valid Krakatoa "Render" license from [Thinkbox](http://www.thinkboxsoftware.com/) for this plugin to work.

To build you will also need the Krakatoa SR C++ SDK which can be downloaded from the [Thinkbox website](http://www.thinkboxsoftware.com/krakatoa-sr-downloads/)

Pull requests welcomed. 

##### Features

- Renders ICE point clouds with Krakatoa from within Softimage as a native C++ renderer plug-in
- Custom ICE channels mapped to Krakatoa channels (see below)
- Region Render tool support
- Light Groups can be used to control with lights are used by Krakatoa
- Occlusion mesh support
- Multi-channel EXR output support

##### The Following ICE Channels are mapped for Krakatoa if they exist and are not being optimized away by ICE

- PointPosition
- Color
- Density
- Lighting
- MBlurTime
- Absorption
- Emission
- PointNormal
- Tangent
- PointVelocity
- Eccentricity
- PhaseEccentricity
- SpecularPower
- SpecularLevel
- DiffuseLevel
- GlintGlossiness
- GlintLevel
- GlintSize
- Specular2Glossiness
- Specular2Level
- Specular2Shift
- SpecularGlossiness
- SpecularShift








