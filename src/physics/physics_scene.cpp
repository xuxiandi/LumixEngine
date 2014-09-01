#include "physics/physics_scene.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <PxPhysicsAPI.h>
#include "cooking/PxCooking.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/iserializer.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "engine/engine.h"
#include "graphics/render_scene.h"
#include "graphics/texture.h"
#include "physics/physics_system.h"
#include "physics/physics_system_impl.h"


namespace Lumix
{


static const uint32_t BOX_ACTOR_HASH = crc32("box_rigid_actor");
static const uint32_t MESH_ACTOR_HASH = crc32("mesh_rigid_actor");
static const uint32_t CONTROLLER_HASH = crc32("physical_controller");
static const uint32_t HEIGHTFIELD_HASH = crc32("physical_heightfield");


struct OutputStream : public physx::PxOutputStream
{
	OutputStream()
	{
		data = LUMIX_NEW_ARRAY(uint8_t, 4096);
		capacity = 4096;
		size = 0;
	}

	~OutputStream()
	{
		LUMIX_DELETE_ARRAY(data);
	}


	virtual physx::PxU32 write(const void* src, physx::PxU32 count)
	{
		if (size + (int)count > capacity)
		{
			int new_capacity = Math::maxValue(size + (int)count, capacity + 4096);
			uint8_t* new_data = LUMIX_NEW_ARRAY(unsigned char, new_capacity);
			memcpy(new_data, data, size);
			LUMIX_DELETE_ARRAY(data);
			data = new_data;
			capacity = new_capacity;
		}
		memcpy(data + size, src, count);
		size += count;
		return count;
	}

	uint8_t* data;
	int capacity;
	int size;
};


struct InputStream : public physx::PxInputStream
{
	InputStream(unsigned char* data, int size)
	{
		this->data = data;
		this->size = size;
		pos = 0;
	}

	virtual physx::PxU32 read(void* dest, physx::PxU32 count)
	{
		if (pos + (int)count <= size)
		{
			memcpy(dest, data + pos, count);
			pos += count;
			return count;
		}
		else
		{
			memcpy(dest, data + pos, size - pos);
			int real_count = size - pos;
			pos = size;
			return real_count;
		}
	}


	int pos;
	int size;
	unsigned char* data;
};


static void matrix2Transform(const Matrix& mtx, physx::PxTransform& transform)
{
	transform.p.x = mtx.m41;
	transform.p.y = mtx.m42;
	transform.p.z = mtx.m43;
	Quat q;
	mtx.getRotation(q);
	transform.q.x = q.x;
	transform.q.y = q.y;
	transform.q.z = q.z;
	transform.q.w = q.w;
}


class Terrain
{
	public:
		Terrain();
		~Terrain();
		void heightmapLoaded(Resource::State, Resource::State new_state);

		PhysicsSceneImpl* m_scene;
		Entity m_entity;
		physx::PxRigidActor* m_actor;
		Texture* m_heightmap;
		float m_xz_scale;
		float m_y_scale;
};


struct PhysicsSceneImpl : public PhysicsScene
{
	enum ActorType
	{
		BOX
	};
	

	~PhysicsSceneImpl()
	{
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			LUMIX_DELETE(m_terrains[i]);
		}
	}


	virtual IPlugin& getPlugin() const override
	{
		return *m_system;
	}


	virtual Component createComponent(uint32_t component_type, const Entity& entity) override
	{
		if (component_type == HEIGHTFIELD_HASH)
		{
			return createHeightfield(entity);
		}
		else if (component_type == CONTROLLER_HASH)
		{
			return createController(entity);
		}
		else if (component_type == BOX_ACTOR_HASH)
		{
			return createBoxRigidActor(entity);
		}
		else if (component_type == MESH_ACTOR_HASH)
		{
			return createMeshRigidActor(entity);
		}
		return Component::INVALID;
	}


	virtual void destroyComponent(const Component& cmp) override
	{
		if(cmp.type == HEIGHTFIELD_HASH)
		{
			LUMIX_DELETE(m_terrains[cmp.index]);
			m_terrains[cmp.index] = NULL;
			m_universe->destroyComponent(cmp);
			m_universe->componentDestroyed().invoke(cmp);
		}
		else if(cmp.type == CONTROLLER_HASH)
		{
			m_controllers[cmp.index].m_is_free = true;
			m_universe->destroyComponent(cmp);
			m_universe->componentDestroyed().invoke(cmp);
		}
		else if (cmp.type == MESH_ACTOR_HASH || cmp.type == BOX_ACTOR_HASH)
		{
			m_actors[cmp.index]->m_entity.index = -1;
			m_universe->destroyComponent(cmp);
			m_universe->componentDestroyed().invoke(cmp);
		}
		else
		{
			ASSERT(false);
		}
	}


	Component createHeightfield(Entity entity)
	{
		Terrain* terrain = LUMIX_NEW(Terrain);
		m_terrains.push(terrain);
		terrain->m_heightmap = NULL;
		terrain->m_scene = this;
		terrain->m_actor = NULL;
		terrain->m_entity = entity;
		Component cmp = m_universe->addComponent(entity, HEIGHTFIELD_HASH, this, m_terrains.size() - 1);
		m_universe->componentCreated().invoke(cmp);
		return cmp;
	}


	Component createController(Entity entity)
	{
		physx::PxCapsuleControllerDesc cDesc;
		cDesc.material = m_default_material;
		cDesc.height = 1.8f;
		cDesc.radius = 0.25f;
		cDesc.slopeLimit = 0.0f;
		cDesc.contactOffset = 0.1f;
		cDesc.stepOffset = 0.02f;
		cDesc.callback = NULL;
		cDesc.behaviorCallback = NULL;
		Vec3 position = entity.getPosition();
		cDesc.position.set(position.x, position.y, position.z);
		PhysicsSceneImpl::Controller c;
		c.m_controller = m_system->m_impl->m_controller_manager->createController(*m_system->m_impl->m_physics, m_scene, cDesc);
		c.m_entity = entity;
		c.m_is_free = false;

		m_controllers.push(c);

		Component cmp = m_universe->addComponent(entity, CONTROLLER_HASH, this, m_controllers.size() - 1);
		m_universe->componentCreated().invoke(cmp);
		return cmp;
	}


	Component createBoxRigidActor(Entity entity)
	{
		RigidActor* actor = LUMIX_NEW(RigidActor);
		m_actors.push(actor);
		actor->m_source = "";
		actor->m_entity = entity;

		physx::PxBoxGeometry geom;
		geom.halfExtents.x = 1;
		geom.halfExtents.y = 1;
		geom.halfExtents.z = 1;
		physx::PxTransform transform;
		Matrix mtx;
		entity.getMatrix(mtx);
		matrix2Transform(mtx, transform);

		physx::PxRigidStatic* physx_actor = PxCreateStatic(*m_system->m_impl->m_physics, transform, geom, *m_default_material);
		physx_actor->userData = (void*)entity.index;
		m_scene->addActor(*physx_actor);
		actor->m_physx_actor = physx_actor;
		physx_actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);

		Component cmp = m_universe->addComponent(entity, BOX_ACTOR_HASH, this, m_actors.size() - 1);
		m_universe->componentCreated().invoke(cmp);
		return cmp;
	}


	Component createMeshRigidActor(Entity entity)
	{
		RigidActor* actor = LUMIX_NEW(RigidActor);
		m_actors.push(actor);
		actor->m_source = "";
		actor->m_entity = entity;
		actor->m_physx_actor = NULL;

		Component cmp = m_universe->addComponent(entity, MESH_ACTOR_HASH, this, m_actors.size() - 1);
		m_universe->componentCreated().invoke(cmp);
		return cmp;
	}


	virtual void getHeightmap(Component cmp, string& str) override
	{
		str = m_terrains[cmp.index]->m_heightmap ? m_terrains[cmp.index]->m_heightmap->getPath().c_str() : "";
	}


	virtual void getHeightmapXZScale(Component cmp, float& scale) override
	{
		scale = m_terrains[cmp.index]->m_xz_scale;
	}


	virtual void setHeightmapXZScale(Component cmp, const float& scale) override
	{
		if (scale != m_terrains[cmp.index]->m_xz_scale)
		{
			m_terrains[cmp.index]->m_xz_scale = scale;
			heightmapLoaded(m_terrains[cmp.index]);
		}
	}


	virtual void getHeightmapYScale(Component cmp, float& scale) override
	{
		scale = m_terrains[cmp.index]->m_y_scale;
	}


	virtual void setHeightmapYScale(Component cmp, const float& scale) override
	{
		if (scale != m_terrains[cmp.index]->m_y_scale)
		{
			m_terrains[cmp.index]->m_y_scale = scale;
			if (m_terrains[cmp.index]->m_heightmap)
			{
				heightmapLoaded(m_terrains[cmp.index]);
			}
		}
	}


	virtual void setHeightmap(Component cmp, const string& str) override
	{
		if (m_terrains[cmp.index]->m_heightmap)
		{
			m_engine->getResourceManager().get(ResourceManager::TEXTURE)->unload(*m_terrains[cmp.index]->m_heightmap);
			m_terrains[cmp.index]->m_heightmap->getObserverCb().unbind<Terrain, &Terrain::heightmapLoaded>(m_terrains[cmp.index]);
		}
		m_terrains[cmp.index]->m_heightmap = static_cast<Texture*>(m_engine->getResourceManager().get(ResourceManager::TEXTURE)->load(str.c_str()));
		m_terrains[cmp.index]->m_heightmap->getObserverCb().bind<Terrain, &Terrain::heightmapLoaded>(m_terrains[cmp.index]);
		m_terrains[cmp.index]->m_heightmap->addDataReference();
		if (m_terrains[cmp.index]->m_heightmap->isReady())
		{
			m_terrains[cmp.index]->heightmapLoaded(Resource::State::LOADING, Resource::State::READY);
		}
	}


	virtual void getShapeSource(Component cmp, string& str) override
	{
		str = m_actors[cmp.index]->m_source;
	}


	virtual void setShapeSource(Component cmp, const string& str) override
	{
		bool is_dynamic = false;
		getIsDynamic(cmp, is_dynamic);
		if (m_actors[cmp.index]->m_source == str && (!m_actors[cmp.index]->m_physx_actor || is_dynamic == !m_actors[cmp.index]->m_physx_actor->isRigidStatic()))
		{
			return;
		}

		physx::PxTriangleMeshGeometry geom;
		createTriMesh(str.c_str(), geom);

		physx::PxTransform transform;
		Matrix mtx;
		cmp.entity.getMatrix(mtx);
		matrix2Transform(mtx, transform);

		if (m_actors[cmp.index] && m_actors[cmp.index]->m_physx_actor)
		{
			m_scene->removeActor(*m_actors[cmp.index]->m_physx_actor);
			m_actors[cmp.index]->m_physx_actor->release();
			m_actors[cmp.index]->m_physx_actor = NULL;
		}

		physx::PxRigidActor* actor;
		if (is_dynamic)
		{
			actor = PxCreateDynamic(*m_system->m_impl->m_physics, transform, geom, *m_default_material, 1.0f);
		}
		else
		{
			actor = PxCreateStatic(*m_system->m_impl->m_physics, transform, geom, *m_default_material);
		}
		if (actor)
		{
			actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
			actor->userData = (void*)cmp.entity.index;
			m_scene->addActor(*actor);
			m_actors[cmp.index]->m_physx_actor = actor;
			m_actors[cmp.index]->m_source = str;
		}
		else
		{
			g_log_error.log("PhysX") << "Could not create PhysX mesh " << str.c_str();
		}
	}


	void createTriMesh(const char* path, physx::PxTriangleMeshGeometry& geom)
	{
		FILE* fp;
		fopen_s(&fp, path, "rb");
		if (fp)
		{
			Array<Vec3> verts;
			int num_verts, num_indices;
			Array<uint32_t> tris;

			fread(&num_verts, sizeof(num_verts), 1, fp);
			verts.resize(num_verts);
			fread(&verts[0], sizeof(Vec3), num_verts, fp);
			fread(&num_indices, sizeof(num_indices), 1, fp);
			tris.resize(num_indices);
			fread(&tris[0], sizeof(uint32_t), num_indices, fp);
			physx::PxTriangleMeshDesc meshDesc;
			meshDesc.points.count = num_verts;
			meshDesc.points.stride = sizeof(physx::PxVec3);
			meshDesc.points.data = &verts[0];

			meshDesc.triangles.count = num_indices / 3;
			meshDesc.triangles.stride = 3 * sizeof(physx::PxU32);
			meshDesc.triangles.data = &tris[0];

			for (int i = 0; i < num_indices; ++i)
			{
				ASSERT(tris[i] < (uint32_t)verts.size());
			}

			OutputStream writeBuffer;
			bool status = m_system->m_impl->m_cooking->cookTriangleMesh(meshDesc, writeBuffer);

			InputStream readBuffer(writeBuffer.data, writeBuffer.size);
			geom.triangleMesh = m_system->m_impl->m_physics->createTriangleMesh(readBuffer);
			fclose(fp);
		}
	}


	void createConvexGeom(const char* path, physx::PxConvexMeshGeometry& geom)
	{
		FILE* fp;
		fopen_s(&fp, path, "rb");
		if (fp)
		{
			fseek(fp, 0, SEEK_END);
			long size = ftell(fp);
			fseek(fp, 0, SEEK_SET);
			Array<Vec3> vertices;
			vertices.resize(size / sizeof(Vec3));
			fread(&vertices[0], size, 1, fp);
			fclose(fp);
			physx::PxConvexMeshDesc meshDesc;
			meshDesc.points.count = vertices.size();
			meshDesc.points.stride = sizeof(Vec3);
			meshDesc.points.data = &vertices[0];
			meshDesc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

			OutputStream writeBuffer;
			bool status = m_system->m_impl->m_cooking->cookConvexMesh(meshDesc, writeBuffer);
			if (!status)
				return;

			InputStream readBuffer(writeBuffer.data, writeBuffer.size);
			physx::PxConvexMesh* mesh = m_system->m_impl->m_physics->createConvexMesh(readBuffer);
			geom.convexMesh = mesh;
		}
	}


	void setControllerPosition(int index, const Vec3& pos)
	{
		physx::PxExtendedVec3 p(pos.x, pos.y, pos.z);
		m_controllers[index].m_controller->setPosition(p);
	}


	virtual void render() override
	{
		m_scene->getNbActors(physx::PxActorTypeSelectionFlag::eRIGID_STATIC);
		const physx::PxRenderBuffer& rb = m_scene->getRenderBuffer();
		const physx::PxU32 num_lines = rb.getNbLines();
		const physx::PxU32 num_points = rb.getNbPoints();
		const physx::PxU32 num_tri = rb.getNbTriangles();
		if (num_lines)
		{
			glBegin(GL_LINES);
			const physx::PxDebugLine* PX_RESTRICT lines = rb.getLines();
			for (physx::PxU32 i = 0; i < num_lines; ++i)
			{
				const physx::PxDebugLine& line = lines[i];
				GLubyte bytes[3];
				bytes[0] = (GLubyte)((line.color0 >> 16) & 0xff);
				bytes[1] = (GLubyte)((line.color0 >> 8) & 0xff);
				bytes[2] = (GLubyte)((line.color0) & 0xff);
				glColor3ubv(bytes);
				glVertex3fv((GLfloat*)&line.pos0);
				glVertex3fv((GLfloat*)&line.pos1);
			}
			glEnd();
		}
	}


	virtual void update(float time_delta) override
	{
		time_delta = 0.01f;
		m_scene->simulate(time_delta);
		m_scene->fetchResults(true);
		for (int i = 0; i < m_dynamic_actors.size(); ++i)
		{
			physx::PxTransform trans = m_dynamic_actors[i]->m_physx_actor->getGlobalPose();
			m_dynamic_actors[i]->m_entity.setPosition(trans.p.x, trans.p.y, trans.p.z);
			m_dynamic_actors[i]->m_entity.setRotation(trans.q.x, trans.q.y, trans.q.z, trans.q.w);

		}
		physx::PxVec3 g(0, time_delta * -9.8f, 0);
		for (int i = 0; i < m_controllers.size(); ++i)
		{
			if(!m_controllers[i].m_is_free)
			{
				const physx::PxExtendedVec3& p = m_controllers[i].m_controller->getPosition();
				m_controllers[i].m_controller->move(g, 0.0001f, time_delta, physx::PxControllerFilters());
				m_controllers[i].m_entity.setPosition((float)p.x, (float)p.y, (float)p.z);
			}
		}
	}


	virtual void moveController(Component cmp, const Vec3& v, float dt) override
	{
		m_controllers[cmp.index].m_controller->move(physx::PxVec3(v.x, v.y, v.z), 0.001f, dt, physx::PxControllerFilters());
	}


	virtual bool raycast(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result) override
	{
		physx::PxVec3 physx_origin(origin.x, origin.y, origin.z);
		physx::PxVec3 unit_dir(dir.x, dir.y, dir.z);
		physx::PxReal max_distance = distance;
		physx::PxRaycastHit hit;

		const physx::PxSceneQueryFlags outputFlags = physx::PxSceneQueryFlag::eDISTANCE | physx::PxSceneQueryFlag::eIMPACT | physx::PxSceneQueryFlag::eNORMAL;

		bool status = m_scene->raycastSingle(physx_origin, unit_dir, max_distance, outputFlags, hit);
		result.normal.x = hit.normal.x;
		result.normal.y = hit.normal.y;
		result.normal.z = hit.normal.z;
		result.position.x = hit.impact.x;
		result.position.y = hit.impact.y;
		result.position.z = hit.impact.z;
		result.entity.index = -1;
		if (hit.shape)
		{
			physx::PxRigidActor& actor = hit.shape->getActor();
			if (actor.userData)
				result.entity.index = (int)actor.userData;
		}
		return status;
	}


	void onEntityMoved(Entity& entity)
	{
		const Entity::ComponentList& cmps = entity.getComponents();
		for (int i = 0, c = cmps.size(); i < c; ++i)
		{
			if (cmps[i].type == BOX_ACTOR_HASH)
			{
				Vec3 pos = entity.getPosition();
				physx::PxVec3 pvec(pos.x, pos.y, pos.z);
				Quat q;
				entity.getMatrix().getRotation(q);
				physx::PxQuat pquat(q.x, q.y, q.z, q.w);
				physx::PxTransform trans(pvec, pquat);
				if (m_actors[cmps[i].index])
				{
					m_actors[cmps[i].index]->m_physx_actor->setGlobalPose(trans, false);
				}
			}
			else if (cmps[i].type == CONTROLLER_HASH)
			{
				Vec3 pos = entity.getPosition();
				physx::PxExtendedVec3 pvec(pos.x, pos.y, pos.z);
				m_controllers[cmps[i].index].m_controller->setPosition(pvec);
			}
		}
	}


	void heightmapLoaded(Terrain* terrain)
	{
		PROFILE_FUNCTION();
		Array<physx::PxHeightFieldSample> heights;

		int width = terrain->m_heightmap->getWidth();
		int height = terrain->m_heightmap->getHeight();
		heights.resize(width * height);
		int bytes_per_pixel = terrain->m_heightmap->getBytesPerPixel();
		if (bytes_per_pixel == 2)
		{
			PROFILE_BLOCK("copyData");
			const uint16_t* LUMIX_RESTRICT data = (const uint16_t*)terrain->m_heightmap->getData();
			for (int j = 0; j < height; ++j)
			{
				int idx = j * width;
				for (int i = 0; i < width; ++i)
				{
					int idx2 = j + i * height;
					heights[idx].height = data[idx2];
					heights[idx].materialIndex0 = heights[idx].materialIndex1 = 0;
					++idx;
				}
			}
		}
		else
		{
			PROFILE_BLOCK("copyData");
			const uint8_t* data = terrain->m_heightmap->getData();
			for (int j = 0; j < height; ++j)
			{
				for (int i = 0; i < width; ++i)
				{
					int idx = i + j * width;
					int idx2 = j + i * height;
					heights[idx].height = data[idx2 * bytes_per_pixel];
					heights[idx].materialIndex0 = heights[idx].materialIndex1 = 0;
				}
			}
		}

		//terrain->m_heightmap->removeDataReference();

		{ // PROFILE_BLOCK scope
			PROFILE_BLOCK("PhysX");
			physx::PxHeightFieldDesc hfDesc;
			hfDesc.format = physx::PxHeightFieldFormat::eS16_TM;
			hfDesc.nbColumns = width;
			hfDesc.nbRows = height;
			hfDesc.samples.data = &heights[0];
			hfDesc.samples.stride = sizeof(physx::PxHeightFieldSample);
			hfDesc.thickness = -1;

			physx::PxHeightField* heightfield = m_system->m_impl->m_physics->createHeightField(hfDesc);
			float height_scale = bytes_per_pixel == 2 ? 1 / (256 * 256.0f - 1) : 1 / 255.0f;
			physx::PxHeightFieldGeometry hfGeom(heightfield, physx::PxMeshGeometryFlags(), height_scale * terrain->m_y_scale, terrain->m_xz_scale, terrain->m_xz_scale);
			if (terrain->m_actor)
			{
				physx::PxRigidActor* actor = terrain->m_actor;
				m_scene->removeActor(*actor);
				actor->release();
				terrain->m_actor = NULL;
			}

			physx::PxTransform transform;
			Matrix mtx;
			terrain->m_entity.getMatrix(mtx);
			matrix2Transform(mtx, transform);

			physx::PxRigidActor* actor;
			actor = PxCreateStatic(*m_system->m_impl->m_physics, transform, hfGeom, *m_default_material);
			if (actor)
			{
				actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, width <= 1024);
				actor->userData = (void*)terrain->m_entity.index;
				m_scene->addActor(*actor);
				terrain->m_actor = actor;
			}
			else
			{
				g_log_error.log("PhysX") << "Could not create PhysX heightfield " << terrain->m_heightmap->getPath().c_str();
			}
		}
	}


	bool isDynamic(int index)
	{
		RigidActor* actor = m_actors[index];
		for (int i = 0, c = m_dynamic_actors.size(); i < c; ++i)
		{
			if (m_dynamic_actors[i] == actor)
			{
				return true;
			}
		}
		return false;
	}


	virtual void getIsDynamic(Component cmp, bool& is) override
	{
		is = isDynamic(cmp.index);
	}


	virtual void getHalfExtents(Component cmp, Vec3& size) override
	{
		physx::PxRigidActor* actor = m_actors[cmp.index]->m_physx_actor;
		physx::PxShape* shapes;
		if (actor->getNbShapes() == 1 && m_actors[cmp.index]->m_physx_actor->getShapes(&shapes, 1))
		{
			physx::PxVec3& half = shapes->getGeometry().box().halfExtents;
			size.x = half.x;
			size.y = half.y;
			size.z = half.z;
		}
	}


	virtual void setHalfExtents(Component cmp, const Vec3& size) override
	{
		physx::PxRigidActor* actor = m_actors[cmp.index]->m_physx_actor;
		physx::PxShape* shapes;
		if (actor->getNbShapes() == 1 && m_actors[cmp.index]->m_physx_actor->getShapes(&shapes, 1))
		{
			physx::PxBoxGeometry box;
			bool is_box = shapes->getBoxGeometry(box);
			ASSERT(is_box);
			physx::PxVec3& half = box.halfExtents;
			half.x = size.x;
			half.y = size.y;
			half.z = size.z;
			shapes->setGeometry(box);
		}
	}


	virtual void setIsDynamic(Component cmp, const bool& new_value) override
	{
		int dynamic_index = -1;
		RigidActor* actor = m_actors[cmp.index];
		for (int i = 0, c = m_dynamic_actors.size(); i < c; ++i)
		{
			if (m_dynamic_actors[i] == actor)
			{
				dynamic_index = i;
				break;
			}
		}
		bool is_dynamic = dynamic_index != -1;
		if (is_dynamic != new_value)
		{
			if (new_value)
			{
				m_dynamic_actors.push(actor);
			}
			else
			{
				m_dynamic_actors.eraseItemFast(actor);
			}
			physx::PxShape* shapes;
			if (m_actors[cmp.index]->m_physx_actor->getNbShapes() == 1 && m_actors[cmp.index]->m_physx_actor->getShapes(&shapes, 1, 0))
			{
				physx::PxGeometryHolder geom = shapes->getGeometry();

				physx::PxTransform transform;
				matrix2Transform(cmp.entity.getMatrix(), transform);

				physx::PxRigidActor* actor;
				if (new_value)
				{
					actor = PxCreateDynamic(*m_system->m_impl->m_physics, transform, geom.any(), *m_default_material, 1.0f);
				}
				else
				{
					actor = PxCreateStatic(*m_system->m_impl->m_physics, transform, geom.any(), *m_default_material);
				}
				ASSERT(actor);
				m_scene->removeActor(*m_actors[cmp.index]->m_physx_actor);
				m_actors[cmp.index]->m_physx_actor->release();
				m_scene->addActor(*actor);
				m_actors[cmp.index]->m_physx_actor = actor;
			}
		}
	}


	void serializeActor(ISerializer& serializer, int idx)
	{
		physx::PxShape* shapes;
		if (m_actors[idx]->m_physx_actor->getNbShapes() == 1 && m_actors[idx]->m_physx_actor->getShapes(&shapes, 1))
		{
			physx::PxBoxGeometry geom;
			physx::PxHeightFieldGeometry hf_geom;
			if (shapes->getBoxGeometry(geom))
			{
				serializer.serialize("type", (int32_t)BOX);
				serializer.serialize("x", geom.halfExtents.x);
				serializer.serialize("y", geom.halfExtents.y);
				serializer.serialize("z", geom.halfExtents.z);
			}
			else
			{
				ASSERT(false);
			}
		}
		else
		{
			ASSERT(false);
		}
	}


	void deserializeActor(ISerializer& serializer, int idx)
	{
		ActorType type;
		serializer.deserialize("type", (int32_t&)type);
		physx::PxTransform transform;
		Matrix mtx;
		m_actors[idx]->m_entity.getMatrix(mtx);
		matrix2Transform(mtx, transform);

		physx::PxGeometry* geom;
		physx::PxBoxGeometry box_geom;
		switch (type)
		{
			case BOX:
				{
					serializer.deserialize("x", box_geom.halfExtents.x);
					serializer.deserialize("y", box_geom.halfExtents.y);
					serializer.deserialize("z", box_geom.halfExtents.z);
					geom = &box_geom;
				}
				break;
			default:
				ASSERT(false);
				break;
		}

		physx::PxRigidActor* actor;
		if (isDynamic(idx))
		{
			actor = PxCreateDynamic(*m_system->m_impl->m_physics, transform, *geom, *m_default_material, 1.0f);
		}
		else
		{
			actor = PxCreateStatic(*m_system->m_impl->m_physics, transform, *geom, *m_default_material);
		}
		actor->userData = (void*)m_actors[idx]->m_entity.index;
		m_scene->addActor(*actor);
		m_actors[idx]->m_physx_actor = actor;
		actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);

		m_universe->addComponent(m_actors[idx]->m_entity, BOX_ACTOR_HASH, this, idx);
	}


	virtual void serialize(ISerializer& serializer) override
	{
		serializer.serialize("count", m_actors.size());
		serializer.beginArray("actors");
		for (int i = 0; i < m_actors.size(); ++i)
		{
			serializer.serializeArrayItem(m_actors[i]->m_source);
			serializer.serializeArrayItem(isDynamic(i));
			serializer.serializeArrayItem(m_actors[i]->m_entity.index);
			if(m_actors[i]->m_entity.index != -1)
			{
				serializeActor(serializer, i);
			}
		}
		serializer.endArray();
		serializer.serialize("count", m_controllers.size());
		serializer.beginArray("controllers");
		for (int i = 0; i < m_controllers.size(); ++i)
		{
			serializer.serializeArrayItem(m_controllers[i].m_entity.index);
			serializer.serializeArrayItem(m_controllers[i].m_is_free);
		}
		serializer.endArray();
		serializer.serialize("count", m_terrains.size());
		serializer.beginArray("terrains");
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			if(m_terrains[i])
			{
				serializer.serializeArrayItem(true);
				serializer.serializeArrayItem(m_terrains[i]->m_entity.index);
				serializer.serializeArrayItem(m_terrains[i]->m_heightmap->getPath().c_str());
				serializer.serializeArrayItem(m_terrains[i]->m_xz_scale);
				serializer.serializeArrayItem(m_terrains[i]->m_y_scale);
			}
			else
			{
				serializer.serializeArrayItem(false);
			}
		}
		serializer.endArray();
	}


	void deserializeActors(ISerializer& serializer)
	{
		int32_t count;
		m_dynamic_actors.clear();
		serializer.deserialize("count", count);
		for (int i = count; i < m_actors.size(); ++i)
		{
			m_actors[i]->m_physx_actor->release();
		}
		m_actors.resize(count);
		serializer.deserializeArrayBegin("actors");
		for (int i = 0; i < m_actors.size(); ++i)
		{
			serializer.deserializeArrayItem(m_actors[i]->m_source);
			bool is_dynamic;
			serializer.deserializeArrayItem(is_dynamic);
			if (is_dynamic)
			{
				m_dynamic_actors.push(m_actors[i]);
			}
			serializer.deserializeArrayItem(m_actors[i]->m_entity.index);
			if(m_actors[i]->m_entity.index != -1)
			{
				m_actors[i]->m_entity.universe = m_universe;
				deserializeActor(serializer, i);
			}
		}
		serializer.deserializeArrayEnd();
	}


	void deserializeControllers(ISerializer& serializer)
	{
		int32_t count;
		serializer.deserialize("count", count);
		for (int i = 0; i < m_controllers.size(); ++i)
		{
			m_controllers[i].m_controller->release();
		}
		m_controllers.clear();
		serializer.deserializeArrayBegin("controllers");
		for (int i = 0; i < count; ++i)
		{
			int index;
			bool is_free;
			serializer.deserializeArrayItem(index);
			serializer.deserializeArrayItem(is_free);
			Entity e(m_universe, index);

			Controller c;
			c.m_is_free = is_free;
			m_controllers.push(c);
			
			if(!is_free)
			{
				physx::PxCapsuleControllerDesc cDesc;
				cDesc.material = m_default_material;
				cDesc.height = 1.8f;
				cDesc.radius = 0.25f;
				cDesc.slopeLimit = 0.0f;
				cDesc.contactOffset = 0.1f;
				cDesc.stepOffset = 0.02f;
				cDesc.callback = NULL;
				cDesc.behaviorCallback = NULL;
				Vec3 position = e.getPosition();
				cDesc.position.set(position.x, position.y, position.z);
				c.m_controller = m_system->m_impl->m_controller_manager->createController(*m_system->m_impl->m_physics, m_scene, cDesc);
				c.m_entity = e;
				m_universe->addComponent(e, CONTROLLER_HASH, this, i);
			}
		}
		serializer.deserializeArrayEnd();
	}


	void deserializeTerrains(ISerializer& serializer)
	{
		int32_t count;
		serializer.deserialize("count", count);
		for (int i = count; i < m_terrains.size(); ++i)
		{
			LUMIX_DELETE(m_terrains[i]);
			m_terrains[i] = NULL;
		}
		int old_size = m_terrains.size();
		m_terrains.resize(count);
		serializer.deserializeArrayBegin("terrains");
		for (int i = 0; i < count; ++i)
		{
			bool exists;
			serializer.deserializeArrayItem(exists);
			if(exists)
			{
				if(!m_terrains[i])
				{
					m_terrains[i] = LUMIX_NEW(Terrain);
				}
				m_terrains[i]->m_scene = this;
				m_terrains[i]->m_entity.universe = m_universe;
				serializer.deserializeArrayItem(m_terrains[i]->m_entity.index);
				char tmp[LUMIX_MAX_PATH];
				serializer.deserializeArrayItem(tmp, LUMIX_MAX_PATH);
				serializer.deserializeArrayItem(m_terrains[i]->m_xz_scale);
				serializer.deserializeArrayItem(m_terrains[i]->m_y_scale);

				Component cmp(m_terrains[i]->m_entity, HEIGHTFIELD_HASH, this, i);
				if (m_terrains[i]->m_heightmap == NULL || strcmp(tmp, m_terrains[i]->m_heightmap->getPath().c_str()) != 0)
				{
					setHeightmap(cmp, string(tmp));
				}
				m_universe->addComponent(m_terrains[i]->m_entity, HEIGHTFIELD_HASH, this, i);
			}
		}
		serializer.deserializeArrayEnd();
	}


	virtual void deserialize(ISerializer& serializer) override
	{
		deserializeActors(serializer);
		deserializeControllers(serializer);
		deserializeTerrains(serializer);
	}


	virtual PhysicsSystem& getSystem() const override
	{
		return *m_system;
	}

	struct RigidActor
	{
		physx::PxRigidActor* m_physx_actor;
		string m_source;
		Entity m_entity;
	};

	struct Controller
	{
		physx::PxController* m_controller;
		Entity m_entity;
		bool m_is_free;
	};

	Universe*					m_universe;
	Engine*						m_engine;
	physx::PxScene*				m_scene;
	PhysicsSystem*				m_system;
	physx::PxMaterial*			m_default_material;
	Array<RigidActor*>			m_actors;
	Array<RigidActor*>			m_dynamic_actors;

	Array<Controller>			m_controllers;
	Array<Terrain*>				m_terrains;
};


	
PhysicsScene* PhysicsScene::create(PhysicsSystem& system, Universe& universe, Engine& engine)
{
	PhysicsSceneImpl* impl = LUMIX_NEW(PhysicsSceneImpl);
	impl->m_universe = &universe;
	impl->m_universe->entityMoved().bind<PhysicsSceneImpl, &PhysicsSceneImpl::onEntityMoved>(impl);
	impl->m_engine = &engine;
	physx::PxSceneDesc sceneDesc(system.m_impl->m_physics->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(0.0f, -9.8f, 0.0f);
	if (!sceneDesc.cpuDispatcher) 
	{
		physx::PxDefaultCpuDispatcher* cpu_dispatcher = physx::PxDefaultCpuDispatcherCreate(1);
		if (!cpu_dispatcher)
		{
			g_log_error.log("physics") << "PxDefaultCpuDispatcherCreate failed!";
		}
		sceneDesc.cpuDispatcher = cpu_dispatcher;
	} 
	if (!sceneDesc.filterShader)
	{
		sceneDesc.filterShader = &physx::PxDefaultSimulationFilterShader;
	}

	impl->m_scene = system.m_impl->m_physics->createScene(sceneDesc);
	if (!impl->m_scene)
	{
		LUMIX_DELETE(impl)
		return NULL;
	}
	
	/*impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
	impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, 1.0);
	impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eACTOR_AXES, 1.0f);
	impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_AABBS, 1.0f);
	impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eWORLD_AXES, 1.0f);
	impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCONTACT_POINT, 1.0f);*/
	impl->m_system = &system;
	impl->m_default_material = impl->m_system->m_impl->m_physics->createMaterial(0.5,0.5,0.5);
	return impl;
}


void PhysicsScene::destroy(PhysicsScene* scene)
{
	PhysicsSceneImpl* impl = static_cast<PhysicsSceneImpl*>(scene);
	impl->m_default_material->release();
	impl->m_scene->release();
	LUMIX_DELETE(scene);
}


Terrain::Terrain()
{
	m_heightmap = NULL;
	m_xz_scale = 1.0f;
	m_y_scale = 1.0f;
	m_actor = NULL;
}


Terrain::~Terrain()
{
	if (m_heightmap)
	{
		m_heightmap->getResourceManager().get(ResourceManager::TEXTURE)->unload(*m_heightmap);
		m_heightmap->getObserverCb().unbind<Terrain, &Terrain::heightmapLoaded>(this);
	}
}


void Terrain::heightmapLoaded(Resource::State, Resource::State new_state)
{
	if (new_state == Resource::State::READY)
	{
		m_scene->heightmapLoaded(this);
	}
}



} // !namespace Lumix
