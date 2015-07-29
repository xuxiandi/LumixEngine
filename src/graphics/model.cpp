#include "core/lumix.h"
#include "graphics/model.h"

#include "core/array.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec3.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/model_manager.h"
#include "graphics/pose.h"

#include <cfloat>


namespace Lumix
{

		
Model::~Model()
{
	ASSERT(isEmpty());
}


RayCastModelHit Model::castRay(const Vec3& origin, const Vec3& dir, const Matrix& model_transform, float scale)
{
	RayCastModelHit hit;
	hit.m_is_hit = false;
	if(!isReady())
	{
		return hit;
	}
	
	Matrix inv = model_transform;
	inv.multiply3x3(scale);
	inv.inverse();
	Vec3 local_origin = inv.multiplyPosition(origin);
	Vec3 local_dir = static_cast<Vec3>(inv * Vec4(dir.x, dir.y, dir.z, 0));

	const Array<Vec3>& vertices = m_vertices;
	const Array<int32_t>& indices = m_indices;
	int vertex_offset = 0;
	for (int mesh_index = 0; mesh_index < m_meshes.size(); ++mesh_index)
	{
		int indices_end = m_meshes[mesh_index].getIndicesOffset() + m_meshes[mesh_index].getIndexCount();
		for (int i = m_meshes[mesh_index].getIndicesOffset(); i < indices_end; i += 3)
		{
			Vec3 p0 = vertices[vertex_offset + indices[i]];
			Vec3 p1 = vertices[vertex_offset + indices[i + 1]];
			Vec3 p2 = vertices[vertex_offset + indices[i + 2]];
			Vec3 normal = crossProduct(p1 - p0, p2 - p0);
			float q = dotProduct(normal, local_dir);
			if (q == 0)
			{
				continue;
			}
			float d = -dotProduct(normal, p0);
			float t = -(dotProduct(normal, local_origin) + d) / q;
			if (t < 0)
			{
				continue;
			}
			Vec3 hit_point = local_origin + local_dir * t;

			Vec3 edge0 = p1 - p0;
			Vec3 VP0 = hit_point - p0;
			if (dotProduct(normal, crossProduct(edge0, VP0)) < 0)
			{
				continue;
			}

			Vec3 edge1 = p2 - p1;
			Vec3 VP1 = hit_point - p1;
			if (dotProduct(normal, crossProduct(edge1, VP1)) < 0)
			{
				continue;
			}

			Vec3 edge2 = p0 - p2;
			Vec3 VP2 = hit_point - p2;
			if (dotProduct(normal, crossProduct(edge2, VP2)) < 0)
			{
				continue;
			}

			if (!hit.m_is_hit || hit.m_t > t)
			{
				hit.m_is_hit = true;
				hit.m_t = t;
				hit.m_mesh = &m_meshes[mesh_index];
			}
		}
		vertex_offset += m_meshes[mesh_index].getAttributeArraySize() / m_meshes[mesh_index].getVertexDefinition().getStride();
	}
	hit.m_origin = origin;
	hit.m_dir = dir;
	return hit;
}


LODMeshIndices Model::getLODMeshIndices(float squared_distance) const
{
	int i = 0;
	while (squared_distance >= m_lods[i].m_distance)
	{
		++i;
	}
	return LODMeshIndices(m_lods[i].m_from_mesh, m_lods[i].m_to_mesh);
}


void Model::getPose(Pose& pose)
{
	ASSERT(pose.getCount() == getBoneCount());
	Vec3* pos =	pose.getPositions();
	Quat* rot = pose.getRotations();
	Matrix mtx;
	for(int i = 0, c = getBoneCount(); i < c; ++i) 
	{
		mtx = m_bones[i].inv_bind_matrix;
		mtx.fastInverse();
		mtx.getTranslation(pos[i]);
		mtx.getRotation(rot[i]);
	}
}


bool Model::parseVertexDef(FS::IFile* file, bgfx::VertexDecl* vertex_definition)
{
	vertex_definition->begin();

	uint32_t attribute_count;
	file->read(&attribute_count, sizeof(attribute_count));

	for (uint32_t i = 0; i < attribute_count; ++i)
	{
		char tmp[50];
		uint32_t len;
		file->read(&len, sizeof(len));
		if (len > sizeof(tmp) - 1)
		{
			return false;
		}
		file->read(tmp, len);
		tmp[len] = '\0';

		if (strcmp(tmp, "in_position") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);
		}
		else if (strcmp(tmp, "in_tex_coords") == 0)
		{
			vertex_definition->add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
		}
		else if (strcmp(tmp, "in_normal") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Normal, 4, bgfx::AttribType::Uint8, true, true);
		}
		else if (strcmp(tmp, "in_tangents") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Uint8, true, true);
		}
		else if (strcmp(tmp, "in_weights") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float);
		}
		else if (strcmp(tmp, "in_indices") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Int16, false, true);
		}
		else
		{
			ASSERT(false);
			return false;
		}

		uint32_t type;
		file->read(&type, sizeof(type));
	}

	vertex_definition->end();
	return true;
}


void Model::create(const bgfx::VertexDecl& def, Material* material, const int* indices_data, int indices_size, const void* attributes_data, int attributes_size)
{
	m_geometry_buffer_object.setAttributesData(attributes_data, attributes_size, def);
	m_geometry_buffer_object.setIndicesData(indices_data, indices_size);

	m_meshes.emplace(def, material, 0, attributes_size, 0, indices_size / sizeof(int), "default", m_allocator);

	Model::LOD lod;
	lod.m_distance = FLT_MAX;
	lod.m_from_mesh = 0;
	lod.m_to_mesh = 0;
	m_lods.push(lod);

	m_indices.resize(indices_size / sizeof(m_indices[0]));
	memcpy(&m_indices[0], indices_data, indices_size);

	m_vertices.resize(attributes_size / def.getStride());
	computeRuntimeData((const uint8_t*)attributes_data);

	onReady();
}


void Model::computeRuntimeData(const uint8_t* vertices)
{
	int index = 0;
	float bounding_radius_squared = 0;
	Vec3 min_vertex(0, 0, 0);
	Vec3 max_vertex(0, 0, 0);

	for (int i = 0; i < m_meshes.size(); ++i)
	{
		int mesh_vertex_count = m_meshes[i].getAttributeArraySize() / m_meshes[i].getVertexDefinition().getStride();
		int mesh_attributes_array_offset = m_meshes[i].getAttributeArrayOffset();
		int mesh_vertex_size = m_meshes[i].getVertexDefinition().getStride();
		int mesh_position_attribute_offset = m_meshes[i].getVertexDefinition().getOffset(bgfx::Attrib::Position);
		for (int j = 0; j < mesh_vertex_count; ++j)
		{
			m_vertices[index] = *(const Vec3*)&vertices[mesh_attributes_array_offset + j * mesh_vertex_size + mesh_position_attribute_offset];
			bounding_radius_squared = Math::maxValue(bounding_radius_squared, dotProduct(m_vertices[index], m_vertices[index]) > 0 ? m_vertices[index].squaredLength() : 0);
			min_vertex.x = Math::minValue(min_vertex.x, m_vertices[index].x);
			min_vertex.y = Math::minValue(min_vertex.y, m_vertices[index].y);
			min_vertex.z = Math::minValue(min_vertex.z, m_vertices[index].z);
			max_vertex.x = Math::maxValue(max_vertex.x, m_vertices[index].x);
			max_vertex.y = Math::maxValue(max_vertex.y, m_vertices[index].y);
			max_vertex.z = Math::maxValue(max_vertex.z, m_vertices[index].z);
			++index;
		}
	}

	m_bounding_radius = sqrt(bounding_radius_squared);
	m_aabb = AABB(min_vertex, max_vertex);
}


bool Model::parseGeometry(FS::IFile* file)
{
	int32_t indices_count = 0;
	file->read(&indices_count, sizeof(indices_count));
	if (indices_count <= 0)
	{
		return false;
	}
	m_indices.resize(indices_count);
	file->read(&m_indices[0], sizeof(m_indices[0]) * indices_count);
	
	int32_t vertices_size = 0;
	file->read(&vertices_size, sizeof(vertices_size));
	if (vertices_size <= 0)
	{
		return false;
	}

	Array<uint8_t> vertices(m_allocator);
	vertices.resize(vertices_size);
	file->read(&vertices[0], sizeof(vertices[0]) * vertices.size());
	
	m_geometry_buffer_object.setAttributesData(&vertices[0], vertices.size(), m_meshes[0].getVertexDefinition());
	m_geometry_buffer_object.setIndicesData(&m_indices[0], m_indices.size() * sizeof(m_indices[0]));

	int vertex_count = 0;
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		vertex_count += m_meshes[i].getAttributeArraySize() / m_meshes[i].getVertexDefinition().getStride();
	}
	m_vertices.resize(vertex_count);
	
	computeRuntimeData(&vertices[0]);

	return true;
}

bool Model::parseBones(FS::IFile* file)
{
	int bone_count;
	file->read(&bone_count, sizeof(bone_count));
	if (bone_count < 0)
	{
		return false;
	}
	m_bones.reserve(bone_count);
	for (int i = 0; i < bone_count; ++i)
	{
		Model::Bone& b = m_bones.emplace(m_allocator);
		int len;
		file->read(&len, sizeof(len));
		char tmp[MAX_PATH_LENGTH];
		if (len >= MAX_PATH_LENGTH)
		{
			return false;
		}
		file->read(tmp, len);
		tmp[len] = 0;
		b.name = tmp;
		m_bone_map.insert(crc32(b.name.c_str()), m_bones.size() - 1);
		file->read(&len, sizeof(len));
		if (len >= MAX_PATH_LENGTH)
		{
			return false;
		}
		file->read(tmp, len);
		tmp[len] = 0;
		b.parent = tmp;
		file->read(&b.position.x, sizeof(float)* 3);
		file->read(&b.rotation.x, sizeof(float)* 4);
	}
	m_first_nonroot_bone_index = -1;
	for (int i = 0; i < bone_count; ++i)
	{
		Model::Bone& b = m_bones[i];
		if (b.parent.length() == 0)
		{
			b.parent_idx = -1;
		}
		else
		{
			b.parent_idx = getBoneIdx(b.parent.c_str());
			if (b.parent_idx > i || b.parent_idx < 0)
			{
				g_log_error.log("renderer") << "Invalid skeleton in " << getPath().c_str();
				return false;
			}
			if (m_first_nonroot_bone_index == -1)
			{
				m_first_nonroot_bone_index = i;
			}
		}
	}
	for (int i = 0; i < m_bones.size(); ++i)
	{
		m_bones[i].rotation.toMatrix(m_bones[i].inv_bind_matrix);
		m_bones[i].inv_bind_matrix.translate(m_bones[i].position);
	}
	for (int i = 0; i < m_bones.size(); ++i)
	{
		m_bones[i].inv_bind_matrix.fastInverse();
	}
	return true;
}

int Model::getBoneIdx(const char* name)
{
	for (int i = 0, c = m_bones.size(); i < c; ++i)
	{
		if (m_bones[i].name == name)
		{
			return i;
		}
	}
	return -1;
}

bool Model::parseMeshes(FS::IFile* file)
{
	int object_count = 0;
	file->read(&object_count, sizeof(object_count));
	if (object_count <= 0)
	{
		return false;
	}
	m_meshes.reserve(object_count);
	char model_dir[MAX_PATH_LENGTH];
	PathUtils::getDir(model_dir, MAX_PATH_LENGTH, m_path.c_str());
	for (int i = 0; i < object_count; ++i)
	{
		int32_t str_size;
		file->read(&str_size, sizeof(str_size));
		char material_name[MAX_PATH_LENGTH];
		file->read(material_name, str_size);
		if (str_size >= MAX_PATH_LENGTH)
		{
			return false;
		}
		material_name[str_size] = 0;
		
		char material_path[MAX_PATH_LENGTH];
		copyString(material_path, sizeof(material_path), model_dir);
		catCString(material_path, sizeof(material_path), material_name);
		catCString(material_path, sizeof(material_path), ".mat");
		Material* material = static_cast<Material*>(m_resource_manager.get(ResourceManager::MATERIAL)->load(Path(material_path)));

		int32_t attribute_array_offset = 0;
		file->read(&attribute_array_offset, sizeof(attribute_array_offset));
		int32_t attribute_array_size = 0;
		file->read(&attribute_array_size, sizeof(attribute_array_size));
		int32_t indices_offset = 0;
		file->read(&indices_offset, sizeof(indices_offset));
		int32_t mesh_tri_count = 0;
		file->read(&mesh_tri_count, sizeof(mesh_tri_count));

		file->read(&str_size, sizeof(str_size));
		if (str_size >= MAX_PATH_LENGTH)
		{
			return false;
		}
		char mesh_name[MAX_PATH_LENGTH];
		mesh_name[str_size] = 0;
		file->read(mesh_name, str_size);

		bgfx::VertexDecl def;
		parseVertexDef(file, &def);
		m_meshes.emplace(def, material, attribute_array_offset, attribute_array_size, indices_offset, mesh_tri_count * 3, mesh_name, m_allocator);
		addDependency(*material);
	}
	return true;
}


bool Model::parseLODs(FS::IFile* file)
{
	int32_t lod_count;
	file->read(&lod_count, sizeof(lod_count));
	if (lod_count <= 0)
	{
		return false;
	}
	m_lods.resize(lod_count);
	for (int i = 0; i < lod_count; ++i)
	{
		file->read(&m_lods[i].m_to_mesh, sizeof(m_lods[i].m_to_mesh));
		file->read(&m_lods[i].m_distance, sizeof(m_lods[i].m_distance));
		m_lods[i].m_from_mesh = i > 0 ? m_lods[i - 1].m_to_mesh + 1 : 0;
	}
	return true;
}


void Model::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{ 
	PROFILE_FUNCTION();
	if(success)
	{
		FileHeader header;
		file->read(&header, sizeof(header));
		if (header.m_magic == FILE_MAGIC
			&& header.m_version <= (uint32_t)FileVersion::LATEST
			&& parseMeshes(file)
			&& parseGeometry(file)
			&& parseBones(file)
			&& parseLODs(file))
		{
			m_size = file->size();
			decrementDepCount();
		}
		else
		{
			g_log_warning.log("renderer") << "Error loading model " << m_path.c_str();
			onFailure();
			fs.close(file);
			return;
		}
	}
	else
	{
		g_log_warning.log("renderer") << "Error loading model " << m_path.c_str();
		onFailure();
	}

	fs.close(file);
}

void Model::doUnload(void)
{
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		removeDependency(*m_meshes[i].getMaterial());
		m_resource_manager.get(ResourceManager::MATERIAL)->unload(*m_meshes[i].getMaterial());
	}
	m_meshes.clear();
	m_bones.clear();
	m_lods.clear();
	m_geometry_buffer_object.clear();

	m_size = 0;
	onEmpty();
}


} // ~namespace Lumix
