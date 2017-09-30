#include <engine.hpp>

#include "mesh.hpp"
#include "model.hpp"
#include "camera.hpp"
#include "ackglm.hpp"
#include "../../scene/entity.hpp"
#include "../opengl/shader.hpp"

#include "../debug/debugdrawer.hpp"

#include <vector>
#include <algorithm>
#include <map>
#include <unordered_map>

#define LIGHT_LIMIT 16

extern Shader * defaultShader;

Shader * FB(Shader * sh)
{
	if(sh) return sh;
	return defaultShader;
}

struct LIGHTDATA
{
	__attribute__((aligned(4))) int type;
	__attribute__((aligned(4))) float intensity;
	__attribute__((aligned(4))) float arc;
	__attribute__((aligned(16))) VECTOR position;
	__attribute__((aligned(16))) VECTOR direction;
	__attribute__((aligned(16))) COLOR color;
};

static BUFFER * ubo = nullptr;
static BUFFER * bonesBuf = nullptr;
static BUFFER * instaBuf = nullptr;

extern Shader * currentShader;

static void setupLights()
{
	GLint block_index = glGetUniformBlockIndex(
		currentShader->api().object,
		"LightBlock");
	if(block_index >= 0) // only when lights are required
	{
		int lcount = 0;
		LIGHTDATA * lights = (LIGHTDATA*)glMapNamedBuffer(
					ubo->object,
					GL_WRITE_ONLY);
		for(LIGHT * l = light_next(nullptr); l != nullptr; l = light_next(l))
		{
			lights[lcount].type = l->type;
			lights[lcount].intensity = l->intensity;
			lights[lcount].arc = cos(0.5 * DEG_TO_RAD * l->arc); // arc is full arc, but cos() is half-arc
			lights[lcount].position = l->position;
			lights[lcount].direction = l->direction;
			lights[lcount].color = l->color;

			vec_normalize(&lights[lcount].direction, 1.0);
			lcount += 1;
			if(lcount >= LIGHT_LIMIT) {
				break;
			}
		}
		glUnmapNamedBuffer(ubo->object);

		GLuint binding_point_index = 2;
		glBindBufferBase(
			GL_UNIFORM_BUFFER,
			binding_point_index,
			ubo->object);
		glUniformBlockBinding(
			currentShader->api().object,
			block_index,
			binding_point_index);
		currentShader->iLightCount = lcount;
	} else {
		currentShader->iLightCount = 0;
	}
}

static void setupBones()
{
	GLint block_index = glGetUniformBlockIndex(
		currentShader->api().object,
		"BoneBlock");
	if(block_index >= 0)
	{
		GLuint binding_point_index = 4;
		glBindBufferBase(
			GL_UNIFORM_BUFFER,
			binding_point_index,
			bonesBuf->object);
		glUniformBlockBinding(
			currentShader->api().object,
			block_index,
			binding_point_index);
	}
}

struct Drawcall
{
	ENTITY const * ent = nullptr;
	MODEL const * model = nullptr;
	MESH const * mesh = nullptr;
	MATERIAL const * material = nullptr;
	MATRIX matWorld;
	bool renderDoubleSided;

	Drawcall() = default;
	Drawcall(Drawcall const &) = default;
	Drawcall(Drawcall &&) = default;
	~Drawcall() = default;

	Drawcall & operator =(Drawcall const &) = default;

	void exec()
	{
		opengl_drawMesh(mesh);
	}
};

static bool cull(MATRIX const & frustrum, AABB const & aabb)
{
	if((aabb.minimum.x > aabb.maximum.x) || (aabb.minimum.y > aabb.maximum.y) || (aabb.minimum.z > aabb.maximum.z))
	{
		return false;
	}

	VECTOR4 points[8] = {
	    { aabb.minimum.x, aabb.minimum.y, aabb.minimum.z, 1 },
	    { aabb.minimum.x, aabb.minimum.y, aabb.maximum.z, 1 },
	    { aabb.minimum.x, aabb.maximum.y, aabb.minimum.z, 1 },
	    { aabb.minimum.x, aabb.maximum.y, aabb.maximum.z, 1 },
	    { aabb.maximum.x, aabb.minimum.y, aabb.minimum.z, 1 },
	    { aabb.maximum.x, aabb.minimum.y, aabb.maximum.z, 1 },
	    { aabb.maximum.x, aabb.maximum.y, aabb.minimum.z, 1 },
	    { aabb.maximum.x, aabb.maximum.y, aabb.maximum.z, 1 },
	};
	for(int i = 0; i < 8; i++)
	{
		vec4_transform(&points[i], &frustrum);
		points[i].x /= points[i].w;
		points[i].y /= points[i].w;
		points[i].z /= points[i].w;
	}

	if(std::all_of(&points[0], &points[8], [](VECTOR4 const & pt) -> bool {
				return (pt.x < -1.0);
			}))
		return true;

	if(std::all_of(&points[0], &points[8], [](VECTOR4 const & pt) -> bool {
				return (pt.x > 1.0);
			}))
		return true;

	if(std::all_of(&points[0], &points[8], [](VECTOR4 const & pt) -> bool {
				return (pt.y < -1.0);
			}))
		return true;

	if(std::all_of(&points[0], &points[8], [](VECTOR4 const & pt) -> bool {
				return (pt.y > 1.0);
			}))
		return true;

	if(std::all_of(&points[0], &points[8], [](VECTOR4 const & pt) -> bool {
				return (pt.z < -1.0);
			}))
		return true;

	if(std::all_of(&points[0], &points[8], [](VECTOR4 const & pt) -> bool {
				return (pt.z > 1.0);
			}))
		return true;

	return false;
}

struct Drawgroup
{
	MATERIAL const * mtl = nullptr;
	MESH const * mesh = nullptr;
	MODEL const * model = nullptr;
	bool doublesided = false;
};

static bool operator ==(Drawgroup const & lhs, Drawgroup const & rhs)
{
	return lhs.mtl == rhs.mtl
		&& lhs.model == rhs.model
		&& lhs.mesh == rhs.mesh
		&& lhs.doublesided == rhs.doublesided;
}

class DrawgroupHash
{
public:
    size_t operator()(const Drawgroup &group) const
    {
        size_t h1 = reinterpret_cast<size_t>(group.mtl);
		size_t h2 = reinterpret_cast<size_t>(group.model);
        size_t h3 = reinterpret_cast<size_t>(group.mesh);
		size_t h4 = std::hash<bool>()(group.doublesided);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

struct Instance
{
	// NOTE: MUST BE FIRST MEMBER OF
	// STRUCT, OTHERWISE THE VERTEX ARRAY LAYOUT
	// WILL MESS UP!
	MATRIX transform;
	ENTITY const * ent = nullptr;
} __attribute__((packed));

extern GLuint vao;

static void render_scene(CAMERA * perspective, MATERIAL * mtlOverride);

static SHADER * create_ppshader(char const * pixelop)
{
	SHADER * ppshader = shader_create();

	shader_addFileSource(ppshader, VERTEXSHADER, "/builtin/shaders/postprocess.vert");
	shader_addFileSource(ppshader, FRAGMENTSHADER, "/builtin/shaders/gamma.glsl");
	shader_addFileSource(ppshader, FRAGMENTSHADER, pixelop);

	shader_link(ppshader);

	return ppshader;
}

ACKNEXT_API_BLOCK
{
	CAMERA * camera;

	COLOR sky_color = { 0.3, 0.7, 1.0, 1.0 };

	var lod_distances[16] =
	{
		0.5,
	    1.0,
	    5.0,
	    10.0,
	    15.0,
	    25.0,
	    50.0,
	    75.0,
	    100.0,
	    250.0,
	    500.0,
	    1000.0,
	    2500.0,
	    4000.0,
	    10000.0,
		20000.0,
	};

	void render_scene_with_camera(CAMERA * perspective)
	{
		GLint drawFboId = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFboId);

		engine_log("Current FB: %d", drawFboId);

		static FRAMEBUFFER * stage1 = nullptr;
		static FRAMEBUFFER * stage2 = nullptr;
		static FRAMEBUFFER * stage3 = nullptr;

		if(stage1 == nullptr)
		{
			stage1 = framebuf_create();
			stage1->targets[0] = bmap_create(GL_TEXTURE_2D, GL_RGBA32F);
			stage1->depthBuffer = bmap_create(GL_TEXTURE_2D, GL_DEPTH24_STENCIL8);
			framebuf_update(stage1);
		}

		if(stage2 == nullptr)
		{
			stage2 = framebuf_create();
			stage2->targets[0] = bmap_create(GL_TEXTURE_2D, GL_RGBA32F);
			stage2->depthBuffer = bmap_create(GL_TEXTURE_2D, GL_DEPTH24_STENCIL8);
			framebuf_update(stage2);
		}

		if(stage3 == nullptr)
		{
			stage3 = framebuf_create();
			stage3->targets[0] = bmap_create(GL_TEXTURE_2D, GL_RGBA32F);
			stage3->depthBuffer = bmap_create(GL_TEXTURE_2D, GL_DEPTH24_STENCIL8);
			framebuf_update(stage3);
		}

		static SHADER * tonemap = nullptr;

		static SHADER * bloomblur = nullptr;
		static SHADER * bloomcomb = nullptr;

		if(!tonemap) tonemap = create_ppshader("/builtin/shaders/pp/hdr/reinhard.frag");

		if(!bloomblur) bloomblur = create_ppshader("/builtin/shaders/pp/bloom/blur.frag");
		if(!bloomcomb) bloomcomb = create_ppshader("/builtin/shaders/pp/bloom/combine.frag");

		SIZE targetSize;
		view_to_bounds(view_current, nullptr, &targetSize);

		{ // 1: render scnee
			framebuf_resize(stage1, targetSize);
			opengl_setFrameBuffer(stage1);

			render_scene(perspective, nullptr);
		}

		glDisable(GL_CULL_FACE);
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);

		{ // 2: render bloom image (half size)
			SIZE halfSize = targetSize;
			halfSize.width /= 2;
			halfSize.height /= 2;
			framebuf_resize(stage2, halfSize);
			opengl_setFrameBuffer(stage2);

			opengl_setShader(bloomblur);
			currentShader->texInput = stage1->targets[0];

			opengl_drawFullscreenQuad();
		}

		{ // 3: combine bloom image with source image
			framebuf_resize(stage3, targetSize);
			opengl_setFrameBuffer(stage3);

			opengl_setShader(bloomcomb);
			currentShader->texInput = stage1->targets[0];
			currentShader->texBloom = stage2->targets[0];

			opengl_drawFullscreenQuad();
		}

		{ // 4:
			opengl_setFrameBuffer(nullptr);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFboId);

			opengl_setShader(tonemap);

			currentShader->texInput = stage3->targets[0];
			currentShader->fExposure = 1.0;

			opengl_drawFullscreenQuad();
		}
	}
}

static void render_scene(CAMERA * perspective, MATERIAL * mtlOverride)
{
	glBindVertexArray(vao);

	if(perspective == nullptr) {
		return;
	}

	if(!ubo)
	{
		ubo = buffer_create(UNIFORMBUFFER);
		buffer_set(ubo, sizeof(LIGHTDATA) * LIGHT_LIMIT, nullptr);
	}

	if(!bonesBuf)
	{
		bonesBuf = buffer_create(UNIFORMBUFFER);
		buffer_set(bonesBuf, sizeof(MATRIX) * ACKNEXT_MAX_BONES, NULL);
	}

	if(!instaBuf)
	{
		instaBuf = buffer_create(VERTEXBUFFER);
	}

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	if(opengl_wireFrame) {
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
	} else {
		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	}

	glEnable(GL_DEPTH_TEST);
	glClearColor(sky_color.red, sky_color.green, sky_color.blue, sky_color.alpha);
	glClearDepth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_BLEND);

	MATRIX matView, matProj;
	camera_to_matrix(perspective, &matView, &matProj, view_current);

	std::vector<Drawcall> drawcalls;

	for(ENTITY * ent = ent_next(nullptr); ent != nullptr; ent = ent_next(ent))
	{
		// Entity * entity = promote<Entity>(ent);
		if(ent->model == nullptr)
			continue;
		if(!(ent->flags & VISIBLE))
			continue;
		// TODO: Filter entity by mask bits

		MATRIX matWorld;
		glm_to_ack(&matWorld,
			glm::translate(glm::mat4(), ack_to_glm(ent->position)) *
			glm::mat4_cast(ack_to_glm(ent->rotation)) *
			glm::scale(glm::mat4(), ack_to_glm(ent->scale)));

		MATRIX matWorldViewProj;
		glm_to_ack(&matWorldViewProj,
			  ack_to_glm(matProj)
			* ack_to_glm(matView)
			* ack_to_glm(matWorld));

		var dist = vec_dist(&camera->position, &ent->position);
		uint lod;
		for(lod = 15; lod_distances[lod] > dist && lod > 0; lod--);

		if(lod >= 16)
			continue;

		if(lod > ent->model->minimumLOD)
			continue;

		Model * model = promote<Model>(ent->model);
		for(int i = 0; i < model->api().meshCount; i++)
		{
			Drawcall call;

			if(mtlOverride != nullptr)
			{
				call.material = mtlOverride;
			}
			else
			{
				if(ent->material == nullptr)
					call.material = model->api().materials[i];
				else
					call.material = ent->material;
			}

			call.matWorld = matWorld;
			call.model = demote(model);
			call.mesh = model->api().meshes[i];
			call.ent = ent;
			call.renderDoubleSided = !!(call.mesh->lodMask & DOUBLESIDED);

			// Only allow rendering of meshes when the
			// LOD is enabled in the MESH
			if(call.mesh->lodMask & (1<<lod)) {
				// And only render it, when the
				// mesh is actually visible
				// if(!cull(matWorldViewProj, call.mesh->boundingBox))
				{
					drawcalls.push_back(call);
				}
			}
		}
	}

	{
		std::unordered_map<Drawgroup, std::vector<Instance>, DrawgroupHash> groups;
		for(auto & call : drawcalls)
		{
			Drawgroup group;
			group.mtl = call.material;
			group.mesh = call.mesh;
			group.model = call.model;
			group.doublesided = call.renderDoubleSided;

			Instance instance;
			instance.ent = call.ent;
			instance.transform = call.matWorld;

			groups[group].push_back(instance);
		}

//			engine_log("start rendering");
		for(auto & entry : groups)
		{
			Drawgroup const & params = entry.first;
			std::vector<Instance> const & instances = entry.second;

			// Setup:
			{
				opengl_setMaterial(params.mtl);

				currentShader->matView = matView;
				currentShader->matProj = matProj;

				currentShader->vecViewPos = perspective->position;
				static const COLOR fog = {152/255.0,179/255.0,166/255.0,0.0003};
				currentShader->vecFogColor = fog;
				currentShader->fArc = tan(0.5 * DEG_TO_RAD * perspective->arc);

				setupLights();
				setupBones();
			}

			shader_setUniforms(&currentShader->api(), params.model, false);
			shader_setUniforms(&currentShader->api(), params.mesh, false);

			// Animated models have a slight problem:
			// They can't be instanced!
			bool useInstancing =
				   (currentShader->api().flags & USE_INSTANCING)
				&& ((params.mesh->lodMask & ANIMATED) == 0);

//				engine_log("Render %5d of (mtl=%p model=%p mesh=%p dsr=%d)%s",
//					int(instances.size()),
//					params.mtl,
//					params.model,
//					params.mesh,
//					params.doublesided,
//					(useInstancing) ? " instanced" : "");

			if(params.doublesided)
				glDisable(GL_CULL_FACE);
			else
				glEnable(GL_CULL_FACE);

			if(useInstancing == false)
			{
				for(Instance const & inst : instances)
				{
					MATRIX animatedBones[ACKNEXT_MAX_BONES];
					for(int i = 0; i < ACKNEXT_MAX_BONES; i++)
					{
						MATRIX & transform = animatedBones[i];
						mat_id(&transform);
						mat_translate(&transform, &inst.ent->pose[i].position);
						mat_rotate(&transform, &inst.ent->pose[i].rotation);
						mat_scale(&transform, &inst.ent->pose[i].scale);
					}

					MATRIX transforms[ACKNEXT_MAX_BONES];
					transforms[0] = animatedBones[0];
					for(int i = 1; i < inst.ent->model->boneCount; i++)
					{
						BONE * bone = &inst.ent->model->bones[i];
						mat_mul(&transforms[i], &animatedBones[i], &transforms[bone->parent]);
					}

					MATRIX * boneTrafos = (MATRIX*)buffer_map(bonesBuf, READWRITE);
					for(int i = 0; i < inst.ent->model->boneCount; i++)
					{
						BONE * bone = &inst.ent->model->bones[i];
						mat_mul(&boneTrafos[i], &bone->bindToBoneTransform, &transforms[i]);
					}
					buffer_unmap(bonesBuf);

					currentShader->useInstancing = false;
					currentShader->useBones = true;
					currentShader->matWorld = inst.transform;
					opengl_drawMesh(params.mesh);
				}
			}
			else
			{
				MATRIX transforms[ACKNEXT_MAX_BONES];
				transforms[0] = params.model->bones[0].transform;
				for(int i = 1; i < params.model->boneCount; i++)
				{
					BONE const * bone = &params.model->bones[i];
					mat_mul(&transforms[i], &bone->transform, &transforms[bone->parent]);
				}

				MATRIX * boneTrafos = (MATRIX*)buffer_map(bonesBuf, READWRITE);
				for(int i = 0; i < params.model->boneCount; i++)
				{
					BONE const * bone = &params.model->bones[i];
					mat_mul(&boneTrafos[i], &bone->bindToBoneTransform, &transforms[i]);
				}
				buffer_unmap(bonesBuf);

				currentShader->useInstancing = true;
				currentShader->useBones = false;

				// TODO: Implement instance buffer cycling
				{
					size_t size = instances.size() * sizeof(Instance);
					if(size <= instaBuf->size) {
						buffer_update(
							instaBuf,
							0,
							size,
							instances.data());
					} else {
						buffer_set(
							instaBuf,
							size,
							instances.data());
					}
				}

				glVertexArrayVertexBuffer(
					vao,
					12,
					instaBuf->object,
					0,
					sizeof(Instance));
				glVertexArrayBindingDivisor(
					vao,
					12,
					1);

				currentShader->matWorld = MATRIX { 0 };

				int count;
				GLenum type = opengl_setMesh(params.mesh, &count);

				opengl_draw(
					type,
					0,
					count,
					instances.size());
			}
		}
	}

	DebugDrawer::render(matView, matProj);
}
