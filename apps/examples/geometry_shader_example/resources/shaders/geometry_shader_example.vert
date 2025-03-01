#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
// vertex shader

// -- Uniform inputs

layout (set=0, binding=1) uniform GeometryShaderExampleParams {
// uCanvas:
// .x -> global canvas height (in pixels) 
// .y -> global canvas width (in pixels)
// .z -> identity distance, that is the distance at which canvas is rendered 1:1
	vec3 uCanvas; 
	vec3 uGeometryShaderExampleSource; ///< source of flare in screen space
	float uHowClose;
};

// -- Attribute inputs

layout (location = 0) in vec4 position;

// -- Outputs

layout ( location = 0 ) out VertexAttrib 
{
	     vec3  position;
	flat int   flare_type;
	flat float radius;
	flat float distanceToBorder;
	flat float rotation;
	flat float intensity;
} vertex;

void main()
{

	// the vertex attribute holds lens flare specific data:

	// the type of flare is set through first attribute
	vertex.flare_type = int(position.x);

	// brightness trigger point for types of flares that have dynamic triggers.
	float fGeometryShaderExampleTriggerPointOnAxis = position.y;

	// radius: 
	float fGeometryShaderExampleRadius = position.w;

	// fGeometryShaderExamplePositionOnAxis is a scale factor to place the flare along the axis.
	// a scale factor of 0.5 means we're at screen centre, 
	// a scale factor of 0 means we're on the flare point.
	float fGeometryShaderExamplePositionOnAxis = position.z;

	// uGeometryShaderExampleSource is already in screen space, needs to be scaled 
	// and translated so that we can render it 
	// as if it was placed on a screen plane at identity distance.
	// make sure position.z is 1 for unit distance 
	// note that z is linear, here!

	// we first calculate the position of the flare in screen space, placing 
	// it on our virtual image plane. 

	vec3 sourceInClipSpace = vec3(uGeometryShaderExampleSource.xy, 1) * 
	vec3(uCanvas.x * 0.5, - uCanvas.y * 0.5, uCanvas.z *0.5); // calculate position in window space.

	// we then calculate the position of the virtual screen centre in NDC space
	vec3 screenCentre = vec3( 0, 0, -uCanvas.z*0.5 );
	// and the direction of an axis going from the flare through the NDC centre
	vec3 flareAxisDirection = (sourceInClipSpace - screenCentre);

	vertex.position = (screenCentre - flareAxisDirection * (fGeometryShaderExamplePositionOnAxis - 0.5) );

	vec3 triggerPoint = (screenCentre - flareAxisDirection * (fGeometryShaderExampleTriggerPointOnAxis - 0.5) ) ;
	
	float distanceToBorder = min(
		abs(uCanvas.x * 0.25) - abs(triggerPoint.x),
		abs(uCanvas.y * 0.25) - abs(triggerPoint.y)) / (uCanvas.x * 0.25); //< divide by largest possible distance. you need to change this to .y if canvas height < canvas width

	vertex.rotation = atan(triggerPoint.y,triggerPoint.x); // gets rotation in range -pi,pi
	vertex.radius = fGeometryShaderExampleRadius;
	vertex.distanceToBorder = distanceToBorder;
	vertex.intensity = (1.0-smoothstep(500,1300,uHowClose));
}
