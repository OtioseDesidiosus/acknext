#include "include/acknext/ext/terrain.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>

#include <acknext/acff.h>
#include <acknext/extension.h>
#include <acknext/serialization.h>

#include <ode/common.h>
#include <ode/collision.h>

#include "l3dt/l3dt.h"

#include "resource-compiler.h"

DECLARE_RESOURCE(terrain_frag)
DECLARE_RESOURCE(terrain_tesc)
DECLARE_RESOURCE(terrain_tese)
DECLARE_RESOURCE(terrain_vert)

static const ACKGUID terrainguid = {{
	0x55, 0x07, 0xC2, 0x7D,
	0x37, 0xB7, 0x46, 0x23,
	0xB3, 0x63, 0xED, 0x27,
	0x3E, 0x7B, 0xA9, 0x62
}};

static SHADER * shdTerrain;

static ACKTYPE canLoad(ACKGUID const * guid)
{
	if(memcmp(guid, &terrainguid, sizeof(ACKGUID)) == 0) {
		return TYPE_MODEL;
	}
	return TYPE_INVALID;
}

static dGeomID terrainCreateCollider(dSpaceID space, struct MODEL * model)
{
	assert(space != NULL);
	assert(model != NULL);
	GLenum type;
	dHeightfieldDataID const * data = (dHeightfieldDataID*)obj_getvar(model, "terrain-collider-data", &type);
	assert(data != NULL);
	assert(type == ACK_POINTER);
	assert(*data != NULL);
	return dCreateHeightfield(space, *data, 1);
}

static MODEL * terrain_load(ACKFILE * file, ACKGUID const * guid)
{
	assert(guid_compare(guid, &terrainguid));

	BLOB * packed = blob_load("/terrain/GrassyMountains_HF.hfz");
	BLOB * unpacked = blob_inflate(packed);

	blob_remove(packed);

	L3Heightfield * hf = l3hf_decode(unpacked->data, unpacked->size);

	BITMAP * heightmapTexture = bmap_create(GL_TEXTURE_2D, GL_R32F);

	bmap_set(heightmapTexture, hf->width, hf->height, GL_RED, GL_FLOAT, hf->data);

	bmap_to_mipmap(heightmapTexture);

	GLuint hmp = heightmapTexture->object;
	glTextureParameteri(hmp, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTextureParameteri(hmp, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(hmp, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	const int subdiv = 6;
	const int tilesX = (1<<subdiv);
	const int tilesY = (1<<subdiv);
	const int sizeX = hf->width / tilesX;
	const int sizeY = hf->height / tilesY;

	engine_log("(%d,%d) → (%d, %d)", tilesX, tilesY, sizeX, sizeY);

	BUFFER * indexBuffer = buffer_create(INDEXBUFFER);
	buffer_set(indexBuffer, 4 * sizeof(INDEX) * tilesX * tilesY, NULL);
	INDEX * indices = buffer_map(indexBuffer, WRITEONLY);

	const int stride = hf->width;
	const int delta = 1;
	for(int y = 0; y < tilesY; y++)
	{
		for(int x = 0; x < tilesX; x++)
		{
			INDEX * face = &indices[4 * (y * tilesX + x)];
			int base = stride * y + delta * x;
			face[0] = base + 0;
			face[1] = base + stride;
			face[2] = base + stride + delta;
			face[3] = base + delta;
		}
	}

	buffer_unmap(indexBuffer);

	MESH * mesh = mesh_create(GL_QUADS, NULL, indexBuffer);
	{
		BITMAP * bmpNM    = bmap_to_mipmap(bmap_load("/terrain/GrassyMountains_TN.png"));

		mesh_setvar(mesh, "vecTerrainSize", GL_INT_VEC2, hf->width, hf->height);
		mesh_setvar(mesh, "vecTileSize", GL_INT_VEC2, sizeX, sizeY);
		mesh_setvar(mesh, "texHeightmap", GL_SAMPLER_2D, heightmapTexture);
		mesh_setvar(mesh, "fTerrainScale", GL_FLOAT, hf->horizontalScale);
		mesh_setvar(mesh, "texNormalMap", GL_SAMPLER_2D, bmpNM);

		mesh->boundingBox = (AABB){
		    minimum: { 1, 1, 1 },
		    maximum: { -1, -1, -1 },
		};
	}

	MODEL * model = model_create(1, 0, 0);
	{
		model->meshes[0] = mesh;
		model->materials[0] = mtl_create();
		model->materials[0]->shader = shdTerrain;

		*((AABB*)&model->boundingBox) = (AABB){
			minimum: { 1, 1, 1 },
			maximum: { -1, -1, -1 },
		};
	}

	packed = blob_load("/terrain/GrassyMountains_AM.amf.gz");
	BLOB * attributefield = blob_inflate(packed);
	blob_remove(packed);

	L3Attributefield * am = l3af_decode(attributefield->data, attributefield->size);
	if(am->width != hf->width || am->height != hf->height) {
		abort();
	}

	BITMAP * amtexture = bmap_create(GL_TEXTURE_2D, GL_RG8UI);
	bmap_set(amtexture,
		am->width, am->height,
		GL_RG_INTEGER, GL_UNSIGNED_BYTE,
		am->data);
	shader_setvar(model->materials[0], "texTerrainMaterial", GL_UNSIGNED_INT_SAMPLER_2D, amtexture);

	l3af_free(am);

	BITMAP * materials = bmap_create(GL_TEXTURE_2D_ARRAY, GL_RGBA8);
	{
		glTextureStorage3D(
			materials->object,
			10,
			materials->format,
			1024,
			1024,
			18);
		struct { int index; char const * file; } maps[] =
		{
			{ 0x0B, "/textures/TexturesCom_Grass0160_1_seamless_S.jpg" },
			{ 0x0C, "/textures/TexturesCom_Grass0169_1_seamless_S.jpg" },
			{ 0x0E, "/textures/TexturesCom_Grass0157_1_seamless_S.jpg" },
			{ 0x0F, "/textures/TexturesCom_GrassDead0101_1_seamless_S.jpg" },
			{ 0x10, "/textures/TexturesCom_Cliffs0167_1_seamless_S.jpg" },
			{ 0x11, "/textures/TexturesCom_Cliffs0177_1_seamless_S.jpg" },
			{ 0, NULL },
		};
		for(int i = 0; maps[i].file; i++)
		{
			BITMAP * source = bmap_load(maps[i].file);
			assert(source && source->width == 1024 && source->height == 1024);
			glTextureSubImage3D(
				materials->object,
				0,
				0, 0, maps[i].index,
				1024, 1024, 1,
				GL_BGRA, GL_UNSIGNED_BYTE,
				source->pixels);
			bmap_remove(source);
		}
		glGenerateTextureMipmap(materials->object);

		/*
		glTextureParameterf(
			materials->object,
			GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest);
		*/
		shader_setvar(model->materials[0], "texTerrainMaterials", GL_SAMPLER_2D_ARRAY, materials);
	}

	{ // Setup heightmap data
		dHeightfieldDataID heightfield = dGeomHeightfieldDataCreate();

		engine_log("%d, %d", hf->width, hf->height);

//		dGeomHeightfieldDataBuildCallback(
//			heightfield,
//			hf,
//			terrainReadHeight,
//			hf->horizontalScale * (hf->width - 1), hf->horizontalScale * (hf->height - 1),
//			hf->width, hf->height,
//			1.0, 1.0,
//			10.0,
//			0);

		dGeomHeightfieldDataBuildSingle(
			heightfield,
			hf->data,
			0, // please reference hf data, not copy
			hf->horizontalScale * (hf->width - 1), hf->horizontalScale * (hf->height - 1), // real size
			hf->width, hf->height, // data point size
			1.0, 0.0, // no scale/offset, we have already correct height data
			1000.0, // 1km thick terrain
			0); // no wrap

		var min =  FLT_MAX;
		var max = -FLT_MAX;
		for(int z = 0; z < hf->height; z++)
		{
			for(int x = 0; x < hf->width; x++)
			{
				var h = hf->data[z * hf->width + x];
				min = minv(min, h);
				max = maxv(max, h);
			}
		}

		dGeomHeightfieldDataSetBounds(
			heightfield,
			min,
			max);

		engine_log("min,max: %f %f", min, max);

		obj_setvar(model, "terrain-collider-data", ACK_POINTER, heightfield);

		model->createCollider = &terrainCreateCollider;
	}

	obj_setvar(model, "terrain-data", ACK_POINTER, hf);


	if(true)
	{
		// writeout terrain file
		ACKFILE * file = file_open_write("terrain.esd");
		file_write_header(file, TYPE_MODEL, terrainguid);



		file_close(file);
	}


	return model;
}

float terrain_getheight(MODEL * terrain, float x, float z)
{
	if(terrain == NULL) {
		engine_seterror(ERR_INVALIDARGUMENT, "terrain was NULL!");
		return 0.0;
	}

	GLenum type;
	L3Heightfield * const * hfptr = obj_getvar(terrain, "terrain-data", &type);
	if(type != ACK_POINTER) {
		engine_seterror(ERR_INVALIDARGUMENT, "terrain model was not a terrain!");
		return 0.0;
	}

	return l3hf_get(*hfptr, x, z);
}









static EXTENSION terraX =
{
	.canLoad   = canLoad,
    .loadModel = terrain_load,
};

void terrainmodule_init()
{
	engine_log("Initialize terrain extension...");
	ext_register("Acknext Terrain Module", &terraX);

	shdTerrain = shader_create();
	{
		shader_addSourceExt(
			shdTerrain,
			VERTEXSHADER,
			RESOURCE_DATA(terrain_vert),
			RESOURCE_SIZE(terrain_vert));

		shader_addSourceExt(
			shdTerrain,
			TESSCTRLSHADER,
			RESOURCE_DATA(terrain_tesc),
			RESOURCE_SIZE(terrain_tesc));

		shader_addSourceExt(
			shdTerrain,
			TESSEVALSHADER,
			RESOURCE_DATA(terrain_tese),
			RESOURCE_SIZE(terrain_tese));

		shader_addSourceExt(
			shdTerrain,
			FRAGMENTSHADER,
			RESOURCE_DATA(terrain_frag),
			RESOURCE_SIZE(terrain_frag));

		shader_addFileSource(shdTerrain, FRAGMENTSHADER, "/builtin/shaders/lighting.glsl");
		shader_addFileSource(shdTerrain, FRAGMENTSHADER, "/builtin/shaders/gamma.glsl");
		shader_addFileSource(shdTerrain, FRAGMENTSHADER, "/builtin/shaders/ackpbr.glsl");
		shader_addFileSource(shdTerrain, FRAGMENTSHADER, "/builtin/shaders/fog.glsl");
		shader_link(shdTerrain);

		int iSubdivision;
		glGetIntegerv(GL_MAX_TESS_GEN_LEVEL, &iSubdivision);
		engine_log("Maximum subdivision: %d", iSubdivision);

		shader_setvar(shdTerrain, "iSubdivision", GL_INT, iSubdivision);
		shader_setvar(shdTerrain, "vecTesselationParameters", GL_FLOAT_VEC3,
			20.0f, // tesselation rate
			32.0f, // ???
			50.0f  // ???
		);
	}

	// Setup the shader uniforms of itself
	shader_setUniforms(shdTerrain, shdTerrain, true);
}
