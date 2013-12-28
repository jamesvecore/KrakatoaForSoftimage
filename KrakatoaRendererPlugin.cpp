// The MIT License (MIT)
// 
// Copyright (c) 2013 James Vecore
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Created by James Vecore
// james.vecore@gmail.com


#include <Windows.h>

#include <xsi_application.h>
#include <xsi_context.h>
#include <xsi_model.h>
#include <xsi_pluginregistrar.h>
#include <xsi_status.h>
#include <xsi_renderercontext.h>
#include <xsi_renderer.h>
#include <xsi_framebuffer.h>
#include <xsi_primitive.h>
#include <xsi_camera.h>
#include <xsi_geometry.h>
#include <xsi_point.h>
#include <xsi_iceattribute.h>
#include <xsi_iceattributedataarray.h>
#include <xsi_vector2f.h>
#include <xsi_vector3f.h>
#include <xsi_vector4f.h>
#include <xsi_quaternionf.h>
#include <xsi_color4f.h>
#include <xsi_kinematics.h>
#include <xsi_light.h>
#include <xsi_triangle.h>
#include <xsi_vector3.h>
#include <xsi_group.h>
#include <xsi_project.h>
#include <xsi_scene.h>
#include <xsi_geometryaccessor.h>
#include <xsi_polygonmesh.h>
#include <xsi_utils.h>

#include <krakatoasr_progress.hpp>
#include <krakatoasr_renderer.hpp>
#include <krakatoasr_light.hpp>

#include <string>
#include <vector>
#include <map>
#include <algorithm>

using namespace XSI; 
using namespace krakatoasr;
using namespace std;

// this could be changed from multiple threads so make sure its marked as volatile
// should probably use a condition variable or event, but don't want to get into OS specific issues
static volatile bool g_shouldAbort = false; 


class SIProgressLogger : public progress_logger_interface 
{
    RendererContext& ctx;
    CString curTitle;
public:
    SIProgressLogger(RendererContext& ctx) : ctx(ctx)
    {
    
    }
    virtual ~SIProgressLogger() {}
	virtual void set_title( const char* title )
    {
        curTitle = title;
        ctx.ProgressUpdate(curTitle, curTitle, 0);
    }

	virtual void set_progress( float progress )
    {
        ctx.ProgressUpdate(curTitle, curTitle, (int)(progress * 100.0f));
    }
};

class SICancelRenderInterface : public cancel_render_interface 
{
public:
    virtual ~SICancelRenderInterface() {}
	virtual bool is_cancelled()
    {
		return g_shouldAbort;
    }
};

struct RGBA
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

// Krakatoa works in linear space, need to convert to sRGB to show in viewport
inline unsigned char linearToSRGB(float v)
{
	if (v <= 0.0f)
		return 0;
	if (v >= 1.0f)
		return 255;
	if (v <= 0.0031308f)
		return  (unsigned char)((12.92f * v * 255.0f) + 0.5f);
	return (unsigned char)( ( ( 1.055f * pow(v, 1.0f / 2.4f ) ) - 0.055f ) * 255.0f + 0.5f);
}

/*
KaraktoaSR only gives updates to the frame buffer all at once (full image) even if its not all filled out
this fragment will update either the full image or just the crop window
*/
class KrakFragment : public RendererImageFragment
{
private:
    unsigned int krakWidth;
    unsigned int krakHeight;
	unsigned int fragWidth;
    unsigned int fragHeight;
    unsigned int offsetX; // offset from the left
    unsigned int offsetY; // offset from the bottom
    const frame_buffer_pixel_data* pLastData;
public:

    KrakFragment(unsigned int fragWidth, unsigned int fragHeight, unsigned int offsetX, unsigned int offsetY) : 
        fragWidth(fragWidth), 
        fragHeight(fragHeight),
        krakWidth(0),
        krakHeight(0),
        offsetX(offsetX),
        offsetY(offsetY),
        pLastData(0)
    {
    }

    virtual ~KrakFragment()
    {
    }

    void Update( int width, int height, const frame_buffer_pixel_data* data )
    {
        this->krakWidth = width;
        this->krakHeight = height;

        pLastData = data;
    }

    unsigned int GetOffsetX() const { return offsetX; }
	unsigned int GetOffsetY() const { return offsetY; }
	unsigned int GetWidth()   const { return fragWidth;   }
	unsigned int GetHeight()  const { return fragHeight;  }
    
	bool GetScanlineRGBA( unsigned int in_uiRow, siImageBitDepth in_eBitDepth, unsigned char *out_pScanline ) const

	{
        // in_uiRow :  the scanline counting from the bottom
        // out_pScanline : RGBA buffer of type in_eBitDepth (use little endian for values larger than 1 byte)  (little endian is default for x86) large enough to fit the a scan line of width = this->width

        if (pLastData == 0)
            return false;

        // default when showing to user is the siImageBitDepthInteger8 RGBA packed into a single integer
        // we need to convert krakatoa's floating point pixels into rgba
		RGBA* pScanline = (RGBA*)out_pScanline;

        // assume kraktoa data is layed out from top-left to bottom right
        // softimage references data from bottom left up

        unsigned int fullOffsetY = offsetY + in_uiRow; // full offset for this scanline from the bottom
        unsigned int krakY       = fullOffsetY;
        unsigned int krakXStart  = offsetX; // x is the same for krak and frag

        const frame_buffer_pixel_data* pKrakPixel;



        unsigned int kx = 0;
        for( unsigned int i = 0; i < fragWidth; i++ )
        {
            kx = krakXStart + i;
            
            pKrakPixel = &this->pLastData[ kx + krakY*krakWidth];

            // do a basic linear to sRGB conversion here...
            pScanline[i].r = linearToSRGB(pKrakPixel->r);
            pScanline[i].g = linearToSRGB(pKrakPixel->g);
            pScanline[i].b = linearToSRGB(pKrakPixel->b);
            pScanline[i].a = (unsigned char)(((pKrakPixel->r_alpha + pKrakPixel->g_alpha + pKrakPixel->b_alpha) / 3.0f) * 255.0f); // take the average of the alphas for now...
        }

		return true;
	}
};

class SIFrameBufferInterface : public frame_buffer_interface 
{
    RendererContext& ctx;
    KrakFragment* pFrag;
public:
    SIFrameBufferInterface(RendererContext& ctx, int cropWidth, int cropHeight, int offsetX, int offsetY) : ctx(ctx)
    {
        pFrag = new KrakFragment(cropWidth, cropHeight, offsetX, offsetY);

    }
    virtual ~SIFrameBufferInterface()
    {
        if (pFrag != 0)
        {
            delete pFrag;
            pFrag = 0;
        }
    }
	/*
	 * Called periodically by the renderer and provides the semi-complete rendered image to the user.
	 * It is also called after the renderer has completed with the final rendered image.
	 * @param width The width of the rendered image.
	 * @param height The height of the rendered image.
	 * @param data An array of length width*height with color and alpha components making up the rendered image.
	 */
	virtual void set_frame_buffer( int width, int height, const frame_buffer_pixel_data* data )
    {
        // assume this is only called from a single thread for now...
        pFrag->Update(width, height, data);
        // update softimage
        ctx.NewFragment(*pFrag);
    }
};

class SINoSave : public render_save_interface 
{
public:
    virtual ~SINoSave(){}
    virtual void render_save_interface::save_render_data(int width,int height,int imageCount,const output_type_t* listOfTypes,const frame_buffer_pixel_data* const* listOfImages)
    {
        // do nothing!    
    }
};

class SIPointCloudParticleStream : public particle_stream_interface
{
protected:
	static map<string, string> channelNameMappings;

    Geometry& geometry;
    vector<ICEAttribute> attributes;
    vector<channel_data> channels;
    krakatoasr::INT64 particleCount;
    krakatoasr::INT64 particleIndex;
    
    vector<CBaseICEAttributeDataArray*> dataArrays;
    
public:
    SIPointCloudParticleStream(Geometry& geometry) : geometry(geometry), particleCount(-1), particleIndex(0)
    {
		if (channelNameMappings.size() == 0) // only happens the first time
		{
			channelNameMappings["PointPosition"]       = "Position";
			channelNameMappings["Color"]               = "Color";
			channelNameMappings["Density"]             = "Density";    // 1 float
			channelNameMappings["Lighting"]            = "Lighting";   // 3 floats
			channelNameMappings["MBlurTime"]           = "MBlurTime";  // 1 float
			channelNameMappings["Absorption"]          = "Absorption"; // 3 floats // only used if absorbtion channel is on
			channelNameMappings["Emission"]            = "Emission";   // 3 floats // only used if emission is on
			channelNameMappings["PointNormal"]         = "Normal";     // used by phong shader
			channelNameMappings["Tangent"]             = "Tangent";    // used by Marschner Hair shader
			channelNameMappings["PointVelocity"]       = "Velocity";
			// theses are used by shaders:
			channelNameMappings["Eccentricity"]        = "Eccentricity";         // used by henyey_greenstein, schlick 
			channelNameMappings["PhaseEccentricity"]   = "Eccentricity";         // used by henyey_greenstein, schlick  (support both names, assume the user is going to fill one)
			channelNameMappings["SpecularPower"]       = "SpecularPower";        // used by phong, kajiya_kay shader
			channelNameMappings["SpecularLevel"]       = "SpecularLevel";        // used by phong, kajiya_kay shader
			channelNameMappings["DiffuseLevel"]        = "DiffuseLevel";         // used by marschner
			channelNameMappings["GlintGlossiness"]     = "GlintGlossiness";      // used by marschner
			channelNameMappings["GlintLevel"]          = "GlintLevel";           // used by marschner
			channelNameMappings["GlintSize"]           = "GlintSize";            // used by marschner
			channelNameMappings["Specular2Glossiness"] = "Specular2Glossiness";  // used by marschner
			channelNameMappings["Specular2Level"]      = "Specular2Level";       // used by marschner
			channelNameMappings["Specular2Shift"]      = "Specular2Shift";       // used by marschner
			channelNameMappings["SpecularGlossiness"]  = "SpecularGlossiness";   // used by marschner
			channelNameMappings["SpecularShift"]       = "SpecularShift";        // used by marschner
      
			/*      
			known 3dsmax channels the renderer doesn't actually use but might be useful on .prt export?
			channelNameMappings["Mapping2"]      = "Mapping2";
			channelNameMappings["Mapping3"]      = "Mapping3";
			channelNameMappings["Mapping4"]      = "Mapping4";
			channelNameMappings["Mapping5"]      = "Mapping5";
			channelNameMappings["Mapping6"]      = "Mapping6";
			channelNameMappings["Mapping7"]      = "Mapping7";
			channelNameMappings["Mapping8"]      = "Mapping8";
			channelNameMappings["Mapping9"]      = "Mapping9";
			channelNameMappings["Orientation"]   = "Orientation";
			channelNameMappings["Rotation"]      = "Orientation";
			channelNameMappings["Spin"]          = "Spin";
			channelNameMappings["ID"]            = "ID";
			*/
		}
        
        ScanForChannels();
    } 
    virtual ~SIPointCloudParticleStream() 
    {
        for (vector<CBaseICEAttributeDataArray*>::iterator i=dataArrays.begin(); i != dataArrays.end(); i++)
        {
            CBaseICEAttributeDataArray*& ptr = *i;
            if (ptr != 0)
            {
                delete ptr;
                ptr = 0;
            }
        }
    }

    void ScanForChannels()
    {
        CStatus res;
        CPointRefArray points( geometry.GetPoints() );
        particleCount = points.GetCount();

        attributes.clear();
        channels.clear();

		if (particleCount == 0) // don't scan for anything if the point cloud is empty
		{
			Application().LogMessage(CString("Point cloud is empty skipping channel mapping: ") + geometry.GetName() , siInfoMsg);
			return;
		}
		
        
        CRefArray attributesRefArray = geometry.GetICEAttributes();
        for (int i=0; i < attributesRefArray.GetCount(); i++)
        {
            ICEAttribute attr(attributesRefArray[i]);
            string attrName  = attr.GetName().GetAsciiString();

            bool defined = attr.IsDefined();
            siICENodeContextType context = attr.GetContextType();
            
            if (defined == false || context != siICENodeContextComponent0D)
                continue;

            // TODO: ensure its only a supported type as well...

            // see if we have a mapping into krakatoa for this
            map<string,string>::iterator pos =  channelNameMappings.find(attrName);
            if (channelNameMappings.end() == pos)
            {
                // channel is not supported by krakatoa so skip it
                continue;
            }

            string krakName = pos->second;
            channel_data data;
            CBaseICEAttributeDataArray* dataArray;

            switch (attr.GetDataType())
            {
            case siICENodeDataBool:
                data = this->append_channel(krakName.c_str(), DATA_TYPE_UINT8, 1);
                dataArray = new CICEAttributeDataArrayBool();
                break;
            case siICENodeDataLong:
                data = this->append_channel(krakName.c_str(), DATA_TYPE_INT32, 1);
                dataArray = new CICEAttributeDataArrayLong();
                break;
            case siICENodeDataFloat:
                data = this->append_channel(krakName.c_str(), DATA_TYPE_FLOAT32, 1);
                dataArray = new CICEAttributeDataArrayFloat();
                break;
            case siICENodeDataVector2:
                data = this->append_channel(krakName.c_str(), DATA_TYPE_FLOAT32, 2);
                dataArray = new CICEAttributeDataArrayVector2f();
                break;
            case siICENodeDataVector3:
                data = this->append_channel(krakName.c_str(), DATA_TYPE_FLOAT32, 3);
                dataArray = new CICEAttributeDataArrayVector3f();
                break;
            case siICENodeDataVector4:
                data = this->append_channel(krakName.c_str(), DATA_TYPE_FLOAT32, 4);
                dataArray = new CICEAttributeDataArrayVector4f();
                break;
            case siICENodeDataQuaternion:
                data = this->append_channel(krakName.c_str(), DATA_TYPE_FLOAT32, 4);
                dataArray = new CICEAttributeDataArrayQuaternionf();
                break;
            case siICENodeDataColor4:
                data = this->append_channel(krakName.c_str(), DATA_TYPE_FLOAT32, 3); // NOTE: krakatoa expected color to be just RGB, not alpha, this is a special case mis-map on purpose
                dataArray = new CICEAttributeDataArrayColor4f();
                break;
            case siICENodeDataRotation:
                data = this->append_channel(krakName.c_str(), DATA_TYPE_FLOAT32, 4); // store as quat xyzw
                dataArray = new CICEAttributeDataArrayRotationf();
                break;
            default:
                continue; // skip this channel if its not a supported data type
                    
            }

            Application().LogMessage(CString("Mapping channel: ") + CString(attr.GetName()) + CString(" ") +  CString(krakName.c_str()) ,siInfoMsg);

            this->channels.push_back(data);
            this->attributes.push_back(attr);
            attr.GetDataArray(*dataArray);
            this->dataArrays.push_back(dataArray);
        }
    }
    virtual krakatoasr::INT64 particle_count() const 
    {
        if (this->particleCount == -1)
            throw std::exception("particle_count() called before attributes were scanned");
        return this->particleCount;
    }
    virtual bool get_next_particle( void* particleData ) 
    {
        // use to temp copy values from the data array
        static unsigned char pBuff[100];
        float* pFloatBuff = (float*)pBuff;
        LONG* pLONGBuff = (LONG*)pBuff;

        for (int i=0; i < channels.size(); ++i)
        {
            // TODO: refactor to use iterators
            ICEAttribute& attr = attributes[i];
            channel_data& cd   = channels[i];
            CBaseICEAttributeDataArray* pDataArray = dataArrays[i];
                        
            switch (attr.GetDataType())
            {
                case siICENodeDataLong:
                {
                    set_channel_value(cd, particleData, &(*((CICEAttributeDataArrayLong*)pDataArray))[(ULONG)particleIndex]);
                    break;
                }
                case siICENodeDataFloat:
                {
                    set_channel_value(cd, particleData, &(*((CICEAttributeDataArrayFloat*)pDataArray))[(ULONG)particleIndex]);
                    break;
                }
                case siICENodeDataVector3:
                {
                    set_channel_value(cd, particleData, &(*((CICEAttributeDataArrayVector3f*)pDataArray))[(ULONG)particleIndex]);
                    break;
                }
                case siICENodeDataVector4:
                {
                    set_channel_value(cd, particleData, &(*((CICEAttributeDataArrayVector4f*)pDataArray))[(ULONG)particleIndex]);
                    break;
                }
                case siICENodeDataColor4:
                {
                    set_channel_value(cd, particleData, &(*((CICEAttributeDataArrayColor4f*)pDataArray))[(ULONG)particleIndex]);
                    break;
                }
              /*
			   TODO: support these....
			   case siICENodeDataQuaternion:
                    storeData<CICEAttributeDataArrayQuaternionf,float>(attr,pPD, partioAttr);
                    break;
                case siICENodeDataRotation:
                    storeData<CICEAttributeDataArrayRotationf ,float>(attr,pPD, partioAttr);
                    break;*/
            }
        }
        
        particleIndex++;
        return particleIndex <= particleCount;
    }
    virtual void close() 
    {
        particleIndex = 0;
    }
};

map<string,string> SIPointCloudParticleStream::channelNameMappings;

class SILogger : public krakatoasr::logging_interface
{
private:
    std::map<logging_level_t, siSeverityType> levelMap;
public:
    SILogger()
    {
        levelMap[LOG_ERRORS]   = siErrorMsg;
	    levelMap[LOG_WARNINGS] = siWarningMsg;
	    levelMap[LOG_PROGRESS] = siInfoMsg;
	    levelMap[LOG_STATS]    = siInfoMsg;
	    levelMap[LOG_DEBUG]    = siInfoMsg;
	    levelMap[LOG_CUSTOM]   = siInfoMsg;
    }
    virtual void write_log_line( const char* line, krakatoasr::logging_level_t level ) 
    {
        Application().LogMessage( CString(line), levelMap[level]);
    }

    virtual ~SILogger()
    {}
};

static SILogger g_msgLogger; // lame global so we can log both in side and outside the render callback

inline krakatoasr::animated_transform Mat2AT(MATH::CMatrix4& mat4)
{
	return animated_transform((float)mat4.GetValue(0, 0), (float)mat4.GetValue(0, 1), (float)mat4.GetValue(0, 2), (float)mat4.GetValue(0, 3),
		(float)mat4.GetValue(1, 0), (float)mat4.GetValue(1, 1), (float)mat4.GetValue(1, 2), (float)mat4.GetValue(1, 3),
		(float)mat4.GetValue(2, 0), (float)mat4.GetValue(2, 1), (float)mat4.GetValue(2, 2), (float)mat4.GetValue(2, 3),
		(float)mat4.GetValue(3, 0), (float)mat4.GetValue(3, 1), (float)mat4.GetValue(3, 2), (float)mat4.GetValue(3, 3));
}

void AddLight(krakatoasr::krakatoa_renderer& renderer, Light& light)
{
	Primitive lightPrim = light.GetActivePrimitive();

	int   type = lightPrim.GetParameter("Type").GetValue();
	float falloffExp = lightPrim.GetParameter("LightExponent").GetValue();
	float intensity = lightPrim.GetParameter("LightEnergyIntens").GetValue();
	float energyR = lightPrim.GetParameter("LightEnergyR").GetValue();
	float energyG = lightPrim.GetParameter("LightEnergyG").GetValue();
	float energyB = lightPrim.GetParameter("LightEnergyB").GetValue();

	// TODO: support manual attenuation

	switch (type)
	{
	case 0: // Point
	{
				point_light klight;
				klight.set_name(light.GetName().GetAsciiString());
				klight.set_flux(energyR * intensity, energyG * intensity, energyB * intensity);
				klight.set_decay_exponent((int)falloffExp);
				klight.use_near_attenuation(false);
				klight.use_far_attenuation(false);
				renderer.add_light(&klight, Mat2AT(light.GetKinematics().GetGlobal().GetTransform().GetMatrix4()));
				break;
	}
	case 1: // Infinite / directional
	{
				direct_light klight;
				klight.set_name(light.GetName().GetAsciiString());
				klight.set_flux(energyR * intensity, energyG * intensity, energyB * intensity);
				klight.set_decay_exponent((int)falloffExp);
				klight.use_near_attenuation(false);
				klight.use_far_attenuation(false);
				renderer.add_light(&klight, Mat2AT(light.GetKinematics().GetGlobal().GetTransform().GetMatrix4()));

				break;
	}
	case 2: // Spot light
	{
				float lightConeAngleDeg = lightPrim.GetParameter("LightCone").GetValue();

				spot_light klight;
				klight.set_cone_angle(lightConeAngleDeg, lightConeAngleDeg); // for now just put both in there
				klight.set_name(light.GetName().GetAsciiString());
				klight.set_flux(energyR * intensity, energyG * intensity, energyB * intensity);
				klight.set_decay_exponent((int)falloffExp);
				klight.use_near_attenuation(false);
				klight.use_far_attenuation(false);
				renderer.add_light(&klight, Mat2AT(light.GetKinematics().GetGlobal().GetTransform().GetMatrix4()));

				break;
	}
	}

}

void SetShaderFromProperty(krakatoasr::krakatoa_renderer& renderer, Property& prop)
{
	int shader = prop.GetParameter("Shader").GetValue();
	if (shader == 0) // iso-tropic
	{
		shader_isotropic shader;
		renderer.set_shader(&shader); // makes a copy so we don't need to keep shader around
	}
	else if (shader == 1) // phong
	{
		shader_phong shader;
		shader.set_specular_level(prop.GetParameter("SpecularLevel").GetValue());
		shader.set_specular_power(prop.GetParameter("SpecularPower").GetValue());
		shader.use_specular_level_channel(prop.GetParameter("UseSpecularLevelChannel").GetValue());
		shader.use_specular_power_channel(prop.GetParameter("UseSpecularPowerChannel").GetValue());

		renderer.set_shader(&shader); // makes a copy so we don't need to keep shader around
	}
	else if (shader == 2) // henyey_greenstein 
	{
		shader_henyey_greenstein shader;
		shader.set_phase_eccentricity(prop.GetParameter("Eccentricity").GetValue());
		shader.use_phase_eccentricity_channel(prop.GetParameter("UseEccentricityChannel").GetValue());

		renderer.set_shader(&shader); // makes a copy so we don't need to keep shader around
	}
	else if (shader == 3) // schlick
	{
		shader_schlick shader;
		shader.set_phase_eccentricity(prop.GetParameter("Eccentricity").GetValue());
		shader.use_phase_eccentricity_channel(prop.GetParameter("UseEccentricityChannel").GetValue());

		renderer.set_shader(&shader); // makes a copy so we don't need to keep shader around
	}
	else if (shader == 4) // kajiya_kay 
	{
		shader_kajiya_kay  shader;
		shader.set_specular_level(prop.GetParameter("SpecularLevel").GetValue());
		shader.set_specular_power(prop.GetParameter("SpecularPower").GetValue());
		shader.use_specular_level_channel(prop.GetParameter("UseSpecularLevelChannel").GetValue());
		shader.use_specular_power_channel(prop.GetParameter("UseSpecularPowerChannel").GetValue());

		renderer.set_shader(&shader); // makes a copy so we don't need to keep shader around
	}
	else if (shader == 5)
	{
		// this one sucks....
		shader_marschner shader;
		shader.set_specular_glossiness(prop.GetParameter("SpecularGlossiness").GetValue());
		shader.set_specular_level(prop.GetParameter("SpecularLevel").GetValue());
		shader.set_specular_shift(prop.GetParameter("SpecularShift").GetValue());

		shader.set_secondary_specular_glossiness(prop.GetParameter("SecondarySpecularGlossiness").GetValue());
		shader.set_secondary_specular_level(prop.GetParameter("SecondarySpecularLevel").GetValue());
		shader.set_secondary_specular_shift(prop.GetParameter("SecondarySpecularShift").GetValue());

		shader.set_glint_level(prop.GetParameter("GlintLevel").GetValue());
		shader.set_glint_size(prop.GetParameter("GlintSize").GetValue());
		shader.set_glint_glossiness(prop.GetParameter("GlintGlossiness").GetValue());

		shader.set_diffuse_level(prop.GetParameter("DiffuseLevel").GetValue());

		shader.use_specular_glossiness_channel(prop.GetParameter("UseSpecularGlossinessChannel").GetValue());
		shader.use_specular_level_channel(prop.GetParameter("UseSpecularLevelChannel").GetValue());
		shader.use_specular_shift_channel(prop.GetParameter("UseSpecularShiftChannel").GetValue());

		shader.use_secondary_specular_glossiness_channel(prop.GetParameter("UseSecondarySpecularGlossinessChannel").GetValue());
		shader.use_secondary_specular_level_channel(prop.GetParameter("UseSecondarySpecularLevelChannel").GetValue());
		shader.use_secondary_specular_shift_channel(prop.GetParameter("UseSecondarySpecularShiftChannel").GetValue());

		shader.use_glint_level_channel(prop.GetParameter("UseGlintLevelChannel").GetValue());
		shader.use_glint_size_channel(prop.GetParameter("UseGlintSizeChannel").GetValue());
		shader.use_glint_glossiness_channel(prop.GetParameter("UseGlintGlossinessChannel").GetValue());

		shader.use_diffuse_level_channel(prop.GetParameter("UseDiffuseLevelChannel").GetValue());

		renderer.set_shader(&shader);
	}
}

triangle_mesh* AddOcclusionMesh(krakatoa_renderer& renderer, X3DObject& obj3d)
{
	Primitive& prim = obj3d.GetActivePrimitive();  // should be a polygon mesh
	PolygonMesh geom = prim.GetGeometry();
	if (geom.IsValid() == false)
	{
		Application().LogMessage(CString("Object is not a polygon mesh: ") + obj3d.GetName(), siWarningMsg);
		return 0;
	}
	CGeometryAccessor ga = geom.GetGeometryAccessor();
	triangle_mesh* pMesh = new triangle_mesh();

	int triCount = ga.GetTriangleCount();
	int vertCount = ga.GetVertexCount();
	CLongArray indices;
	ga.GetTriangleVertexIndices(indices);
	CDoubleArray verts;
	ga.GetVertexPositions(verts);

	pMesh->set_num_vertices(verts.GetCount());
	pMesh->set_num_triangle_faces(indices.GetCount());

	for (int i = 0; i < vertCount; i++)
	{
		pMesh->set_vertex_position(i, (float)verts[i * 3 + 0], (float)verts[i * 3 + 1], (float)verts[i * 3 + 2]);
	}
	for (int i = 0; i < triCount; i++)
	{
		pMesh->set_face(i, indices[i * 3 + 0], indices[i * 3 + 1], indices[i * 3 + 2]);
	}


	// TODO: optionally pull this data from a custom property on the mesh
	pMesh->set_visible_to_camera(true);
	pMesh->set_visible_to_lights(true);

	renderer.add_mesh(pMesh, Mat2AT(obj3d.GetKinematics().GetGlobal().GetTransform().GetMatrix4()));

	return pMesh;
}

// This class ensures the render data is unlocked safely no matter how the render function exists
// create on the stack, then when it goes out of scope it cleans up if it needs to 
class LockRendererData
{
protected:
	Renderer& renderer;
	bool locked;

public:
	LockRendererData(Renderer& renderer) :
		renderer(renderer),
		locked(false)
	{

	}

	CStatus lock()
	{
		if (locked == false)
		{
			CStatus res = renderer.LockSceneData();
			if (res == CStatus::OK)
				locked = true;
			return res;
		}
		return CStatus::OK;
	}

	CStatus unlock()
	{
		if (locked)
		{
			CStatus res = renderer.UnlockSceneData();
			if (res == CStatus::OK)
			{
				locked = false;
			}
			return res;
		}
		return CStatus::OK;
	}


	~LockRendererData()
	{
		unlock(); // ensure unlocked happens when this object goes out of scope
	}
};

SICALLBACK XSILoadPlugin( PluginRegistrar& in_reg )
{
    Application().LogMessage(L"KrakatoaSRIntegration being loaded", siInfoMsg);

	in_reg.PutAuthor(L"James Vecore");
    in_reg.PutEmail(L"james.vecore@gmail.com");
	in_reg.PutName(L"SoftimageKrakatoa");
	in_reg.PutVersion(1,0);
	//RegistrationInsertionPoint - do not remove this line

    in_reg.RegisterRenderer(L"KrakatoaSR");
    
	return CStatus::OK;
}

SICALLBACK XSIUnloadPlugin( const PluginRegistrar& in_reg )
{
	CString strPluginName;
	strPluginName = in_reg.GetName();
	Application().LogMessage(strPluginName + L" has been unloaded.",siInfoMsg);
	return CStatus::OK;
}

SICALLBACK KrakatoaSR_Init( CRef& in_context )
{ 
    Application().LogMessage("KrakatoaSR Init",siInfoMsg);
    Context context(in_context);

    g_shouldAbort = false;

    Renderer renderer(context.GetSource());
    
    CStatus res;
    res = renderer.AddDefaultChannel(L"Main", siRenderChannelColorType);
    res = renderer.AddOutputImageFormat("Open EXR","exr");
    res = renderer.AddOutputImageFormatSubType(siRenderChannelColorType, "RGBA", siImageBitDepthFloat32);
    res = renderer.AddProperty(siRenderPropertyOptions, "KrakatoaRendererPropertyPlugin.Krakatoa Options"); // implement in the python script for now
    res = renderer.PutName("Krakatoa");
    CLongArray processTypesArray(2);
    processTypesArray[0] = siRenderSequence;
    processTypesArray[1] = siRenderFramePreview;
    res = renderer.PutProcessTypes(processTypesArray);

    krakatoasr::set_global_logging_interface(&g_msgLogger);
    krakatoasr::set_global_logging_level( krakatoasr::LOG_DEBUG );

    return res;
}

SICALLBACK KrakatoaSR_Term( CRef &in_ctxt )
{
    g_shouldAbort = false;

	return  CStatus::OK;
}

SICALLBACK KrakatoaSR_Process( CRef& in_context )
{ 
	g_shouldAbort = false;

    Application().LogMessage("KrakatoaSR_Process()", siInfoMsg);
    RendererContext context(in_context);
    Renderer renderer(context.GetSource());

	LockRendererData locker = LockRendererData(renderer); // create this on the stack to ensure render data is unlocked on error or exception
	
    if (locker.lock() != CStatus::OK)
		return CStatus::Abort;

    ULONG                renderID    = (ULONG)context.GetAttribute(L"RenderID");                     // unsigned int
    siRenderProcessType  process     = (siRenderProcessType)(ULONG)context.GetAttribute(L"Process"); // siRenderProcessType
    CString              renderType  = context.GetAttribute(L"RenderType");       // String: "Pass", "Region", "Shaderball"
    CRefArray&           scene       = context.GetArrayAttribute(L"Scene");       // CRefArray of Model
    CRefArray&           objList     = context.GetArrayAttribute(L"ObjectList");  // CRefArray of X3DObject
    CRefArray&           dirtyList   = context.GetArrayAttribute(L"DirtyList");   // CRefArray of X3DObject
    CRefArray&           lights      = context.GetArrayAttribute(L"Lights");      // CRefArray of Light
    Primitive            cameraPrim  = context.GetAttribute(L"Camera");           // Primitive
    CValue&              vMaterial   = context.GetAttribute(L"Material");         // Material or Shader to use is non is assigned, may be null or empty

    unsigned int imageWidth         = (ULONG)context.GetAttribute(L"ImageWidth");
    unsigned int imageHeight        = (ULONG)context.GetAttribute(L"ImageHeight");
    unsigned int cropLeft           = (ULONG)context.GetAttribute(L"CropLeft");
    unsigned int cropBottom         = (ULONG)context.GetAttribute(L"CropBottom");
    unsigned int cropWidth          = (ULONG)context.GetAttribute(L"CropWidth");
    unsigned int cropHeight         = (ULONG)context.GetAttribute(L"CropHeight");
    bool selectionOnly              = context.GetAttribute(L"SelectionOnly");
    bool trackSelection             = context.GetAttribute(L"TrackSelection");
    bool motionBlur                 = context.GetAttribute(L"MotionBlur");
    double shutterSpeed             = context.GetAttribute(L"ShutterSpeed");
    double shutterOffset            = context.GetAttribute(L"ShutterOffset");
    siRenderShutterType shutterType = (siRenderShutterType)(ULONG)context.GetAttribute(L"ShutterType");
    bool motionBlurDeformation      = context.GetAttribute(L"MotionBlurDeformation");
    bool fileOutput                 = context.GetAttribute(L"FileOutput");
    bool skipExistingFrames         = context.GetAttribute(L"SkipExistingFiles");
    bool fieldRender                = context.GetAttribute(L"FieldRender");
    siRenderFieldType fieldType     = (siRenderFieldType)(ULONG)context.GetAttribute(L"FieldInterleave");
    CString archiveFileName         = context.GetAttribute(L"ArchiveFileName");
    bool archiveMultiFrame          = context.GetAttribute(L"ArchiveMultiFrame");
    bool archiveDisplayProxies      = context.GetAttribute(L"ArchiveDisplayProxies");
    CRefArray renderMapList         = context.GetArrayAttribute(L"RenderMapList");
    unsigned int renderMapTileSize  = (ULONG)context.GetAttribute(L"RenderMapTileSize");
	

    X3DObject			cameraObj	= cameraPrim.GetOwners( )[ 0 ];
	Camera				camera		= cameraObj;
    Primitive           camPrim     = camera.GetActivePrimitive();
	CString				cameraName	= cameraObj.GetName();

    Application().LogMessage(CString(L"Render Type: ") + renderType, siInfoMsg);
    Application().LogMessage(CString(L"Using Camera: ") + cameraName,siInfoMsg);

	krakatoasr::krakatoa_renderer krakatoa; // locally scoped
   
       
    CTime evalTime = context.GetTime();
    Property& rendererProp = context.GetRendererProperty( evalTime );

	bool outputPrt = rendererProp.GetParameter("OutputPrt").GetValue();
	bool actuallydOutputPrt = outputPrt && renderType != CString("Region");
	bool actuallyRenderImage = !actuallydOutputPrt;
	
    krakatoa.set_error_on_missing_license( rendererProp.GetParameter("ErrorOnMissingLicense").GetValue() );
	
    rendering_method_t method = (krakatoasr::rendering_method_t)(int)rendererProp.GetParameter("RenderingMethod").GetValue();
    krakatoa.set_rendering_method( method );

    krakatoasr::filter_t filter = (krakatoasr::filter_t)(int)rendererProp.GetParameter("AttenuationLookupFilter").GetValue();
    int size = rendererProp.GetParameter("AttenuationLookupFilterSize").GetValue();
    krakatoa.set_attenuation_lookup_filter(filter,size > 0 ? size : 1);

    filter = (krakatoasr::filter_t)(int)rendererProp.GetParameter("DrawPointFilter").GetValue();
    size = rendererProp.GetParameter("DrawPointFilterSize").GetValue();
    krakatoa.set_draw_point_filter(filter, size > 0 ? size : 1 );

    krakatoa.set_voxel_filter_radius( rendererProp.GetParameter("VoxelRadius").GetValue() );
    krakatoa.set_voxel_size( rendererProp.GetParameter("VoxelSize").GetValue() );
    
    krakatoa.set_background_color( rendererProp.GetParameter("BackgroundR").GetValue(), rendererProp.GetParameter("BackgroundG").GetValue(), rendererProp.GetParameter("BackgroundB").GetValue() );
    
    krakatoa.set_density_per_particle( rendererProp.GetParameter("DensityPerParticle").GetValue());
    krakatoa.set_density_exponent( rendererProp.GetParameter("DensityExponent").GetValue() );
    
    krakatoa.use_emission( rendererProp.GetParameter("UseEmission").GetValue() );
    krakatoa.set_emission_strength( rendererProp.GetParameter("EmissionStrength").GetValue() );
    krakatoa.set_emission_strength_exponent( rendererProp.GetParameter("EmissionExponent").GetValue() );

    krakatoa.set_lighting_density_per_particle( rendererProp.GetParameter("LightingDensityPerParticle").GetValue() );
    krakatoa.set_lighting_density_exponent( rendererProp.GetParameter("LightingDensityExponent").GetValue() );

    krakatoa.use_absorption_color(  rendererProp.GetParameter("UseAbsorbtionChannel").GetValue() );
    krakatoa.set_additive_mode(  rendererProp.GetParameter("AdditiveMode").GetValue() );
    krakatoa.enable_camera_blur(  rendererProp.GetParameter("CameraBlur").GetValue() );

    krakatoa.enable_depth_of_field( rendererProp.GetParameter("UseDepthOfField").GetValue() );
    krakatoa.set_depth_of_field(rendererProp.GetParameter("FStop").GetValue(), rendererProp.GetParameter("FocalLength").GetValue() , rendererProp.GetParameter("FocalDistance").GetValue() , rendererProp.GetParameter("SampleRate").GetValue() );

    krakatoa.enable_motion_blur( rendererProp.GetParameter("UseMotionBlur").GetValue() );
    krakatoa.set_motion_blur(rendererProp.GetParameter("ShutterBegin").GetValue(), rendererProp.GetParameter("ShutterEnd").GetValue() , rendererProp.GetParameter("MBSamples").GetValue() , rendererProp.GetParameter("Jitter").GetValue() );

    // render elements / extra channels
    krakatoa.enable_normal_render( rendererProp.GetParameter("Normals").GetValue() );
    krakatoa.enable_occluded_rgba_render( rendererProp.GetParameter("OccludedRGBA").GetValue() );
    krakatoa.enable_velocity_render( rendererProp.GetParameter("Velocity").GetValue() );
    krakatoa.enable_z_depth_render( rendererProp.GetParameter("ZDepth").GetValue() );

    SetShaderFromProperty(krakatoa, rendererProp); // must happen before particle add

    SIProgressLogger logger(context);
    SICancelRenderInterface canceler;
    SIFrameBufferInterface frameBufferInterface(context, cropWidth, cropHeight, cropLeft, cropBottom);
    SINoSave noSave;
    multi_channel_exr_file_saver* pSaver = 0;
        
     //add the file saver to the renderer
    if (renderType != CString("Region") && fileOutput && outputPrt == false)
    {
        bool found = false;
        CRefArray& frameBuffers = context.GetFramebuffers();
        for (int i=0; i < frameBuffers.GetCount(); ++i)
        {
            Framebuffer fb(frameBuffers[i]);
            if (fb.GetName() == "Main")
            {
				CValue enbableVal = fb.GetParameterValue("Enabled", evalTime.GetTime());
				if (enbableVal) // if the frame buffer is not enabled just ignore
				{

					CString path = fb.GetResolvedPath();
					CString pathWithFrame = CUtils::ResolveTokenString(path, evalTime, true);
					// we only support saving .exr files so lets if the user extension is correct or not
					// it is possible when switch renderer to get a .pic output in there despite the filter specification in the renderer
					ULONG dotindex = pathWithFrame.ReverseFindString(".");
					if (dotindex != ULONG_MAX)
					{
						CString ext = pathWithFrame.GetSubString(dotindex + 1);
						ext.Lower();
						if (ext != CString("exr"))
						{
							Application().LogMessage("Unsupported file type, cannot render: " + ext, siErrorMsg);
							return CStatus::Abort;
						}
					}

					// TODO: check for access denied error before we start rendering...

					Application().LogMessage("Saving render to file: " + pathWithFrame, siInfoMsg);
					pSaver = new multi_channel_exr_file_saver(pathWithFrame.GetAsciiString());


					pSaver->set_exr_compression_type((krakatoasr::exr_compression_t)(int)rendererProp.GetParameter("ExrCompression").GetValue());

					krakatoa.set_render_save_callback( pSaver ); // you must set a file saver or krakatoa exits
					found = true;
				}
				// we don't support other frame buffer names and we already found Main so just break
				break;
            }
        }
        if (found == false)
        {
            Application().LogMessage("Failed to find a Framebuffer called 'Main' or it was disabled, not saving output to disk", siWarningMsg);
            krakatoa.set_render_save_callback( &noSave);
        }
    }
    else
    {
        krakatoa.set_render_save_callback( &noSave);
    }
    
    krakatoasr::set_global_logging_level(LOG_DEBUG);
    
    krakatoa.set_progress_logger_update(&logger);
	krakatoa.set_cancel_render_callback(&canceler);
	if (actuallyRenderImage)
	{
		// only set this stuff up if we actually going to render
		krakatoa.set_render_resolution(imageWidth, imageHeight);
		krakatoa.set_frame_buffer_update(&frameBufferInterface);
	}
        
    //we now apply the transform to the camera
	krakatoa.set_camera_tm( Mat2AT(camera.GetKinematics().GetGlobal().GetTransform().GetMatrix4()) );

    float nearPlane   = camPrim.GetParameter("near").GetValue();
    float farPlane    = camPrim.GetParameter("far").GetValue();
    float pixelAspect = camPrim.GetParameter("pixelratio").GetValue();
    int   projType    = camPrim.GetParameter("proj").GetValue();    // 0 = orthographic 1 = perspective

    if (projType == 0) // orthographic camera
    {
        krakatoa.set_camera_type( CAMERA_ORTHOGRAPHIC );
        
        float orthoHeight = camPrim.GetParameter("orthoheight").GetValue();
        float orthoWidth  = ((float)imageWidth) * orthoHeight / ((float)imageHeight);
        
        krakatoa.set_camera_orthographic_width(orthoWidth);
    }
    else // perspective camera
    {
        krakatoa.set_camera_type( CAMERA_PERSPECTIVE );

        float fov   = camPrim.GetParameter("fov").GetValue();
        int fovType = camPrim.GetParameter("fovtype").GetValue(); // 0 = vertical, 1 = horizontal
    

        if (fovType == 1)
            krakatoa.set_camera_perspective_fov(fov * 3.1415926535897932384626433832795028841f / 180.0f); // expected horizontal fov in RADIANS!
        else
        {
            // fov is vertical, need to convert to horizontal
            float hfov = ((float)imageWidth) * fov / ((float)imageHeight);
            krakatoa.set_camera_perspective_fov(hfov * 3.1415926535897932384626433832795028841f / 180.0f); // expected horizontal fov in RADIANS!
        }
    }

    krakatoa.set_camera_clipping(nearPlane, farPlane);
    krakatoa.set_pixel_aspect_ratio(pixelAspect);
	
	
	if (outputPrt)
	{
		if (renderType == CString("Region"))
		{
			// don't do .prt output on a region render
			Application().LogMessage(CString("Skipping .prt output during region render."), siWarningMsg);
		}
		else
		{
			// if we are 'rendering' prt files, then we assume nothing else has to be loaded

			bool computeLighting = rendererProp.GetParameter("ComputeLighting").GetValue();
			CString prtOutput = rendererProp.GetParameter("PrtPathExpression").GetValue();

			bool hasFrameToken = prtOutput.FindString("[Frame]") != ULONG_MAX;
			hasFrameToken = hasFrameToken || prtOutput.FindString("[frame]") != ULONG_MAX;

			CString prtOutputResolved = CUtils::ResolveTokenString(prtOutput, evalTime, true);

			// some lame path handling stuff here, don't want to take a dependency on boost or pystring just for this...
			ULONG dotindex = prtOutputResolved.ReverseFindString(".");
			if (dotindex == ULONG_MAX)
			{
				Application().LogMessage(CString("Prt Output Path did not include the '.prt' extension: ") + prtOutputResolved, siErrorMsg);
				return CStatus::Fail;
			}
			CString ext = prtOutputResolved.GetSubString(dotindex);
			ULONG lastSlash = prtOutputResolved.ReverseFindString(CUtils::Slash());
			if (lastSlash == ULONG_MAX)
			{
				Application().LogMessage(CString("Prt Output Path was not a valid path (no directory specified): ") + prtOutputResolved, siErrorMsg);
				return CStatus::Fail;
			}
			CString dir = prtOutputResolved.GetSubString(0, lastSlash);
			CString fname = prtOutputResolved.GetSubString(lastSlash + 1);

			if (CUtils::EnsureFolderExists(dir, false) == false)
			{
				Application().LogMessage(CString("Prt Output Path was to an invalid directory: ") + dir, siErrorMsg);
				return CStatus::Fail;
			}

			CString fnameNoExt = fname.GetSubString(0, fname.ReverseFindString("."));
			// now we need to put in the frame number
			// should really check the existing file name with a regex for #### or %04d etc
			// for now just assume the user does not specify in the path name the frame spec and default to 4 padded frame number

			CString outputPath;
			if (hasFrameToken == false)
			{
				int frame = (int)evalTime.GetTime(CTime::Frames);
				static char buff[5];
				sprintf(buff, ".%04d", frame);
				CString fnameWithFrames = fnameNoExt + CString(buff) + ext;
				outputPath = CUtils::BuildPath(dir, fnameWithFrames);
			}
			else
			{
				outputPath = prtOutputResolved;
			}

			// for now just support default channels....
			// this doesn't actually write the prt file, we still need to call render()
			// lights, occlusion meshes, etc can all affect the output prt so they all still need to be added as well
			krakatoa.save_output_prt(outputPath.GetAsciiString(), computeLighting, true);
		}
	}

    vector<SIPointCloudParticleStream*> pStreamInterfaces;
    vector<triangle_mesh*> meshPtrs;

    bool useOcclusionMeshes    = rendererProp.GetParameter("UseOcclusionMeshes").GetValue();
    CString occlusionGroupName = rendererProp.GetParameter("OcclusionMeshGroupName").GetValue();
    bool useLightGroup         = rendererProp.GetParameter("UseLightGroup").GetValue();
    CString lightGroupName     = rendererProp.GetParameter("LightGroupName").GetValue();
    
    for (int i=0; i < scene.GetCount(); i++)
    {
        CRef ref(scene[i]);
        if (ref.IsA(siX3DObjectID))
        {
            X3DObject obj(ref);
            CRefArray& pointClouds = obj.FindChildren2(CString(), L"pointcloud",CStringArray(), true);
            for (int j=0; j < pointClouds.GetCount(); ++j)
            {
                X3DObject child( pointClouds[j] );
                Primitive& prim = child.GetActivePrimitive();   
                bool valid = prim.IsValid();
                Geometry& geom = prim.GetGeometry();
                valid = geom.IsValid();
				if (geom.GetPoints().GetCount() == 0)
				{
					Application().LogMessage(CString("Skipping point cloud since particle count is 0: ") + child.GetFullName(), siInfoMsg);
				}
				else
				{
					Application().LogMessage(CString("Adding particle stream from point cloud: ") + child.GetFullName(), siInfoMsg);
					SIPointCloudParticleStream* pStream = new SIPointCloudParticleStream(geom);
					pStreamInterfaces.push_back(pStream);
					krakatoa.add_particle_stream(particle_stream::create_from_particle_stream_interface(pStream));
				}
            }

            if (useOcclusionMeshes || useLightGroup)
            {

                Model model(ref); // we can't find groups with FindChildren2 which is super annoying, we have to pull from the scene root model
                if (model.IsValid())
                {
                    CRefArray& groups = model.GetGroups();
                    for (int j=0; j < groups.GetCount(); j++)
                    {
                        Group group(groups[j]);
                        if (useOcclusionMeshes && group.GetName() == occlusionGroupName)
                        {
                            CRefArray& groupMembers = group.GetMembers();
                            for (int k=0; k < groupMembers.GetCount(); k++)
                            {
                                X3DObject gchild(groupMembers[k]);
                                const char* gchildName = gchild.GetName().GetAsciiString();
                                if (gchild.GetType() == CString("polymsh"))
                                {
                                    triangle_mesh* pMesh = AddOcclusionMesh(krakatoa, gchild);
                                    if (pMesh != 0)
                                    {
                                        Application().LogMessage(CString("Added occlusion mesh: ") + gchild.GetName(), siInfoMsg);
                                        meshPtrs.push_back(pMesh);
                                    }
                                }
                                else
                                {
                                    Application().LogMessage(CString("skipping object in occlusion group (it is not a polygon mesh): ") + gchild.GetFullName(), siWarningMsg);
                                }
                            }
                        }
                        else if (method == METHOD_PARTICLE && useLightGroup && group.GetName() == lightGroupName)
                        {
                            CRefArray& groupMembers = group.GetMembers();
                            for (int k=0; k < groupMembers.GetCount(); k++)
                            {
                                Light light(groupMembers[k]);
                                if (light.IsValid())
                                {
                                    AddLight(krakatoa, light);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    if (method == METHOD_PARTICLE && useLightGroup == false) // voxel mode errors if you add lights
    {
        // add all scene lights since we are not using a light group
        for (int i=0; i < lights.GetCount(); ++i)
        {
            CRef& ref = lights[i];
            Light light(ref);
            bool valid = light.IsValid();
            AddLight(krakatoa, light);
        }
    }
       
    // Unlock the scene data *before* we start rendering and sending tile data back.
	// we are done querying the scene
	if (locker.unlock() != CStatus::OK)
		return CStatus::Abort;

    context.NewFrame( imageWidth, imageHeight );

    try
    {
        bool successful = krakatoa.render();
        krakatoa.reset_renderer(); // reset render to drop progress logger, meshes, lights, etc
        
        for (vector<SIPointCloudParticleStream*>::iterator i = pStreamInterfaces.begin(); i != pStreamInterfaces.end(); ++i)
            delete *i;
        pStreamInterfaces.clear();

        for (vector<triangle_mesh*>::iterator i = meshPtrs.begin(); i != meshPtrs.end(); ++i)
            delete *i;
        meshPtrs.clear();

        if (pSaver != 0)
		{
            delete pSaver;
			pSaver = 0;
		}

        if (successful == false) // if we get a false but no exception, the use canceled, it was not a real error
        {
            Application().LogMessage("Krakatoa renderer aborted",siInfoMsg);
            return CStatus::Abort;
        }
        Application().LogMessage("Krakatoa renderer completed successfully",siInfoMsg);
        return( CStatus::OK );
    }
    catch (std::exception& ex)
    {
        Application().LogMessage(CString("Karkatoa rendering failed: ") + CString(ex.what()), siErrorMsg);
    
        krakatoa.reset_renderer(); // reset render to drop progress logger, etc

        for (vector<SIPointCloudParticleStream*>::iterator i = pStreamInterfaces.begin(); i != pStreamInterfaces.end(); ++i)
            delete *i;
        pStreamInterfaces.clear();

        for (vector<triangle_mesh*>::iterator i = meshPtrs.begin(); i != meshPtrs.end(); ++i)
            delete *i;
        meshPtrs.clear();

        if (pSaver != 0)
		{
            delete pSaver;
			pSaver = 0;
		}

        return CStatus::Fail;
    }
    
    return CStatus::OK;
}

SICALLBACK KrakatoaSR_Cleanup( CRef& in_context )
{ 
    Application().LogMessage("KrakatoaSR Cleanup",siInfoMsg);
    Context context(in_context);
    Renderer renderer(context.GetSource());

    return CStatus::OK;
}

SICALLBACK KrakatoaSR_Abort( CRef& in_context )
{ 
    Application().LogMessage("KrakatoaSR Abort",siInfoMsg);
	
	// the is volatile so we can just set it and the checker thread should pick up the new value next time it checks
	g_shouldAbort = true; 

    return CStatus::OK;
}
